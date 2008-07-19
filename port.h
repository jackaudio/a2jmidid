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

#ifndef PORT_H__757ADD0F_5E53_41F7_8B7F_8119C5E8A9F1__INCLUDED
#define PORT_H__757ADD0F_5E53_41F7_8B7F_8119C5E8A9F1__INCLUDED

enum {
  PORT_HASH_BITS = 4,
  PORT_HASH_SIZE = 1 << PORT_HASH_BITS
};

typedef struct a2j_port* port_hash_t[PORT_HASH_SIZE];

struct a2j_port
{
  struct a2j_port * next;
  bool is_dead;
  char name[64];
  snd_seq_addr_t remote;
  jack_port_t * jack_port;

  jack_ringbuffer_t * early_events; // alsa_midi_event_t + data
  int64_t last_out_time;

  void * jack_buf;
};

struct a2j_port *
a2j_port_create(
  struct a2j * self,
  int type,
  snd_seq_addr_t addr,
  const snd_seq_port_info_t * info);

void
a2j_port_insert(
  port_hash_t hash,
  struct a2j_port * port);

struct a2j_port *
a2j_port_get(
  port_hash_t hash,
  snd_seq_addr_t addr);

void
a2j_port_setdead(
  port_hash_t hash,
  snd_seq_addr_t addr);

void
a2j_port_free(
  struct a2j * self,
  struct a2j_port * port);

#endif /* #ifndef PORT_H__757ADD0F_5E53_41F7_8B7F_8119C5E8A9F1__INCLUDED */
