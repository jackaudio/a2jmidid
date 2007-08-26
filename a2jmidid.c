/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2006,2007 Dmitry S. Baikov <c0ff@konstruktiv.org>
 * Copyright (c) 2007 Nedko Arnaudov <nedko@arnaudov.name>
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

/*
 * a2j_read:
 *  add new ports
 *  reads queued snd_seq_event's
 *  if PORT_EXIT: mark port as dead
 *  if PORT_ADD, PORT_CHANGE: send addr to port_thread (it also may mark port as dead)
 *  else process input event
 *  remove dead ports and send them to port_thread
 *
 * a2j_write:
 *  remove dead ports and send them to port_thread
 *  add new ports
 *  queue output events
 *
 * port_thread:
 *  wait for port_sem
 *  free deleted ports
 *  create new ports or mark existing as dead
 */

/*
 * ringbuffers:
 *  * early_events ( alsa_midi_event_t + data)
 *  * new_ports
 *  * port_add (snd_seq_addr_t)
 *  * port_del (port_t *)
 *
 */

#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>
#include <jack/thread.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <semaphore.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>
#include <jack/jack.h>
#include <jack/thread.h>

#define JACK_INVALID_PORT NULL

#define MAX_PORTS  64
#define MAX_EVENT_SIZE 1024
#define MAX_CLIENTS 64

typedef struct port_t port_t;

struct port_t {
  port_t *next;
  int is_dead;
  char name[64];
  snd_seq_addr_t remote;
  jack_port_t * jack_port;

  jack_ringbuffer_t *early_events; // alsa_midi_event_t + data
  int64_t last_out_time;

  void *jack_buf;
};

typedef struct {
  snd_midi_event_t *codec;

  jack_ringbuffer_t *new_ports;

  port_t *ports[MAX_PORTS];
} stream_t;

struct a2j_jack_client
{
  char name[64];
  jack_client_t * client;
  struct a2j * a2j_ptr;
};

struct a2j
{
  const char * jack_server_name;
  struct a2j_jack_client jack_clients[MAX_CLIENTS];

  snd_seq_t *seq;
  int client_id;
  int port_id;
  int queue;

  int keep_walking;

  pthread_t port_thread;
  sem_t port_sem;
  jack_ringbuffer_t *port_add; // snd_seq_addr_t
  jack_ringbuffer_t *port_del; // port_t*

  stream_t stream[2];
};

static struct a2j * a2j_new(const char * jack_server_name);
static void a2j_destroy(struct a2j *self);
static int a2j_attach(struct a2j *self);
static int a2j_detach(struct a2j *self);
static int a2j_start(struct a2j *self);
static int a2j_stop(struct a2j *self);
static void a2j_read(struct a2j * self, struct a2j_jack_client * client_ptr, jack_nframes_t nframes);
static void a2j_write(struct a2j * self, struct a2j_jack_client * client_ptr, jack_nframes_t nframes);

#define MESSAGE(...) fprintf(stderr, __VA_ARGS__)

#define info_log(...)  MESSAGE(__VA_ARGS__)
#define error_log(...) MESSAGE(__VA_ARGS__)

#ifdef DEBUG
#define debug_log(...) MESSAGE(__VA_ARGS__)
#else
#define debug_log(...)
#endif

#define NSEC_PER_SEC ((int64_t)1000*1000*1000)

static
int jack_process(jack_nframes_t nframes, void *arg)
{
  struct a2j_jack_client * client_ptr = arg;

  a2j_read(client_ptr->a2j_ptr, client_ptr, nframes);
  a2j_write(client_ptr->a2j_ptr, client_ptr, nframes);

  return 0;
}

static
void jack_freewheel(int starting, void *arg)
{
  struct a2j *midi = arg;
  if (starting)
    a2j_stop(midi);
  else
    a2j_start(midi);
}

static
int keep_walking = 1;

static
void sigint_handler(int i)
{
  keep_walking = 0;
}

static
void jack_shutdown(void *arg)
{
  keep_walking = 0;
}

static void help(const char* self)
{
  printf("Usage: %s [-j jack-server]\n", self);
  printf("Defaults:\n");
  printf("-j default\n\n");
}

int main(int argc, char *argv[])
{
  struct a2j *midi;
  const char* jack_server = NULL;

  printf("JACK MIDI <-> ALSA sequencer MIDI bridge\n");
  printf("Copyright 2006,2007 Dmitry S. Baikov\n");
  printf("Copyright 2007 Nedko Arnaudov\n\n");
  while (1) {
    int c = getopt(argc, argv, "j:");
    if (c == -1)
      break;
    switch (c) {
    case 'j': jack_server = optarg; break;
    default:
      help(argv[0]);
      return 1;
    }
  }

  midi = a2j_new(jack_server);

  if (!midi)
    goto fail1;

  signal(SIGINT, &sigint_handler);
  signal(SIGTERM, &sigint_handler);

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

enum {
  PORT_HASH_BITS = 4,
  PORT_HASH_SIZE = 1 << PORT_HASH_BITS
};

typedef port_t* port_hash_t[PORT_HASH_SIZE];

struct alsa_midi_event {
  int64_t time;
  int size;
};
typedef struct alsa_midi_event alsa_midi_event_t;

struct process_info {
  int dir;
  jack_nframes_t nframes;
  jack_nframes_t period_start;
  jack_nframes_t sample_rate;
  jack_nframes_t cur_frames;
  int64_t alsa_time;
};


enum PortType { PORT_INPUT = 0, PORT_OUTPUT = 1 };

typedef void (*port_jack_func)(struct a2j *self, port_t *port,struct process_info* info);
static void do_jack_input(struct a2j *self, port_t *port, struct process_info* info);
static void do_jack_output(struct a2j *self, port_t *port, struct process_info* info);

typedef struct {
  int alsa_mask;
  int jack_caps;
  char name[4];
  port_jack_func jack_func;
} port_type_t;

static port_type_t port_type[2] = {
  {
    SND_SEQ_PORT_CAP_SUBS_READ,
    JackPortIsOutput|JackPortIsPhysical|JackPortIsTerminal,
    "in",
    do_jack_input
  },
  {
    SND_SEQ_PORT_CAP_SUBS_WRITE,
    JackPortIsInput|JackPortIsPhysical|JackPortIsTerminal,
    "out",
    do_jack_output
  }
};


static
void stream_init(struct a2j *self, int dir)
{
  stream_t *str = &self->stream[dir];

  str->new_ports = jack_ringbuffer_create(MAX_PORTS*sizeof(port_t*));
  snd_midi_event_new(MAX_EVENT_SIZE, &str->codec);
}

static void port_free(struct a2j *self, port_t *port);
static void free_ports(struct a2j *self, jack_ringbuffer_t *ports);

static
void stream_attach(struct a2j *self, int dir)
{
}

static
void stream_detach(struct a2j *self, int dir)
{
  stream_t *str = &self->stream[dir];
  int i;

  free_ports(self, str->new_ports);

  // delete all ports from hash
  for (i=0; i<PORT_HASH_SIZE; ++i) {
    port_t *port = str->ports[i];
    while (port) {
      port_t *next = port->next;
      port_free(self, port);
      port = next;
    }
    str->ports[i] = NULL;
  }
}

static
void stream_close(struct a2j *self, int dir)
{
  stream_t *str = &self->stream[dir];

  if (str->codec)
    snd_midi_event_free(str->codec);
  if (str->new_ports)
    jack_ringbuffer_free(str->new_ports);
}

bool
a2j_jack_client_init(struct a2j * self, struct a2j_jack_client * client_ptr, const char * name)
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
    client_ptr->client = jack_client_new(name);
  }

  if (!client_ptr->client)
  {
    error_log("Cannot create jack client\n");
    goto fail;
  }

  jack_set_process_callback(client_ptr->client, jack_process, client_ptr);
  jack_set_freewheel_callback(client_ptr->client, jack_freewheel, self);
  jack_on_shutdown(client_ptr->client, jack_shutdown, NULL);

  if (jack_activate(client_ptr->client))
  {
    error_log("can't activate jack client\n");
    goto fail_close;
  }

  return true;

fail_close:
  error = jack_client_close(client_ptr->client);
  if (error != 0)
  {
    error_log("Cannot close jack client\n");
  }

fail:
  return false;
}

void
a2j_jack_client_uninit(struct a2j_jack_client * client_ptr)
{
  jack_deactivate(client_ptr->client);
  jack_client_close(client_ptr->client);
}

struct a2j *
a2j_new(const char * jack_server_name)
{
  struct a2j *self = calloc(1, sizeof(struct a2j));
  debug_log("midi: new\n");
  if (!self)
    return NULL;

  self->jack_server_name = jack_server_name;

  if (!a2j_jack_client_init(self, self->jack_clients + 0, "a2j"))
  {
    free(self);
    return NULL;
  }

  self->port_add = jack_ringbuffer_create(2*MAX_PORTS*sizeof(snd_seq_addr_t));
  self->port_del = jack_ringbuffer_create(2*MAX_PORTS*sizeof(port_t*));
  sem_init(&self->port_sem, 0, 0);

  stream_init(self, PORT_INPUT);
  stream_init(self, PORT_OUTPUT);

  return self;
}

static
void a2j_destroy(struct a2j * self)
{
  debug_log("midi: delete\n");
  a2j_detach(self);

  stream_close(self, PORT_OUTPUT);
  stream_close(self, PORT_INPUT);

  jack_ringbuffer_free(self->port_add);
  jack_ringbuffer_free(self->port_del);
  sem_close(&self->port_sem);

  a2j_jack_client_uninit(self->jack_clients + 0);

  free(self);
}

static
int a2j_attach(struct a2j * self)
{
  int err;

  debug_log("midi: attach\n");

  if (self->seq)
    return -EALREADY;

  if ((err = snd_seq_open(&self->seq, "hw", SND_SEQ_OPEN_DUPLEX, 0)) < 0) {
    error_log("failed to open alsa seq");
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

  stream_attach(self, PORT_INPUT);
  stream_attach(self, PORT_OUTPUT);

  snd_seq_nonblock(self->seq, 1);

  return 0;
}

static
int a2j_detach(struct a2j * self)
{
  debug_log("midi: detach\n");

  if (!self->seq)
    return -EALREADY;

  a2j_stop(self);

  jack_ringbuffer_reset(self->port_add);
  free_ports(self, self->port_del);

  stream_detach(self, PORT_INPUT);
  stream_detach(self, PORT_OUTPUT);

  snd_seq_close(self->seq);
  self->seq = NULL;

  return 0;
}

static void* port_thread(void *);

static void add_existing_ports(struct a2j *self);
static void update_ports(struct a2j *self);
static void add_ports(stream_t *str);

static
int a2j_start(struct a2j *self)
{
  int err;

  debug_log("midi: start\n");

  if (!self->seq)
    return -EBADF;

  if (self->keep_walking)
    return -EALREADY;

  snd_seq_connect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  snd_seq_drop_input(self->seq);

  add_existing_ports(self);
  update_ports(self);
  add_ports(&self->stream[PORT_INPUT]);
  add_ports(&self->stream[PORT_OUTPUT]);

  self->keep_walking = 1;

  if ((err = pthread_create(&self->port_thread, NULL, port_thread, self))) {
    self->keep_walking = 0;
    return -errno;
  }

  return 0;
}

static
int a2j_stop(struct a2j * self)
{
  debug_log("midi: stop\n");

  if (!self->keep_walking)
    return -EALREADY;

  snd_seq_disconnect_from(self->seq, self->port_id, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

  self->keep_walking = 0;

  sem_post(&self->port_sem);
  pthread_join(self->port_thread, NULL);
  self->port_thread = 0;

  return 0;
}

static
int alsa_connect_from(struct a2j *self, int client, int port)
{
  snd_seq_port_subscribe_t* sub;
  snd_seq_addr_t seq_addr;
  int err;

  snd_seq_port_subscribe_alloca(&sub);
  seq_addr.client = client;
  seq_addr.port = port;
  snd_seq_port_subscribe_set_sender(sub, &seq_addr);
  seq_addr.client = self->client_id;
  seq_addr.port = self->port_id;
  snd_seq_port_subscribe_set_dest(sub, &seq_addr);

  snd_seq_port_subscribe_set_time_update(sub, 1);
  snd_seq_port_subscribe_set_queue(sub, self->queue);
  snd_seq_port_subscribe_set_time_real(sub, 1);

  if ((err=snd_seq_subscribe_port(self->seq, sub)))
    error_log("can't subscribe to %d:%d - %s\n", client, port, snd_strerror(err));
  return err;
}

/*
 * ==================== Port routines =============================
 */
static inline
int port_hash(snd_seq_addr_t addr)
{
  return (addr.client + addr.port) % PORT_HASH_SIZE;
}

static
port_t* port_get(port_hash_t hash, snd_seq_addr_t addr)
{
  port_t **pport = &hash[port_hash(addr)];
  while (*pport) {
    port_t *port = *pport;
    if (port->remote.client == addr.client && port->remote.port == addr.port)
      return port;
    pport = &port->next;
  }
  return NULL;
}

static
void port_insert(port_hash_t hash, port_t *port)
{
  port_t **pport = &hash[port_hash(port->remote)];
  port->next = *pport;
  *pport = port;
}

static
void port_setdead(port_hash_t hash, snd_seq_addr_t addr)
{
  port_t *port = port_get(hash, addr);
  if (port)
    port->is_dead = 1; // see jack_process_internal
  else
    debug_log("port_setdead: not found (%d:%d)\n", addr.client, addr.port);
}

static
void port_free(struct a2j *self, port_t *port)
{
  //snd_seq_disconnect_from(self->seq, self->port_id, port->remote.client, port->remote.port);
  //snd_seq_disconnect_to(self->seq, self->port_id, port->remote.client, port->remote.port);
  if (port->early_events)
    jack_ringbuffer_free(port->early_events);
  if (port->jack_port != JACK_INVALID_PORT)
    jack_port_unregister(self->jack_clients[0].client, port->jack_port);
  info_log("port deleted: %s\n", port->name);

  free(port);
}

static
port_t* port_create(struct a2j *self, int type, snd_seq_addr_t addr, const snd_seq_port_info_t *info)
{
  port_t *port;
  char *c;
  int err;
  int client;
  snd_seq_client_info_t * client_info_ptr;

  err = snd_seq_client_info_malloc(&client_info_ptr);
  if (err != 0)
  {
    error_log("Failed to allocate client info");
    goto fail;
  }

  client = snd_seq_port_info_get_client(info);

  err = snd_seq_get_any_client_info(self->seq, client, client_info_ptr);
  if (err != 0)
  {
    error_log("Failed to get client info");
    goto fail_free_client_info;
  }

  debug_log("client name: '%s'\n", snd_seq_client_info_get_name(client_info_ptr));
  debug_log("port name: '%s'\n", snd_seq_port_info_get_name(info));

  port = calloc(1, sizeof(port_t));
  if (!port)
  {
    goto fail_free_client_info;
  }

  port->jack_port = JACK_INVALID_PORT;
  port->remote = addr;

  snprintf(port->name, sizeof(port->name), "%s : %s", snd_seq_client_info_get_name(client_info_ptr), snd_seq_port_info_get_name(info));

  // replace all offending characters by -
  for (c = port->name; *c; ++c)
    if (!isalnum(*c) && *c != '(' && *c != ')' && *c != ':')
      *c = ' ';

  port->jack_port = jack_port_register(self->jack_clients[0].client, port->name, JACK_DEFAULT_MIDI_TYPE, port_type[type].jack_caps, 0);
  if (port->jack_port == JACK_INVALID_PORT)
    goto fail_free_port;

  if (type == PORT_INPUT)
    err = alsa_connect_from(self, port->remote.client, port->remote.port);
  else
    err = snd_seq_connect_to(self->seq, self->port_id, port->remote.client, port->remote.port);
  if (err)
    goto fail_free_port;

  port->early_events = jack_ringbuffer_create(MAX_EVENT_SIZE*16);

  info_log("port created: %s\n", port->name);
  return port;

fail_free_port:
  port_free(self, port);

fail_free_client_info:
  snd_seq_client_info_free(client_info_ptr);

fail:
  return NULL;
}

/*
 * ==================== Port add/del handling thread ==============================
 */
static
void update_port_type(struct a2j *self, int type, snd_seq_addr_t addr, int caps, const snd_seq_port_info_t *info)
{
  stream_t *str = &self->stream[type];
  int alsa_mask = port_type[type].alsa_mask;
  port_t *port = port_get(str->ports, addr);

  info_log("update_port_type(%d:%d)\n", addr.client, addr.port);

  if (port && (caps & alsa_mask)!=alsa_mask) {
    debug_log("setdead: %s\n", port->name);
    port->is_dead = 1;
  }

  if (!port && (caps & alsa_mask)==alsa_mask) {
    assert (jack_ringbuffer_write_space(str->new_ports) >= sizeof(port));
    port = port_create(self, type, addr, info);
    if (port)
      jack_ringbuffer_write(str->new_ports, (char*)&port, sizeof(port));
  }
}

static
void update_port(struct a2j *self, snd_seq_addr_t addr, const snd_seq_port_info_t *info)
{
  unsigned int port_caps = snd_seq_port_info_get_capability(info);
  unsigned int port_type = snd_seq_port_info_get_type(info);

  info_log("port type: 0x%08X\n", port_type);
  info_log("port caps: 0x%08X\n", port_caps);

  if (port_type & SND_SEQ_PORT_TYPE_SPECIFIC)
  {
    info_log("SPECIFIC\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GENERIC)
  {
    info_log("MIDI_GENERIC\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GM)
  {
    info_log("MIDI_GM\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GS)
  {
    info_log("MIDI_GS\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_XG)
  {
    info_log("MIDI_XG\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_MT32)
  {
    info_log("MIDI_MT32\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_MIDI_GM2)
  {
    info_log("MIDI_GM2\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SYNTH)
  {
    info_log("SYNTH\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_DIRECT_SAMPLE)
  {
    info_log("DIRECT_SAMPLE\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SAMPLE)
  {
    info_log("SAMPLE\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_HARDWARE)
  {
    info_log("HARDWARE\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SOFTWARE)
  {
    info_log("SOFTWARE\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_SYNTHESIZER)
  {
    info_log("SYNTHESIZER\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_PORT)
  {
    info_log("PORT\n");
  }

  if (port_type & SND_SEQ_PORT_TYPE_APPLICATION)
  {
    info_log("APPLICATION\n");
  }

  if (port_type == 0)
  {
    info_log("Ignoring port of type 0\n");
    return;
  }

  if (port_type & SND_SEQ_PORT_TYPE_HARDWARE)
  {
    info_log("Ignoring hardware port\n");
    return;
  }

  if (port_caps & SND_SEQ_PORT_CAP_NO_EXPORT)
  {
    info_log("Ignoring no-export port\n");
    return;
  }

  update_port_type(self, PORT_INPUT, addr, port_caps, info);
  update_port_type(self, PORT_OUTPUT, addr, port_caps, info);
}

static
void free_ports(struct a2j *self, jack_ringbuffer_t *ports)
{
  port_t *port;
  int sz;
  while ((sz = jack_ringbuffer_read(ports, (char*)&port, sizeof(port)))) {
    assert (sz == sizeof(port));
    port_free(self, port);
  }
}

static
void update_ports(struct a2j *self)
{
  snd_seq_addr_t addr;
  int size;

  while ((size = jack_ringbuffer_read(self->port_add, (char*)&addr, sizeof(addr)))) {
    snd_seq_port_info_t *info;
    int err;

    snd_seq_port_info_alloca(&info);
    assert (size == sizeof(addr));
    assert (addr.client != self->client_id);
    if ((err=snd_seq_get_any_port_info(self->seq, addr.client, addr.port, info))>=0) {
      update_port(self, addr, info);
    } else {
      //port_setdead(self->stream[PORT_INPUT].ports, addr);
      //port_setdead(self->stream[PORT_OUTPUT].ports, addr);
    }
  }
}

static
void* port_thread(void *arg)
{
  struct a2j *self = arg;

  while (self->keep_walking) {
    sem_wait(&self->port_sem);
    free_ports(self, self->port_del);
    update_ports(self);
  }
  debug_log("port_thread exited\n");
  return NULL;
}

static
void add_existing_ports(struct a2j *self)
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
      update_port(self, addr, port_info);
    }
  }
}

/*
 * =================== Input/output port handling =========================
 */
static
void set_process_info(struct process_info *info, struct a2j *self, struct a2j_jack_client * client_ptr, int dir, jack_nframes_t nframes)
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
void add_ports(stream_t *str)
{
  port_t *port;
  while (jack_ringbuffer_read(str->new_ports, (char*)&port, sizeof(port))) {
    debug_log("jack: inserted port %s\n", port->name);
    port_insert(str->ports, port);
  }
}

static
void jack_process_internal(struct a2j *self, struct process_info *info)
{
  stream_t *str = &self->stream[info->dir];
  port_jack_func process = port_type[info->dir].jack_func;
  int i, del=0;

  add_ports(str);

  // process ports
  for (i=0; i<PORT_HASH_SIZE; ++i) {
    port_t **pport = &str->ports[i];
    while (*pport) {
      port_t *port = *pport;
      port->jack_buf = jack_port_get_buffer(port->jack_port, info->nframes);
      if (info->dir == PORT_INPUT)
        jack_midi_clear_buffer(port->jack_buf);

      if (!port->is_dead)
        (*process)(self, port, info);
      else if (jack_ringbuffer_write_space(self->port_del) >= sizeof(port)) {
        debug_log("jack: removed port %s\n", port->name);
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
void do_jack_input(struct a2j *self, port_t *port, struct process_info *info)
{
  // process port->early_events
  alsa_midi_event_t ev;
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
    debug_log("input: it's time for %d bytes at %d\n", ev.size, time);
  }
}

static
void port_event(struct a2j *self, snd_seq_event_t *ev)
{
  const snd_seq_addr_t addr = ev->data.addr;

  if (addr.client == self->client_id)
    return;

  if (ev->type == SND_SEQ_EVENT_PORT_START || ev->type == SND_SEQ_EVENT_PORT_CHANGE) {
    assert (jack_ringbuffer_write_space(self->port_add) >= sizeof(addr));

    debug_log("port_event: add/change %d:%d\n", addr.client, addr.port);
    jack_ringbuffer_write(self->port_add, (char*)&addr, sizeof(addr));
    sem_post(&self->port_sem);
  } else if (ev->type == SND_SEQ_EVENT_PORT_EXIT) {
    debug_log("port_event: del %d:%d\n", addr.client, addr.port);
    port_setdead(self->stream[PORT_INPUT].ports, addr);
    port_setdead(self->stream[PORT_OUTPUT].ports, addr);
  }
}

static
void input_event(struct a2j *self, snd_seq_event_t *alsa_event, struct process_info* info)
{
  jack_midi_data_t data[MAX_EVENT_SIZE];
  stream_t *str = &self->stream[PORT_INPUT];
  long size;
  int64_t alsa_time, time_offset;
  int64_t frame_offset, event_frame;
  port_t *port;

  port = port_get(str->ports, alsa_event->source);
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

  debug_log("input: %d bytes at event_frame=%d\n", (int)size, (int)event_frame);

  if (event_frame >= info->nframes &&
      jack_ringbuffer_write_space(port->early_events) >= (sizeof(alsa_midi_event_t) + size)) {
    alsa_midi_event_t ev;
    ev.time = event_frame + info->period_start;
    ev.size = size;
    jack_ringbuffer_write(port->early_events, (char*)&ev, sizeof(ev));
    jack_ringbuffer_write(port->early_events, (char*)data, size);
    debug_log("postponed to next frame +%d\n", (int) (event_frame - info->nframes)); 
    return;
  }

  if (event_frame < 0)
    event_frame = 0;
  else if (event_frame >= info->nframes)
    event_frame = info->nframes - 1;

  jack_midi_event_write(port->jack_buf, event_frame, data, size);
}

static
void a2j_read(struct a2j * self, struct a2j_jack_client * client_ptr, jack_nframes_t nframes)
{
  int res;
  snd_seq_event_t *event;
  struct process_info info;

  if (!self->keep_walking)
    return;

  set_process_info(&info, self, client_ptr, PORT_INPUT, nframes);
  jack_process_internal(self, &info); 

  while ((res = snd_seq_event_input(self->seq, &event))>0) {
    if (event->source.client == SND_SEQ_CLIENT_SYSTEM)
      port_event(self, event);
    else
      input_event(self, event, &info);
  }
}

/*
 * ============================ Output ==============================
 */

static
void do_jack_output(struct a2j *self, port_t *port, struct process_info* info)
{
  stream_t *str = &self->stream[info->dir];
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
    debug_log("alsa_out: written %d bytes to %s at +%d: %d\n", jack_event.size, port->name, (int)frame_offset, err);
  }
}

static
void a2j_write(struct a2j * self, struct a2j_jack_client * client_ptr, jack_nframes_t nframes)
{
  struct process_info info;

  if (!self->keep_walking)
    return;

  set_process_info(&info, self, client_ptr, PORT_OUTPUT, nframes);
  jack_process_internal(self, &info);
  snd_seq_drain_output(self->seq);
}
