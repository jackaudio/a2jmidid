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

#include <stdbool.h>
#include <dbus/dbus.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

#include "dbus_internal.h"
#include "a2jmidid.h"
#include "log.h"
#include "list.h"
#include "structs.h"
#include "port_thread.h"
#include "conf.h"

#define INTERFACE_NAME "org.gna.home.a2jmidid.control"

void
a2j_dbus_signal_emit_bridge_started()
{
  a2j_dbus_signal("/", INTERFACE_NAME, "bridge_started", DBUS_TYPE_INVALID);
}

void
a2j_dbus_signal_emit_bridge_stopped()
{
  a2j_dbus_signal("/", INTERFACE_NAME, "bridge_stopped", DBUS_TYPE_INVALID);
}

static
void
a2j_dbus_exit(
  struct a2j_dbus_method_call * call_ptr)
{
	g_keep_walking = false;
  a2j_dbus_construct_method_return_void(call_ptr);
}

static void a2j_dbus_set_hw_export(struct a2j_dbus_method_call * call_ptr)
{
  DBusError error;
  dbus_bool_t hw_export;

  if (a2j_is_started())
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_BRIDGE_RUNNING, "Bridge is started");
    return;
  }

  dbus_error_init(&error);

  if (!dbus_message_get_args(
        call_ptr->message,
        &error,
        DBUS_TYPE_BOOLEAN, &hw_export,
        DBUS_TYPE_INVALID))
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_INVALID_ARGS, "Invalid arguments to method \"%s\"", call_ptr->method_name);
    dbus_error_free(&error);
    return;
  }

  g_a2j_export_hw_ports = hw_export;

  a2j_info("Hardware ports %s be exported.", g_a2j_export_hw_ports ? "will": "will not");

  a2j_dbus_construct_method_return_void(call_ptr);
}

static void a2j_dbus_get_hw_export(struct a2j_dbus_method_call * call_ptr)
{
  dbus_bool_t hw_export;

  hw_export = g_a2j_export_hw_ports;

  a2j_dbus_construct_method_return_single(
    call_ptr,
    DBUS_TYPE_BOOLEAN,
    &hw_export);
}

static
void
a2j_dbus_start(
  struct a2j_dbus_method_call * call_ptr)
{
	if (!a2j_start())
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_GENERIC, "a2j_start() failed.");
  }
  else
  {
    a2j_dbus_construct_method_return_void(call_ptr);
  }
}

static
void
a2j_dbus_stop(
  struct a2j_dbus_method_call * call_ptr)
{
	if (!a2j_stop())
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_GENERIC, "a2j_stop() failed.");
  }
  else
  {
    a2j_dbus_construct_method_return_void(call_ptr);
  }
}

static
void
a2j_dbus_is_started(
  struct a2j_dbus_method_call * call_ptr)
{
  dbus_bool_t is_started;

	is_started = a2j_is_started();

  a2j_dbus_construct_method_return_single(
    call_ptr,
    DBUS_TYPE_BOOLEAN,
    &is_started);
}

static
void
a2j_dbus_get_jack_client_name(
  struct a2j_dbus_method_call * call_ptr)
{
  const char * jack_client_name;

  jack_client_name = A2J_JACK_CLIENT_NAME;

  a2j_dbus_construct_method_return_single(
    call_ptr,
    DBUS_TYPE_STRING,
    &jack_client_name);
}

static
void
a2j_dbus_map_alsa_to_jack_port(
  struct a2j_dbus_method_call * call_ptr)
{
  DBusError error;
  dbus_uint32_t client_id;
  dbus_uint32_t port_id;
  dbus_bool_t map_playback;
  snd_seq_addr_t addr;
  struct a2j_port * port_ptr;
  const char * direction_string;
  struct a2j_stream * stream_ptr;
  const char * jack_port;

  dbus_error_init(&error);

  if (!dbus_message_get_args(
        call_ptr->message,
        &error,
        DBUS_TYPE_UINT32, &client_id,
        DBUS_TYPE_UINT32, &port_id,
        DBUS_TYPE_BOOLEAN, &map_playback,
        DBUS_TYPE_INVALID))
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_INVALID_ARGS, "Invalid arguments to method \"%s\"", call_ptr->method_name);
    dbus_error_free(&error);
    return;
  }

  if (!a2j_is_started())
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_BRIDGE_NOT_RUNNING, "Bridge not started");
    return;
  }

  addr.client = client_id;
  addr.port = port_id;

  if (map_playback)
  {
    stream_ptr = g_a2j->stream + A2J_PORT_PLAYBACK;
    direction_string = "playback";
  }
  else
  {
    stream_ptr = g_a2j->stream + A2J_PORT_CAPTURE;
    direction_string = "capture";
  }

  port_ptr = a2j_find_port_by_addr(stream_ptr, addr);
  if (port_ptr == NULL)
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_UNKNOWN_PORT, "Unknown ALSA sequencer port %u:%u (%s)", (unsigned int)client_id, (unsigned int)port_id, direction_string);
    return;
  }

  jack_port = port_ptr->name;

  a2j_info("map %u:%u (%s) -> '%s'", (unsigned int)client_id, (unsigned int)port_id, direction_string, jack_port);

  a2j_dbus_construct_method_return_single(
    call_ptr,
    DBUS_TYPE_STRING,
    &jack_port);
}

static
void
a2j_dbus_map_jack_port_to_alsa(
  struct a2j_dbus_method_call * call_ptr)
{
  snd_seq_client_info_t * client_info_ptr;
  snd_seq_port_info_t * port_info_ptr;
  DBusError error;
  const char * jack_port;
  struct a2j_port * port_ptr;
  const char * client_name;
  const char * port_name;
  dbus_uint32_t client_id;
  dbus_uint32_t port_id;
	DBusMessageIter iter;


  snd_seq_client_info_alloca(&client_info_ptr);
  snd_seq_port_info_alloca(&port_info_ptr);

  dbus_error_init(&error);

  if (!dbus_message_get_args(
        call_ptr->message,
        &error,
        DBUS_TYPE_STRING, &jack_port,
        DBUS_TYPE_INVALID))
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_INVALID_ARGS, "Invalid arguments to method \"%s\"", call_ptr->method_name);
    dbus_error_free(&error);
    return;
  }

  if (!a2j_is_started())
  {
    a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_BRIDGE_NOT_RUNNING, "Bridge not started");
    return;
  }

  port_ptr = a2j_find_port_by_jack_port_name(g_a2j->stream + A2J_PORT_CAPTURE, jack_port);
  if (port_ptr == NULL)
  {
    port_ptr = a2j_find_port_by_jack_port_name(g_a2j->stream + A2J_PORT_PLAYBACK, jack_port);
    if (port_ptr == NULL)
    {
      a2j_dbus_error(call_ptr, A2J_DBUS_ERROR_UNKNOWN_PORT, "Unknown JACK port '%s'", jack_port);
      return;
    }
  }

  if (snd_seq_get_any_client_info(g_a2j->seq, port_ptr->remote.client, client_info_ptr) >= 0)
  {
    client_name = snd_seq_client_info_get_name(client_info_ptr);
  }
  else
  {
    client_name = "";
  }

  if (snd_seq_get_any_port_info(g_a2j->seq, port_ptr->remote.client, port_ptr->remote.port, port_info_ptr) >= 0)
  {
    port_name = snd_seq_port_info_get_name(port_info_ptr);
  }
  else
  {
    port_name = "";
  }

  a2j_info("map '%s' -> %u:%u ('%s':'%s')", jack_port, (unsigned int)port_ptr->remote.client, (unsigned int)port_ptr->remote.port, client_name, port_name);

  client_id = port_ptr->remote.client;
  port_id = port_ptr->remote.port;

	call_ptr->reply = dbus_message_new_method_return(call_ptr->message);
	if (call_ptr->reply == NULL)
  {
		goto fail;
  }

	dbus_message_iter_init_append(call_ptr->reply, &iter);

  if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &client_id))
  {
    goto fail_unref;
  }

  if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &port_id))
  {
    goto fail_unref;
  }

  if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &client_name))
  {
    goto fail_unref;
  }

  if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &port_name))
  {
    goto fail_unref;
  }

	return;

fail_unref:
	dbus_message_unref(call_ptr->reply);
	call_ptr->reply = NULL;

fail:
	a2j_error("Ran out of memory trying to construct method return");
}

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(exit)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(start)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(stop)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(is_started)
  A2J_DBUS_METHOD_ARGUMENT("started", DBUS_TYPE_BOOLEAN_AS_STRING, A2J_DBUS_DIRECTION_OUT)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(get_jack_client_name)
  A2J_DBUS_METHOD_ARGUMENT("jack_client_name", DBUS_TYPE_STRING_AS_STRING, A2J_DBUS_DIRECTION_OUT)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(map_alsa_to_jack_port)
  A2J_DBUS_METHOD_ARGUMENT("alsa_client_id", DBUS_TYPE_UINT32_AS_STRING, A2J_DBUS_DIRECTION_IN)
  A2J_DBUS_METHOD_ARGUMENT("alsa_port_id", DBUS_TYPE_UINT32_AS_STRING, A2J_DBUS_DIRECTION_IN)
  A2J_DBUS_METHOD_ARGUMENT("map_playback", DBUS_TYPE_BOOLEAN_AS_STRING, A2J_DBUS_DIRECTION_IN)
  A2J_DBUS_METHOD_ARGUMENT("jack_port_name", DBUS_TYPE_STRING_AS_STRING, A2J_DBUS_DIRECTION_OUT)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(map_jack_port_to_alsa)
  A2J_DBUS_METHOD_ARGUMENT("jack_port_name", "s", A2J_DBUS_DIRECTION_IN)
  A2J_DBUS_METHOD_ARGUMENT("alsa_client_id", DBUS_TYPE_UINT32_AS_STRING, A2J_DBUS_DIRECTION_OUT)
  A2J_DBUS_METHOD_ARGUMENT("alsa_port_id", DBUS_TYPE_UINT32_AS_STRING, A2J_DBUS_DIRECTION_OUT)
  A2J_DBUS_METHOD_ARGUMENT("alsa_client_name", DBUS_TYPE_STRING_AS_STRING, A2J_DBUS_DIRECTION_OUT)
  A2J_DBUS_METHOD_ARGUMENT("alsa_port_name", DBUS_TYPE_STRING_AS_STRING, A2J_DBUS_DIRECTION_OUT)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(set_hw_export)
  A2J_DBUS_METHOD_ARGUMENT("hw_export", DBUS_TYPE_BOOLEAN_AS_STRING, A2J_DBUS_DIRECTION_IN)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHOD_ARGUMENTS_BEGIN(get_hw_export)
  A2J_DBUS_METHOD_ARGUMENT("hw_export", DBUS_TYPE_BOOLEAN_AS_STRING, A2J_DBUS_DIRECTION_OUT)
A2J_DBUS_METHOD_ARGUMENTS_END

A2J_DBUS_METHODS_BEGIN
  A2J_DBUS_METHOD_DESCRIBE(exit, a2j_dbus_exit)
  A2J_DBUS_METHOD_DESCRIBE(start, a2j_dbus_start)
  A2J_DBUS_METHOD_DESCRIBE(stop, a2j_dbus_stop)
  A2J_DBUS_METHOD_DESCRIBE(is_started, a2j_dbus_is_started)
  A2J_DBUS_METHOD_DESCRIBE(get_jack_client_name, a2j_dbus_get_jack_client_name)
  A2J_DBUS_METHOD_DESCRIBE(map_alsa_to_jack_port, a2j_dbus_map_alsa_to_jack_port)
  A2J_DBUS_METHOD_DESCRIBE(map_jack_port_to_alsa, a2j_dbus_map_jack_port_to_alsa)
  A2J_DBUS_METHOD_DESCRIBE(set_hw_export, a2j_dbus_set_hw_export)
  A2J_DBUS_METHOD_DESCRIBE(get_hw_export, a2j_dbus_get_hw_export)
A2J_DBUS_METHODS_END

A2J_DBUS_SIGNAL_ARGUMENTS_BEGIN(bridge_started)
A2J_DBUS_SIGNAL_ARGUMENTS_END

A2J_DBUS_SIGNAL_ARGUMENTS_BEGIN(bridge_stopped)
A2J_DBUS_SIGNAL_ARGUMENTS_END

A2J_DBUS_SIGNALS_BEGIN
  A2J_DBUS_SIGNAL_DESCRIBE(bridge_started)
  A2J_DBUS_SIGNAL_DESCRIBE(bridge_stopped)
A2J_DBUS_SIGNALS_END

A2J_DBUS_IFACE_BEGIN(g_a2j_iface_control, INTERFACE_NAME)
  A2J_DBUS_IFACE_EXPOSE_METHODS
  A2J_DBUS_IFACE_EXPOSE_SIGNALS
A2J_DBUS_IFACE_END
