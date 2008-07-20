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

#include <getopt.h>

#include "structs.h"
#include "port.h"
#include "port_thread.h"
#include "log.h"

/*
 * =================== Input/output port handling =========================
 */
static
void
a2j_set_process_info(
  struct a2j_process_info * info,
  struct a2j * self,
  struct a2j_jack_client * client_ptr,
  int dir,
  jack_nframes_t nframes)
{
  const snd_seq_real_time_t* alsa_time;
  snd_seq_queue_status_t *status;

  snd_seq_queue_status_alloca(&status);

  info->dir = dir;

  info->period_start = jack_last_frame_time(client_ptr->client);
  info->nframes = nframes;
  info->sample_rate = jack_get_sample_rate(client_ptr->client);

  info->cur_frames = jack_frame_time(client_ptr->client);

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
    a2j_port_insert(str->ports, port);
  }
}

static
void
a2j_jack_process_internal(
  struct a2j * self,
  struct a2j_process_info * info)
{
  struct a2j_stream *str = &self->stream[info->dir];
  port_jack_func process = g_port_type[info->dir].jack_func;
  int i, del=0;

  a2j_add_ports(str);

  // process ports
  for (i=0; i<PORT_HASH_SIZE; ++i) {
    struct a2j_port **pport = &str->ports[i];
    while (*pport) {
      struct a2j_port *port = *pport;
      port->jack_buf = jack_port_get_buffer(port->jack_port, info->nframes);
      if (info->dir == PORT_INPUT)
        jack_midi_clear_buffer(port->jack_buf);

      if (!port->is_dead)
        (*process)(self, port, info);
      else if (jack_ringbuffer_write_space(self->port_del) >= sizeof(port)) {
        a2j_debug("jack: removed port %s", port->name);
        *pport = port->next;
        jack_ringbuffer_write(self->port_del, (char*)&port, sizeof(port));
        del++;
        continue;
      }

      pport = &port->next;
    }
  }

  if (del)
    sem_post(&self->port_sem);
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
    sem_post(&self->port_sem);
  } else if (ev->type == SND_SEQ_EVENT_PORT_EXIT) {
    a2j_debug("port_event: del %d:%d", addr.client, addr.port);
    a2j_port_setdead(self->stream[PORT_INPUT].ports, addr);
    a2j_port_setdead(self->stream[PORT_OUTPUT].ports, addr);
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

  port = a2j_port_get(str->ports, alsa_event->source);
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

static
void
a2j_read(
  struct a2j * self,
  struct a2j_jack_client * client_ptr,
  jack_nframes_t nframes)
{
  int res;
  snd_seq_event_t *event;
  struct a2j_process_info info;

  if (!self->keep_walking)
    return;

  a2j_set_process_info(&info, self, client_ptr, PORT_INPUT, nframes);
  a2j_jack_process_internal(self, &info); 

  while ((res = snd_seq_event_input(self->seq, &event))>0) {
    if (event->source.client == SND_SEQ_CLIENT_SYSTEM)
      a2j_port_event(self, event);
    else
      a2j_input_event(self, event, &info);
  }
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

static
void
a2j_write(
  struct a2j * self,
  struct a2j_jack_client * client_ptr,
  jack_nframes_t nframes)
{
  struct a2j_process_info info;

  if (!self->keep_walking)
    return;

  a2j_set_process_info(&info, self, client_ptr, PORT_OUTPUT, nframes);
  a2j_jack_process_internal(self, &info);
  snd_seq_drain_output(self->seq);
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

static
int
a2j_start(
  struct a2j * self)
{
  int err;

  a2j_debug("midi: start");

  if (!self->seq)
    return -EBADF;

  if (self->keep_walking)
    return -EALREADY;

  snd_seq_connect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  snd_seq_drop_input(self->seq);

  a2j_add_existing_ports(self);
  a2j_update_ports(self);
  a2j_add_ports(&self->stream[PORT_INPUT]);
  a2j_add_ports(&self->stream[PORT_OUTPUT]);

  self->keep_walking = true;

  if ((err = pthread_create(&self->port_thread, NULL, a2j_port_thread, self))) {
    self->keep_walking = false;
    return -errno;
  }

  return 0;
}

static
int
a2j_stop(
  struct a2j * self)
{
  a2j_debug("midi: stop");

  if (!self->keep_walking)
    return -EALREADY;

  snd_seq_disconnect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

  self->keep_walking = false;

  sem_post(&self->port_sem);
  pthread_join(self->port_thread, NULL);
  self->port_thread = 0;

  return 0;
}

////////////////////////////////

static
int
a2j_jack_process(
  jack_nframes_t nframes,
  void * arg)
{
  struct a2j_jack_client * client_ptr = arg;

  a2j_read(client_ptr->a2j_ptr, client_ptr, nframes);
  a2j_write(client_ptr->a2j_ptr, client_ptr, nframes);

  return 0;
}

static
void
a2j_jack_freewheel(
  int starting,
  void * arg)
{
  struct a2j *midi = arg;
  if (starting)
    a2j_stop(midi);
  else
    a2j_start(midi);
}

static
int keep_walking = true;

static
void
a2j_sigint_handler(
  int i)
{
  keep_walking = false;
}

static
void
a2j_jack_shutdown(
  void * arg)
{
  keep_walking = false;
}

bool
a2j_jack_client_init(
  struct a2j * self,
  struct a2j_jack_client * client_ptr,
  const char * name)
{
  int error;
  jack_status_t status;

  client_ptr->a2j_ptr = self;
  strcpy(client_ptr->name, name);

  if (self->jack_server_name)
  {
    client_ptr->client = jack_client_open(name, JackServerName|JackNoStartServer, &status, self->jack_server_name);
  }
  else
  {
    client_ptr->client = jack_client_open(name, JackNoStartServer, &status);
  }

  if (!client_ptr->client)
  {
    a2j_error("Cannot create jack client");
    goto fail;
  }

  jack_set_process_callback(client_ptr->client, a2j_jack_process, client_ptr);
  jack_set_freewheel_callback(client_ptr->client, a2j_jack_freewheel, self);
  jack_on_shutdown(client_ptr->client, a2j_jack_shutdown, NULL);

  if (jack_activate(client_ptr->client))
  {
    a2j_error("can't activate jack client");
    goto fail_close;
  }

  return true;

fail_close:
  error = jack_client_close(client_ptr->client);
  if (error != 0)
  {
    a2j_error("Cannot close jack client");
  }

fail:
  return false;
}

void
a2j_jack_client_uninit(
  struct a2j_jack_client * client_ptr)
{
  jack_deactivate(client_ptr->client);
  jack_client_close(client_ptr->client);
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
  struct a2j * self,
  int dir)
{
}

static
void
a2j_stream_detach(
  struct a2j * self,
  int dir)
{
  struct a2j_stream *str = &self->stream[dir];
  int i;

  a2j_free_ports(self, str->new_ports);

  // delete all ports from hash
  for (i=0; i<PORT_HASH_SIZE; ++i) {
    struct a2j_port *port = str->ports[i];
    while (port) {
      struct a2j_port *next = port->next;
      a2j_info("port deleted: %s", port->name);
      a2j_port_free(self, port);
      port = next;
    }
    str->ports[i] = NULL;
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
  const char * jack_server_name)
{
  struct a2j *self = calloc(1, sizeof(struct a2j));
  a2j_debug("midi: new");
  if (!self)
    return NULL;

  self->jack_server_name = jack_server_name;

  if (!a2j_jack_client_init(self, self->jack_clients + 0, "a2j"))
  {
    free(self);
    return NULL;
  }

  self->port_add = jack_ringbuffer_create(2*MAX_PORTS*sizeof(snd_seq_addr_t));
  self->port_del = jack_ringbuffer_create(2*MAX_PORTS*sizeof(struct a2j_port*));
  sem_init(&self->port_sem, 0, 0);

  a2j_stream_init(self, PORT_INPUT);
  a2j_stream_init(self, PORT_OUTPUT);

  return self;
}

static
int
a2j_attach(
  struct a2j * self)
{
  int err;

  a2j_debug("midi: attach");

  if (self->seq)
    return -EALREADY;

  if ((err = snd_seq_open(&self->seq, "hw", SND_SEQ_OPEN_DUPLEX, 0)) < 0) {
    a2j_error("failed to open alsa seq");
    return err;
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

  a2j_stream_attach(self, PORT_INPUT);
  a2j_stream_attach(self, PORT_OUTPUT);

  snd_seq_nonblock(self->seq, 1);

  return 0;
}

static
int
a2j_detach(
  struct a2j * self)
{
  a2j_debug("midi: detach");

  if (!self->seq)
    return -EALREADY;

  a2j_stop(self);

  jack_ringbuffer_reset(self->port_add);
  a2j_free_ports(self, self->port_del);

  a2j_stream_detach(self, PORT_INPUT);
  a2j_stream_detach(self, PORT_OUTPUT);

  snd_seq_close(self->seq);
  self->seq = NULL;

  return 0;
}

static
void
a2j_destroy(
  struct a2j * self)
{
  a2j_debug("midi: delete");
  a2j_detach(self);

  a2j_stream_close(self, PORT_OUTPUT);
  a2j_stream_close(self, PORT_INPUT);

  jack_ringbuffer_free(self->port_add);
  jack_ringbuffer_free(self->port_del);
  sem_close(&self->port_sem);

  a2j_jack_client_uninit(self->jack_clients + 0);

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

static
void
a2j_help(
  const char * self)
{
  printf("Usage: %s [-j jack-server] [-e | --export-hw]\n", self);
  printf("Defaults:\n");
  printf("-j default\n\n");
}

int
main(
  int argc,
  char *argv[])
{
  struct a2j *midi;
  const char* jack_server = NULL;
  bool export_hw_ports = false;

  struct option long_opts[] = { { "export-hw", 0, 0, 'e' }, { 0, 0, 0, 0 } };

  a2j_port_type_init();

  printf("JACK MIDI <-> ALSA sequencer MIDI bridge\n");
  printf("Copyright 2006,2007 Dmitry S. Baikov\n");
  printf("Copyright 2007,2008 Nedko Arnaudov\n\n");
  int option_index = 0;
  while (1) {
    int c = getopt_long(argc, argv, "j:eq", long_opts, &option_index);
    if (c == -1)
      break;
    switch (c) {
    case 'j': jack_server = optarg; break;
    case 'e': export_hw_ports = true; break;
    default:
      a2j_help(argv[0]);
      return 1;
    }
  }

  midi = a2j_new(jack_server);

  if (!midi)
    goto fail1;

  midi->export_hw_ports = export_hw_ports;

  signal(SIGINT, &a2j_sigint_handler);
  signal(SIGTERM, &a2j_sigint_handler);

  a2j_attach(midi);
  a2j_start(midi);

  printf("Started.\n");

  while (keep_walking)
    sleep(1);

  a2j_stop(midi);
  a2j_detach(midi);
  a2j_destroy(midi);

fail1:

  return 0;
}
