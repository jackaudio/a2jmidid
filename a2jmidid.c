/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
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
#include <sys/stat.h>
#include <pthread.h>

#include <alsa/asoundlib.h>

#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include <dbus/dbus.h>

#include <getopt.h>

#include "config.h"
#include "list.h"
#include "structs.h"
#include "port.h"
#include "port_thread.h"
#include "port_hash.h"
#include "log.h"
#include "dbus.h"
#include "a2jmidid.h"
#include "paths.h"
#include "conf.h"
#include "jack.h"
#include "sigsegv.h"
#include "gitversion.h"
#include "dbus_iface_control.h"

#define MAIN_LOOP_SLEEP_INTERVAL 50 // in milliseconds

bool g_keep_walking = true;
bool g_stop_request = false;
static bool g_started = false;
struct a2j * g_a2j = NULL;

bool g_a2j_export_hw_ports = false;
char * g_a2j_jack_server_name = "default";

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

static
void
a2j_sigint_handler(
  int i)
{
  g_keep_walking = false;
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
  INIT_LIST_HEAD(&str->list);
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
  struct list_head * node_ptr;

  while (!list_empty(&stream_ptr->list))
  {
    node_ptr = stream_ptr->list.next;
    list_del(node_ptr);
    port_ptr = list_entry(node_ptr, struct a2j_port, siblings);
    a2j_info("port deleted: %s", port_ptr->name);
    a2j_port_free(port_ptr);
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
a2j_new()
{
  int error;
  extern void* a2j_alsa_input_thread (void*);
  extern void* a2j_alsa_output_thread (void*);

  struct a2j *self = calloc(1, sizeof(struct a2j));
  a2j_debug("midi: new");
  if (!self)
    return NULL;

  self->port_add = jack_ringbuffer_create(2*MAX_PORTS*sizeof(snd_seq_addr_t));
  self->port_del = jack_ringbuffer_create(2*MAX_PORTS*sizeof(struct a2j_port*));

  self->outbound_events = jack_ringbuffer_create(MAX_EVENT_SIZE*16*sizeof(struct a2j_delivery_event));

  a2j_stream_init(self, A2J_PORT_CAPTURE);
  a2j_stream_init(self, A2J_PORT_PLAYBACK);

  error = snd_seq_open(&self->seq, "hw", SND_SEQ_OPEN_DUPLEX, 0);
  if (error < 0)
  {
    a2j_error("failed to open alsa seq");
    free(self);
    return NULL;
  }

  snd_seq_set_client_name(self->seq, "a2jmidid");
  self->port_id = snd_seq_create_simple_port(
    self->seq,
    "port",
    SND_SEQ_PORT_CAP_READ|SND_SEQ_PORT_CAP_WRITE
#ifndef DEBUG
    |SND_SEQ_PORT_CAP_NO_EXPORT
#endif
    ,SND_SEQ_PORT_TYPE_APPLICATION);
  self->client_id = snd_seq_client_id(self->seq);

  self->queue = snd_seq_alloc_queue(self->seq);
  snd_seq_start_queue(self->seq, self->queue, 0); 

  a2j_stream_attach(self->stream + A2J_PORT_CAPTURE);
  a2j_stream_attach(self->stream + A2J_PORT_PLAYBACK);

  snd_seq_nonblock(self->seq, 1);

  snd_seq_connect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  snd_seq_drop_input(self->seq);

  a2j_add_ports(&self->stream[A2J_PORT_CAPTURE]);
  a2j_add_ports(&self->stream[A2J_PORT_PLAYBACK]);

  self->jack_client = a2j_jack_client_create(self, A2J_JACK_CLIENT_NAME, g_a2j_jack_server_name);
  if (self->jack_client == NULL)
  {
    free(self);
    return NULL;
  }

  if (sem_init (&self->io_semaphore, 0, 0) < 0) {
    a2j_error ("can't create IO semaphore");
    free (self);
    return NULL;
  }

  if (jack_activate(self->jack_client))
  {
    a2j_error("can't activate jack client");

    error = jack_client_close(self->jack_client);
    if (error != 0)
    {
      a2j_error("Cannot close jack client");
    }

    free(self);
    return NULL;
  }

  a2j_add_existing_ports(self);

  if (pthread_create (&self->alsa_input_thread, NULL, a2j_alsa_input_thread, self) < 0) {
    fprintf (stderr, "cannot start ALSA input thread\n");
    free (self);
    return NULL;
  }

  if (pthread_create (&self->alsa_output_thread, NULL, a2j_alsa_output_thread, self) < 0) {
    fprintf (stderr, "cannot start ALSA output thread\n");
    free (self);
    return NULL;
  }

  return self;
}

static
void
a2j_destroy(
  struct a2j * self)
{
  int error;
  void* alsa_status;

  a2j_debug("midi: delete");

  g_keep_walking = false;

  /* do something that we need to do anyway and will wake the input thread, then join */

  snd_seq_disconnect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  pthread_join (self->alsa_input_thread, &alsa_status);

  /* wake output thread and join, then destroy the semaphore */

  sem_post (&self->io_semaphore);
  pthread_join (self->alsa_output_thread, &alsa_status);
  sem_destroy (&self->io_semaphore);

  jack_ringbuffer_reset(self->port_add);

  jack_deactivate(self->jack_client);

  a2j_stream_detach(self->stream + A2J_PORT_CAPTURE);
  a2j_stream_detach(self->stream + A2J_PORT_PLAYBACK);

  error = jack_client_close(self->jack_client);
  if (error != 0)
  {
    a2j_error("Cannot close jack client (%d)", error);
  }

  snd_seq_close(self->seq);
  self->seq = NULL;

  a2j_stream_close(self, A2J_PORT_PLAYBACK);
  a2j_stream_close(self, A2J_PORT_CAPTURE);

  jack_ringbuffer_free(self->port_add);
  jack_ringbuffer_free(self->port_del);

  free(self);
}

bool
a2j_start()
{
  if (g_started)
  {
    a2j_error("Bridge already started");
    return false;
  }

  a2j_info("Bridge starting...");

  a2j_info("Using JACK server '%s'", g_a2j_jack_server_name);

  a2j_info("Hardware ports %s be exported.", g_a2j_export_hw_ports ? "will": "will not");

  g_a2j = a2j_new();
  if (g_a2j == NULL)
  {
    a2j_error("a2j_new() failed.");
    return false;
  }

  a2j_info("Bridge started");

  if (a2j_dbus_is_available())
  {
    a2j_dbus_signal_emit_bridge_started();
  }

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
  g_a2j = NULL;

  a2j_info("Bridge stopped");

  g_started = false;

  if (a2j_dbus_is_available())
  {
    a2j_dbus_signal_emit_bridge_stopped();
  }

  return true;
}

bool
a2j_is_started()
{
  return g_started;
}

static
void
a2j_help(
  const char * self)
{
  a2j_info("Usage: %s [-j jack-server] [-e | --export-hw]", self);
  a2j_info("Defaults:");
  a2j_info("-j default");
}

int
main(
  int argc,
  char *argv[])
{
  bool dbus;
  struct stat st;
  char timestamp_str[26];

  //test_list_sort();

  st.st_mtime = 0;
  stat(argv[0], &st);
  ctime_r(&st.st_mtime, timestamp_str);
  timestamp_str[24] = 0;

  if (!a2j_paths_init())
  {
    goto fail;
  }

  dbus = argc == 2 && strcmp(argv[1], "dbus") == 0;

  if (!a2j_log_init(dbus))
  {
    goto fail_paths_uninit;
  }

  if (!dbus)
  {
    struct option long_opts[] = { { "export-hw", 0, 0, 'e' }, { 0, 0, 0, 0 } };

    int option_index = 0;
    int c;
    while ((c = getopt_long(argc, argv, "j:eq", long_opts, &option_index)) != -1)
    {
      switch (c)
      {
      case 'j':
        g_a2j_jack_server_name = strdup(optarg);
        break;
      case 'e':
        g_a2j_export_hw_ports = true;
        break;
      default:
        a2j_help(argv[0]);
        return 1;        
      }
    }
  }
  else
  {
    //a2j_conf_load();
  }

  if (dbus)
  {
    a2j_info("----------------------------");
  }

  a2j_info("JACK MIDI <-> ALSA sequencer MIDI bridge, version " A2J_VERSION " (" GIT_VERSION ") built on %s", timestamp_str);
  a2j_info("Copyright 2006,2007 Dmitry S. Baikov");
  a2j_info("Copyright 2007,2008,2009 Nedko Arnaudov");

  if (dbus)
  {
    a2j_info("----------------------------");
    a2j_info("Activated.");
  }
  else
  {
    a2j_info("");
  }

  /* setup our SIGSEGV magic that prints nice stack in our logfile */ 
  setup_sigsegv();

  signal(SIGINT, &a2j_sigint_handler);
  signal(SIGTERM, &a2j_sigint_handler);

  if (dbus)
  {
    if (!a2j_dbus_init())
    {
      a2j_error("a2j_dbus_init() failed.");
      goto fail_uninit_log;
    }
  }
  else
  {
    if (!a2j_start())
    {
      goto fail_uninit_log;
    }

    a2j_info("Press ctrl-c to stop the bridge");
  }

  while (g_keep_walking)
  {
    if (dbus)
    {
      if (!a2j_dbus_run(MAIN_LOOP_SLEEP_INTERVAL))
      {
        a2j_warning("Disconnect message was received from D-Bus.");
        break;
      }
    }
    else
    {
      usleep(MAIN_LOOP_SLEEP_INTERVAL * 1000);
    }

    if (g_stop_request)
    {
      g_stop_request = false;

      a2j_stop();

      if (!dbus)
      {
        break;
      }
    }

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

  if (dbus)
  {
    a2j_dbus_uninit();

    a2j_info("Deactivated.");
    a2j_info("----------------------------");
  }

fail_uninit_log:
  a2j_log_uninit();

fail_paths_uninit:
  a2j_paths_uninit();

fail:
  return 0;
}
