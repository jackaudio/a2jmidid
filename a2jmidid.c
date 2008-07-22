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

#include <signal.h>
#include <semaphore.h>
#include <stdbool.h>

#include <alsa/asoundlib.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include <dbus/dbus.h>

#include "structs.h"
#include "port.h"
#include "port_thread.h"
#include "port_hash.h"
#include "log.h"
#include "dbus.h"

#define MAIN_LOOP_SLEEP_INTERVAL 50 // in milliseconds

struct a2j_port_type g_port_type[2];
bool g_keep_walking = true;
static bool g_started = false;
static bool g_freewheeling = false;
struct a2j * g_a2j = NULL;

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
}

static
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
  port_jack_func process_func;
  int i;
  struct a2j_port ** port_ptr_ptr;
  struct a2j_port * port_ptr;

  stream_ptr = &self->stream[info_ptr->dir];
  process_func = g_port_type[info_ptr->dir].jack_func;

  a2j_add_ports(stream_ptr);

  // process ports
  for (i = 0 ; i < PORT_HASH_SIZE ; i++)
  {
    port_ptr_ptr = &stream_ptr->port_hash[i];
    while (*port_ptr_ptr != NULL)
    {
      port_ptr = *port_ptr_ptr;

      port_ptr->jack_buf = jack_port_get_buffer(port_ptr->jack_port, info_ptr->nframes);

      if (info_ptr->dir == PORT_INPUT)
      {
        jack_midi_clear_buffer(port_ptr->jack_buf);
      }

      if (!port_ptr->is_dead)
      {
        (*process_func)(self, port_ptr, info_ptr);
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
static
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
    a2j_port_setdead(self->stream[PORT_INPUT].port_hash, addr);
    a2j_port_setdead(self->stream[PORT_OUTPUT].port_hash, addr);
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
  struct a2j_stream *str = &self->stream[PORT_INPUT];
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

static
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
    jack_nframes_t frame_offset;
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

    frame_offset = jack_event.time + info->period_start + info->nframes - info->cur_frames;
    out_time = info->alsa_time + (frame_offset * NSEC_PER_SEC) / info->sample_rate;

    // we should use absolute time to prevent reordering caused by rounding errors
    if (out_time < port->last_out_time)
      out_time = port->last_out_time;
    else
      port->last_out_time = out_time;

    out_rt.tv_nsec = out_time % NSEC_PER_SEC;
    out_rt.tv_sec = out_time / NSEC_PER_SEC;
    snd_seq_ev_schedule_real(&alsa_event, self->queue, 0, &out_rt);

    err = snd_seq_event_output(self->seq, &alsa_event);
    a2j_debug("alsa_out: written %d bytes to %s at +%d: %d", jack_event.size, port->name, (int)frame_offset, err);
  }
}

/////////////////////////////

static
void
a2j_add_existing_ports(
  struct a2j * self)
{
  snd_seq_addr_t addr;
  snd_seq_client_info_t *client_info;
  snd_seq_port_info_t *port_info;

  snd_seq_client_info_alloca(&client_info);
  snd_seq_port_info_alloca(&port_info);
  snd_seq_client_info_set_client(client_info, -1);
  while (snd_seq_query_next_client(self->seq, client_info) >= 0)
  {
    addr.client = snd_seq_client_info_get_client(client_info);
    if (addr.client == SND_SEQ_CLIENT_SYSTEM || addr.client == self->client_id)
      continue;
    snd_seq_port_info_set_client(port_info, addr.client);
    snd_seq_port_info_set_port(port_info, -1);
    while (snd_seq_query_next_port(self->seq, port_info) >= 0)
    {
      addr.port = snd_seq_port_info_get_port(port_info);
      a2j_update_port(self, addr, port_info);
    }
  }
}

////////////////////////////////

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

  a2j_set_process_info(&info, a2j_ptr, PORT_INPUT, nframes);
  a2j_jack_process_internal(a2j_ptr, &info); 

  while ((res = snd_seq_event_input(a2j_ptr->seq, &event))>0) {
    if (event->source.client == SND_SEQ_CLIENT_SYSTEM)
      a2j_port_event(a2j_ptr, event);
    else
      a2j_input_event(a2j_ptr, event, &info);
  }

  a2j_set_process_info(&info, a2j_ptr, PORT_OUTPUT, nframes);
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
  g_keep_walking = false;
}

#undef a2j_ptr

static
void
a2j_sigint_handler(
  int i)
{
  g_keep_walking = false;
}

bool
a2j_jack_client_init(
  struct a2j * a2j_ptr,
  const char * client_name,
  const char * server_name)
{
  int error;
  jack_status_t status;
  jack_client_t * jack_client;

  if (server_name != NULL)
  {
    jack_client = jack_client_open(client_name, JackServerName|JackNoStartServer, &status, server_name);
  }
  else
  {
    jack_client = jack_client_open(client_name, JackNoStartServer, &status);
  }

  if (!jack_client)
  {
    a2j_error("Cannot create jack client");
    goto fail;
  }

  jack_set_process_callback(jack_client, a2j_jack_process, a2j_ptr);
  jack_set_freewheel_callback(jack_client, a2j_jack_freewheel, NULL);
  jack_on_shutdown(jack_client, a2j_jack_shutdown, NULL);


  a2j_ptr->jack_client = jack_client;

  if (jack_activate(jack_client))
  {
    a2j_error("can't activate jack client");
    a2j_ptr->jack_client = NULL;
    goto fail_close;
  }

  return true;

fail_close:
  error = jack_client_close(jack_client);
  if (error != 0)
  {
    a2j_error("Cannot close jack client");
  }

fail:
  return false;
}

void
a2j_jack_client_uninit(
  jack_client_t * jack_client)
{
  jack_deactivate(jack_client);
  jack_client_close(jack_client);
}

static
void
a2j_stream_init(
  struct a2j * self,
  int dir)
{
  struct a2j_stream *str = &self->stream[dir];

  str->new_ports = jack_ringbuffer_create(MAX_PORTS*sizeof(struct a2j_port*));
  snd_midi_event_new(MAX_EVENT_SIZE, &str->codec);
}

static
void
a2j_stream_attach(
  struct a2j_stream * stream_ptr)
{
}

static
void
a2j_stream_detach(
  struct a2j_stream * stream_ptr)
{
  struct a2j_port * port_ptr;
  struct a2j_port * next_port_ptr;
  int i;

  a2j_free_ports(stream_ptr->new_ports);

  // delete all ports from hash
  for (i = 0 ; i < PORT_HASH_SIZE ; i++)
  {
    port_ptr = stream_ptr->port_hash[i];
    stream_ptr->port_hash[i] = NULL;

    while (port_ptr != NULL)
    {
      next_port_ptr = port_ptr->next;
      a2j_info("port deleted: %s", port_ptr->name);
      a2j_port_free(port_ptr);
      port_ptr = next_port_ptr;
    }
  }
}

static
void
a2j_stream_close(
  struct a2j * self,
  int dir)
{
  struct a2j_stream *str = &self->stream[dir];

  if (str->codec)
    snd_midi_event_free(str->codec);
  if (str->new_ports)
    jack_ringbuffer_free(str->new_ports);
}

struct a2j *
a2j_new(
  const char * jack_server_name,
  bool export_hw_ports)
{
  int err;

  struct a2j *self = calloc(1, sizeof(struct a2j));
  a2j_debug("midi: new");
  if (!self)
    return NULL;

  self->export_hw_ports = export_hw_ports;

  self->port_add = jack_ringbuffer_create(2*MAX_PORTS*sizeof(snd_seq_addr_t));
  self->port_del = jack_ringbuffer_create(2*MAX_PORTS*sizeof(struct a2j_port*));

  a2j_stream_init(self, PORT_INPUT);
  a2j_stream_init(self, PORT_OUTPUT);

  if ((err = snd_seq_open(&self->seq, "hw", SND_SEQ_OPEN_DUPLEX, 0)) < 0) {
    a2j_error("failed to open alsa seq");
    free(self);
    return NULL;
  }

  snd_seq_set_client_name(self->seq, "a2jmidid");
  self->port_id = snd_seq_create_simple_port(self->seq, "port",
    SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_WRITE
#ifndef DEBUG
    |SND_SEQ_PORT_CAP_NO_EXPORT
#endif
    ,SND_SEQ_PORT_TYPE_APPLICATION);
  self->client_id = snd_seq_client_id(self->seq);

    self->queue = snd_seq_alloc_queue(self->seq);
    snd_seq_start_queue(self->seq, self->queue, 0); 

  a2j_stream_attach(self->stream + PORT_INPUT);
  a2j_stream_attach(self->stream + PORT_OUTPUT);

  snd_seq_nonblock(self->seq, 1);

  snd_seq_connect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  snd_seq_drop_input(self->seq);

  a2j_add_ports(&self->stream[PORT_INPUT]);
  a2j_add_ports(&self->stream[PORT_OUTPUT]);

  if (!a2j_jack_client_init(self, "a2j", jack_server_name))
  {
    free(self);
    return NULL;
  }

  a2j_add_existing_ports(self);
  a2j_update_ports(self);

  return self;
}

static
void
a2j_destroy(
  struct a2j * self)
{
  a2j_debug("midi: delete");

  snd_seq_disconnect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

  jack_ringbuffer_reset(self->port_add);
  a2j_free_ports(self->port_del);

  a2j_stream_detach(self->stream + PORT_INPUT);
  a2j_stream_detach(self->stream + PORT_OUTPUT);

  a2j_jack_client_uninit(self->jack_client);

  snd_seq_close(self->seq);
  self->seq = NULL;

  a2j_stream_close(self, PORT_OUTPUT);
  a2j_stream_close(self, PORT_INPUT);

  jack_ringbuffer_free(self->port_add);
  jack_ringbuffer_free(self->port_del);

  free(self);
}

void
a2j_port_type_init()
{
  g_port_type[PORT_INPUT].alsa_mask = SND_SEQ_PORT_CAP_SUBS_READ;
  g_port_type[PORT_INPUT].jack_caps = JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal;
  g_port_type[PORT_INPUT].jack_func = a2j_do_jack_input;

  g_port_type[PORT_OUTPUT].alsa_mask = SND_SEQ_PORT_CAP_SUBS_WRITE;
  g_port_type[PORT_OUTPUT].jack_caps = JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal;
  g_port_type[PORT_OUTPUT].jack_func = a2j_do_jack_output;
}

bool
a2j_start()
{
  /* TODO: Make these configurable through D-Bus */
  const char* jack_server = NULL;
  bool export_hw_ports = false;

  if (g_started)
  {
    a2j_error("Bridge already started");
    return false;
  }

  a2j_info("Bridge starting...");

  g_a2j = a2j_new(jack_server, export_hw_ports);
  if (g_a2j == NULL)
  {
    a2j_error("a2j_new() failed.");
    return false;
  }

  a2j_info("Bridge started");

  g_started = true;

  return true;
}

bool
a2j_stop()
{
  if (!g_started)
  {
    a2j_error("Bridge already stopped");
    return false;
  }

  a2j_info("Bridge stopping...");

  a2j_destroy(g_a2j);

  a2j_info("Bridge stopped");

  g_started = false;

  return true;
}

bool
a2j_is_started()
{
  return g_started;
}

int
main(
  int argc,
  char *argv[])
{
  a2j_port_type_init();

  a2j_info("----------------------------");
  a2j_info("JACK MIDI <-> ALSA sequencer MIDI bridge");
  a2j_info("Copyright 2006,2007 Dmitry S. Baikov");
  a2j_info("Copyright 2007,2008 Nedko Arnaudov");
  a2j_info("----------------------------");
  a2j_info("Activated.");

  signal(SIGINT, &a2j_sigint_handler);
  signal(SIGTERM, &a2j_sigint_handler);

  if (!a2j_dbus_init())
  {
    a2j_error("a2j_dbus_init() failed.");
		goto fail;
	}

  while (g_keep_walking)
  {
    a2j_dbus_run(MAIN_LOOP_SLEEP_INTERVAL);

    if (g_started)
    {
      a2j_free_ports(g_a2j->port_del);
      a2j_update_ports(g_a2j);
    }
  }

  if (g_started)
  {
    a2j_stop();
  }

  a2j_dbus_uninit();

fail:

  a2j_info("Deactivated.");
  a2j_info("----------------------------");

  return 0;
}
