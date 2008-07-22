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

#include <stdbool.h>
#include <dbus/dbus.h>

#include "dbus_internal.h"

extern bool g_keep_walking;

static
void
a2j_dbus_exit(
  struct a2j_dbus_method_call * call_ptr)
{
	g_keep_walking = false;
}

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(exit)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHODS_BEGIN
  A2J_DBUS_METHOD_DESCRIBE(exit, a2j_dbus_exit)
A2J_DBUS_METHODS_END

A2J_DBUS_IFACE_BEGIN(g_a2j_iface_control, "org.gna.home.a2jmidid.control")
    A2J_DBUS_IFACE_EXPOSE_METHODS
A2J_DBUS_IFACE_END
