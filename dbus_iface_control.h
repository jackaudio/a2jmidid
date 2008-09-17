/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2008 Nedko Arnaudov <nedko@arnaudov.name>
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

#ifndef DBUS_IFACE_CONTROL_H__4CC2B789_43D9_4A3F_9518_E38D517F1C4B__INCLUDED
#define DBUS_IFACE_CONTROL_H__4CC2B789_43D9_4A3F_9518_E38D517F1C4B__INCLUDED

extern struct a2j_dbus_interface_descriptor g_a2j_iface_control;

void
a2j_dbus_signal_emit_bridge_started();

void
a2j_dbus_signal_emit_bridge_stopped();

#endif /* #ifndef DBUS_IFACE_CONTROL_H__4CC2B789_43D9_4A3F_9518_E38D517F1C4B__INCLUDED */
