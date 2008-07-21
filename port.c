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
#include <ctype.h>
#include <semaphore.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "structs.h"
#include "port_hash.h"
#include "log.h"
#include "port.h"

static
int
a2j_alsa_connect_from(
  struct a2j * self,
  int client,
  int port)
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
    a2j_error("can't subscribe to %d:%d - %s", client, port, snd_strerror(err));
  return err;
}

void
a2j_port_setdead(
  a2j_port_hash_t hash,
  snd_seq_addr_t addr)
{
  struct a2j_port *port = a2j_port_get(hash, addr);
  if (port)
    port->is_dead = true; // see jack_process_internal
  else
    a2j_debug("port_setdead: not found (%d:%d)", addr.client, addr.port);
}

void
a2j_port_free(
  struct a2j_port * port)
{
  //snd_seq_disconnect_from(self->seq, self->port_id, port->remote.client, port->remote.port);
  //snd_seq_disconnect_to(self->seq, self->port_id, port->remote.client, port->remote.port);
  if (port->early_events)
    jack_ringbuffer_free(port->early_events);
  if (port->jack_port != JACK_INVALID_PORT)
    jack_port_unregister(port->a2j_ptr->jack_client, port->jack_port);

  free(port);
}

struct a2j_port *
a2j_port_create(
  struct a2j * self,
  int type,
  snd_seq_addr_t addr,
  const snd_seq_port_info_t * info)
{
  struct a2j_port *port;
  char *c;
  int err;
  int client;
  snd_seq_client_info_t * client_info_ptr;

  err = snd_seq_client_info_malloc(&client_info_ptr);
  if (err != 0)
  {
    a2j_error("Failed to allocate client info");
    goto fail;
  }

  client = snd_seq_port_info_get_client(info);

  err = snd_seq_get_any_client_info(self->seq, client, client_info_ptr);
  if (err != 0)
  {
    a2j_error("Failed to get client info");
    goto fail_free_client_info;
  }

  a2j_debug("client name: '%s'", snd_seq_client_info_get_name(client_info_ptr));
  a2j_debug("port name: '%s'", snd_seq_port_info_get_name(info));

  port = calloc(1, sizeof(struct a2j_port));
  if (!port)
  {
    goto fail_free_client_info;
  }

  port->a2j_ptr = self;

  port->jack_port = JACK_INVALID_PORT;
  port->remote = addr;

  snprintf(
    port->name,
    sizeof(port->name),
    "%s (%s): %s",
    snd_seq_client_info_get_name(client_info_ptr),
    type == PORT_INPUT ? "capture": "playback",
    snd_seq_port_info_get_name(info));

  // replace all offending characters by -
  for (c = port->name; *c; ++c)
    if (!isalnum(*c) && *c != '(' && *c != ')' && *c != ':')
      *c = ' ';

  port->jack_port = jack_port_register(self->jack_client, port->name, JACK_DEFAULT_MIDI_TYPE, g_port_type[type].jack_caps, 0);
  if (port->jack_port == JACK_INVALID_PORT)
    goto fail_free_port;

  if (type == PORT_INPUT)
    err = a2j_alsa_connect_from(self, port->remote.client, port->remote.port);
  else
    err = snd_seq_connect_to(self->seq, self->port_id, port->remote.client, port->remote.port);
  if (err)
  {
    a2j_info("port skipped: %s", port->name);
    goto fail_free_port;
  }

  port->early_events = jack_ringbuffer_create(MAX_EVENT_SIZE*16);

  a2j_info("port created: %s", port->name);
  return port;

fail_free_port:
  a2j_port_free(port);

fail_free_client_info:
  snd_seq_client_info_free(client_info_ptr);

fail:
  return NULL;
}
