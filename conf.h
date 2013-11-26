/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
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

#ifndef CONF_H__AE361BE4_EE60_4F5C_B2D4_13D71A525018__INCLUDED
#define CONF_H__AE361BE4_EE60_4F5C_B2D4_13D71A525018__INCLUDED

extern bool g_a2j_export_hw_ports;
extern bool g_disable_port_uniqueness;
extern char * g_a2j_jack_server_name;

void
a2j_conf_save();

void
a2j_conf_load();

#endif /* #ifndef CONF_H__AE361BE4_EE60_4F5C_B2D4_13D71A525018__INCLUDED */
