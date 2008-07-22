/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2007,2008 Nedko Arnaudov <nedko@arnaudov.name>
 * Copyright (C) 2007-2008 Juuso Alasuutari
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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <dbus/dbus.h>

#include "dbus_internal.h"

static char g_xml_data[102400];

static
void
a2j_dbus_introspect(
    struct a2j_dbus_method_call * call)
{
  const char * data;

  data = g_xml_data;

  a2j_dbus_construct_method_return_single(
    call,
    DBUS_TYPE_STRING,
    &data);
}

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(Introspect)
    A2J_DBUS_METHOD_ARGUMENT("xml_data", "s", true)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHODS_BEGIN
    A2J_DBUS_METHOD_DESCRIBE(Introspect, a2j_dbus_introspect)
A2J_DBUS_METHODS_END

A2J_DBUS_IFACE_BEGIN(g_a2j_iface_introspectable, "org.freedesktop.DBus.Introspectable")
    A2J_DBUS_IFACE_EXPOSE_METHODS
A2J_DBUS_IFACE_END

static char * g_buffer_ptr;

static
void
write_line_format(const char * format, ...)
{
  va_list ap;

  va_start(ap, format);
  g_buffer_ptr += vsprintf(g_buffer_ptr, format, ap);
  va_end(ap);
}

static
void
write_line(const char * line)
{
  write_line_format("%s\n", line);
}

void a2j_introspect_init() __attribute__((constructor));

void
a2j_introspect_init()
{
  struct a2j_dbus_interface_descriptor ** interface_ptr_ptr;
  const struct a2j_dbus_interface_method_descriptor * method_ptr;
  const struct a2j_dbus_interface_method_argument_descriptor * method_argument_ptr;
  const struct a2j_dbus_interface_signal_descriptor * signal_ptr;
  const struct a2j_dbus_interface_signal_argument_descriptor * signal_argument_ptr;

  g_buffer_ptr = g_xml_data;

  write_line("<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"");
  write_line("\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">");

  write_line("<node name=\"" A2J_DBUS_OBJECT_PATH "\">");

  interface_ptr_ptr = g_a2j_dbus_interfaces;

  while (*interface_ptr_ptr != NULL)
  {
    write_line_format("  <interface name=\"%s\">\n", (*interface_ptr_ptr)->name);

    if ((*interface_ptr_ptr)->methods != NULL)
    {
      method_ptr = (*interface_ptr_ptr)->methods;
      while (method_ptr->name != NULL)
      {
        write_line_format("    <method name=\"%s\">\n", method_ptr->name);

        method_argument_ptr = method_ptr->arguments;

        while (method_argument_ptr->name != NULL)
        {
          write_line_format(
            "      <arg name=\"%s\" type=\"%s\" direction=\"%s\" />\n",
            method_argument_ptr->name,
            method_argument_ptr->type,
            method_argument_ptr->direction_out ? "out" : "in");
          method_argument_ptr++;
        }

        write_line("    </method>");
        method_ptr++;
      }
    }

    if ((*interface_ptr_ptr)->signals != NULL)
    {
      signal_ptr = (*interface_ptr_ptr)->signals;
      while (signal_ptr->name != NULL)
      {
        write_line_format("    <signal name=\"%s\">\n", signal_ptr->name);

        signal_argument_ptr = signal_ptr->arguments;

        while (signal_argument_ptr->name != NULL)
        {
          write_line_format(
            "      <arg name=\"%s\" type=\"%s\" />\n",
            signal_argument_ptr->name,
            signal_argument_ptr->type);
          signal_argument_ptr++;
        }

        write_line("    </signal>");
        signal_ptr++;
      }
    }

    write_line("  </interface>");
    interface_ptr_ptr++;
  }

  write_line("</node>");

  *g_buffer_ptr = 0;
}
