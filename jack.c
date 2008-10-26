/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008 Nedko Arnaudov <nedko@arnaudov.name>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <stdbool.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "list.h"
#include "structs.h"
#include "jack.h"
#include "log.h"
#include "port_hash.h"
#include "port.h"
#include "a2jmidid.h"

static bool g_freewheeling = false;

/*
 * =================== Input/output port handling =========================
 */
static
void
a2j_set_process_info(
  struct a2j_process_info * info,
  struct a2j * self,
  int dir,
  jack_nframes_t nframes)
{
  const snd_seq_real_time_t* alsa_time;
  snd_seq_queue_status_t *status;

  snd_seq_queue_status_alloca(&status);

  info->dir = dir;

  info->period_start = jack_last_frame_time(self->jack_client);
  info->nframes = nframes;
  info->sample_rate = jack_get_sample_rate(self->jack_client);

  info->cur_frames = jack_frame_time(self->jack_client);

  // immediately get alsa'a real time (uhh, why everybody has their on 'real' time)
  snd_seq_get_queue_status(self->seq, self->queue, status);
  alsa_time = snd_seq_queue_status_get_real_time(status);
  info->alsa_time = alsa_time->tv_sec * NSEC_PER_SEC + alsa_time->tv_nsec;

  if (info->period_start + info->nframes < info->cur_frames) {
    int periods_lost = (info->cur_frames - info->period_start) / info->nframes; 
    info->period_start += periods_lost * info->nframes;
    a2j_debug("xrun detected: %d periods lost", periods_lost);
  }
}

void
a2j_add_ports(
  struct a2j_stream * str)
{
  struct a2j_port *port;
  while (jack_ringbuffer_read(str->new_ports, (char*)&port, sizeof(port))) {
    a2j_debug("jack: inserted port %s", port->name);
    a2j_port_insert(str->port_hash, port);
  }
}

static
void
a2j_jack_process_internal(
  struct a2j * self,
  struct a2j_process_info * info_ptr)
{
  struct a2j_stream * stream_ptr;
  int i;
  struct a2j_port ** port_ptr_ptr;
  struct a2j_port * port_ptr;

  stream_ptr = &self->stream[info_ptr->dir];

  a2j_add_ports(stream_ptr);

  // process ports
  for (i = 0 ; i < PORT_HASH_SIZE ; i++)
  {
    port_ptr_ptr = &stream_ptr->port_hash[i];
    while (*port_ptr_ptr != NULL)
    {
      port_ptr = *port_ptr_ptr;

      port_ptr->jack_buf = jack_port_get_buffer(port_ptr->jack_port, info_ptr->nframes);

      if (info_ptr->dir == A2J_PORT_CAPTURE)
      {
        jack_midi_clear_buffer(port_ptr->jack_buf);
      }

      if (!port_ptr->is_dead)
      {
        if (info_ptr->dir == A2J_PORT_CAPTURE)
        {
          a2j_do_jack_input(self, port_ptr, info_ptr);
        }
        else
        {
          a2j_do_jack_output(self, port_ptr, info_ptr);
        }
      }
      else if (jack_ringbuffer_write_space(self->port_del) >= sizeof(port_ptr))
      {
        a2j_debug("jack: removed port %s", port_ptr->name);
        *port_ptr_ptr = port_ptr->next;
        jack_ringbuffer_write(self->port_del, (char*)&port_ptr, sizeof(port_ptr));
        continue;
      }

      port_ptr_ptr = &port_ptr->next;
    }
  }
}

/*
 * ============================ Input ==============================
 */
void
a2j_do_jack_input(
  struct a2j * self,
  struct a2j_port * port,
  struct a2j_process_info * info)
{
  // process port->early_events
  struct a2j_alsa_midi_event ev;
  while (jack_ringbuffer_read(port->early_events, (char*)&ev, sizeof(ev))) {
    jack_midi_data_t* buf;
    jack_nframes_t time = ev.time - info->period_start;
    if (time < 0)
      time = 0;
    else if (time >= info->nframes)
      time = info->nframes - 1;
    buf = jack_midi_event_reserve(port->jack_buf, time, ev.size);
    if (buf)
      jack_ringbuffer_read(port->early_events, (char*)buf, ev.size);
    else
      jack_ringbuffer_read_advance(port->early_events, ev.size);
    a2j_debug("input: it's time for %d bytes at %d", ev.size, time);
  }
}

static
void
a2j_port_event(
  struct a2j * self,
  snd_seq_event_t * ev)
{
  const snd_seq_addr_t addr = ev->data.addr;

  if (addr.client == self->client_id)
    return;

  if (ev->type == SND_SEQ_EVENT_PORT_START || ev->type == SND_SEQ_EVENT_PORT_CHANGE) {
    assert (jack_ringbuffer_write_space(self->port_add) >= sizeof(addr));

    a2j_debug("port_event: add/change %d:%d", addr.client, addr.port);
    jack_ringbuffer_write(self->port_add, (char*)&addr, sizeof(addr));
  } else if (ev->type == SND_SEQ_EVENT_PORT_EXIT) {
    a2j_debug("port_event: del %d:%d", addr.client, addr.port);
    a2j_port_setdead(self->stream[A2J_PORT_CAPTURE].port_hash, addr);
    a2j_port_setdead(self->stream[A2J_PORT_PLAYBACK].port_hash, addr);
  }
}

static
void
a2j_input_event(
  struct a2j * self,
  snd_seq_event_t * alsa_event,
  struct a2j_process_info * info)
{
  jack_midi_data_t data[MAX_EVENT_SIZE];
  struct a2j_stream *str = &self->stream[A2J_PORT_CAPTURE];
  long size;
  int64_t alsa_time, time_offset;
  int64_t frame_offset, event_frame;
  struct a2j_port *port;

  port = a2j_port_get(str->port_hash, alsa_event->source);
  if (!port)
    return;

  /*
   * RPNs, NRPNs, Bank Change, etc. need special handling
   * but seems, ALSA does it for us already.
   */
  snd_midi_event_reset_decode(str->codec);
  if ((size = snd_midi_event_decode(str->codec, data, sizeof(data), alsa_event))<0)
    return;

  // fixup NoteOn with vel 0
  if (data[0] == 0x90 && data[2] == 0x00) {
    data[0] = 0x80;
    data[2] = 0x40;
  }

  alsa_time = alsa_event->time.time.tv_sec * NSEC_PER_SEC + alsa_event->time.time.tv_nsec;
  time_offset = info->alsa_time - alsa_time;
  frame_offset = (info->sample_rate * time_offset) / NSEC_PER_SEC;
  event_frame = (int64_t)info->cur_frames - info->period_start - frame_offset + info->nframes;

  a2j_debug("input: %d bytes at event_frame=%d", (int)size, (int)event_frame);

  if (event_frame >= info->nframes &&
      jack_ringbuffer_write_space(port->early_events) >= (sizeof(struct a2j_alsa_midi_event) + size)) {
    struct a2j_alsa_midi_event ev;
    ev.time = event_frame + info->period_start;
    ev.size = size;
    jack_ringbuffer_write(port->early_events, (char*)&ev, sizeof(ev));
    jack_ringbuffer_write(port->early_events, (char*)data, size);
    a2j_debug("postponed to next frame +%d", (int) (event_frame - info->nframes)); 
    return;
  }

  if (event_frame < 0)
    event_frame = 0;
  else if (event_frame >= info->nframes)
    event_frame = info->nframes - 1;

  jack_midi_event_write(port->jack_buf, event_frame, data, size);
}

/*
 * ============================ Output ==============================
 */

void
a2j_do_jack_output(
  struct a2j * self,
  struct a2j_port * port,
  struct a2j_process_info * info)
{
  struct a2j_stream *str = &self->stream[info->dir];
  int nevents = jack_midi_get_event_count(port->jack_buf);
  int i;
  for (i=0; i<nevents; ++i) {
    jack_midi_event_t jack_event;
    snd_seq_event_t alsa_event;
    int64_t frame_offset;
    int64_t out_time;
    snd_seq_real_time_t out_rt;
    int err;

    jack_midi_event_get(&jack_event, port->jack_buf, i);

    snd_seq_ev_clear(&alsa_event);
    snd_midi_event_reset_encode(str->codec);
    if (!snd_midi_event_encode(str->codec, jack_event.buffer, jack_event.size, &alsa_event))
      continue; // invalid event

    snd_seq_ev_set_source(&alsa_event, self->port_id);
    snd_seq_ev_set_dest(&alsa_event, port->remote.client, port->remote.port);

    /* NOTE: in case of xrun it could become negative, so it is essential to use signed type! */
    frame_offset = (int64_t)jack_event.time + info->period_start + info->nframes - info->cur_frames;
    if (frame_offset < 0) {
      frame_offset = info->nframes + jack_event.time;
      a2j_error("internal xrun detected: frame_offset = %lld", frame_offset);
    }
    assert (frame_offset < info->nframes*2);

    out_time = info->alsa_time + (frame_offset * NSEC_PER_SEC) / info->sample_rate;

    // we should use absolute time to prevent reordering caused by rounding errors
    if (out_time < port->last_out_time) {
      a2j_debug("alsa_out: limiting out_time %lld at %lld", out_time, port->last_out_time);
      out_time = port->last_out_time;
    } else
      port->last_out_time = out_time;

    out_rt.tv_nsec = out_time % NSEC_PER_SEC;
    out_rt.tv_sec = out_time / NSEC_PER_SEC;
    snd_seq_ev_schedule_real(&alsa_event, self->queue, 0, &out_rt);

    err = snd_seq_event_output(self->seq, &alsa_event);
    a2j_debug("alsa_out: written %d bytes to %s at %d (%lld): %d %s", jack_event.size, port->name, (int)frame_offset, out_time - info->alsa_time, err, err < 0 ? snd_strerror(err) : "bytes queued");
  }
}

#define a2j_ptr ((struct a2j *)arg)

static
int
a2j_jack_process(
  jack_nframes_t nframes,
  void * arg)
{
  int res;
  snd_seq_event_t *event;
  struct a2j_process_info info;

  if (g_freewheeling)
    return 0;

  a2j_set_process_info(&info, a2j_ptr, A2J_PORT_CAPTURE, nframes);
  a2j_jack_process_internal(a2j_ptr, &info); 

  while ((res = snd_seq_event_input(a2j_ptr->seq, &event))>0) {
    if (event->source.client == SND_SEQ_CLIENT_SYSTEM)
      a2j_port_event(a2j_ptr, event);
    else
      a2j_input_event(a2j_ptr, event, &info);
  }

  a2j_set_process_info(&info, a2j_ptr, A2J_PORT_PLAYBACK, nframes);
  a2j_jack_process_internal(a2j_ptr, &info);
  snd_seq_drain_output(a2j_ptr->seq);

  return 0;
}

static
void
a2j_jack_freewheel(
  int starting,
  void * arg)
{
  g_freewheeling = starting;
}

static
void
a2j_jack_shutdown(
  void * arg)
{
  a2j_warning("JACK server shutdown notification received.");
  g_stop_request = true;
}

#undef a2j_ptr

jack_client_t *
a2j_jack_client_create(
  struct a2j * a2j_ptr,
  const char * client_name,
  const char * server_name)
{
  jack_status_t status;
  jack_client_t * jack_client;

  if (server_name != NULL)
  {
    jack_client = jack_client_open(client_name, JackServerName|JackNoStartServer|JackUseExactName, &status, server_name);
  }
  else
  {
    jack_client = jack_client_open(client_name, JackNoStartServer|JackUseExactName, &status);
  }

  if (!jack_client)
  {
    a2j_error("Cannot create jack client");
    return NULL;
  }

  jack_set_process_callback(jack_client, a2j_jack_process, a2j_ptr);
  jack_set_freewheel_callback(jack_client, a2j_jack_freewheel, NULL);
  jack_on_shutdown(jack_client, a2j_jack_shutdown, NULL);

  return jack_client;
}
