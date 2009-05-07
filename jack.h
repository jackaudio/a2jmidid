/* -*- Mode: C ; c-basic-offset: 2 -*- */
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

#ifndef JACK_H__A455F430_D6DE_4978_AAE6_517E713FC305__INCLUDED
#define JACK_H__A455F430_D6DE_4978_AAE6_517E713FC305__INCLUDED

void
a2j_add_ports(
  struct a2j_stream * str);

void
a2j_do_jack_input(
  struct a2j * self,
  struct a2j_port * port,
  struct a2j_process_info * info);

void
a2j_do_jack_output(
  struct a2j * self,
  struct a2j_port * port,
  struct a2j_process_info * info);

jack_client_t *
a2j_jack_client_create(
  struct a2j * a2j_ptr,
  const char * client_name,
  const char * server_name);

#endif /* #ifndef JACK_H__A455F430_D6DE_4978_AAE6_517E713FC305__INCLUDED */
