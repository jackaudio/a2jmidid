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
#include <stdio.h>
#include <string.h>
#include <dbus/dbus.h>

#include "dbus.h"
#include "log.h"
#include "dbus_internal.h"
#include "dbus_iface_control.h"

void
a2j_dbus_error(
  void *dbus_call_context_ptr,
  const char *error_name,
  const char *format,
  ...)
{
  va_list ap;
  char buffer[300];

  va_start(ap, format);

  vsnprintf(buffer, sizeof(buffer), format, ap);

  a2j_error("%s", buffer);
  if (dbus_call_context_ptr != NULL)
  {
    ((struct a2j_dbus_method_call *)dbus_call_context_ptr)->reply = dbus_message_new_error(
      ((struct a2j_dbus_method_call *)dbus_call_context_ptr)->message,
      error_name,
      buffer);
  }

  va_end(ap);
}

/*
 * Send a method return.
 *
 * If call->reply is NULL (i.e. a message construct method failed
 * due to lack of memory) attempt to send a void method return.
 */
static
void
a2j_dbus_send_method_return(
  struct a2j_dbus_method_call * call)
{
  if (call->reply == NULL)
  {
    a2j_debug("send_method_return() called with a NULL message, trying to construct a void return...");

    call->reply = dbus_message_new_method_return(call->message);
    if (call->reply == NULL)
    {
      a2j_error("Failed to construct method return!");
      return;
    }
  }

  if (!dbus_connection_send(call->connection, call->reply, NULL))
  {
    a2j_error("Ran out of memory trying to queue method return");
  }

  dbus_connection_flush(call->connection);
  dbus_message_unref(call->reply);
  call->reply = NULL;
}

/*
 * Construct a method return which holds a single argument or, if
 * the type parameter is DBUS_TYPE_INVALID, no arguments at all
 * (a void message).
 *
 * The operation can only fail due to lack of memory, in which case
 * there's no sense in trying to construct an error return. Instead,
 * call->reply will be set to NULL and handled in send_method_return().
 */
void
a2j_dbus_construct_method_return_single(
    struct a2j_dbus_method_call * call_ptr,
    int type,
    void * arg)
{
    DBusMessageIter iter;

    call_ptr->reply = dbus_message_new_method_return(call_ptr->message);
    if (call_ptr->reply == NULL)
    {
        goto fail_no_mem;
    }

    dbus_message_iter_init_append(call_ptr->reply, &iter);

    if (!dbus_message_iter_append_basic(&iter, type, arg))
    {
        dbus_message_unref(call_ptr->reply);
        call_ptr->reply = NULL;
        goto fail_no_mem;
    }

    return;

fail_no_mem:
    a2j_error("Ran out of memory trying to construct method return");
}

#define descriptor_ptr ((struct a2j_dbus_object_descriptor *)data)

DBusHandlerResult
a2j_dbus_message_handler(
  DBusConnection * connection,
  DBusMessage * message,
  void * data)
{
  struct a2j_dbus_method_call call;
  const char *interface_name;
  struct a2j_dbus_interface_descriptor ** interface_ptr_ptr;

  /* Check if the message is a method call. If not, ignore it. */
  if (dbus_message_get_type (message) != DBUS_MESSAGE_TYPE_METHOD_CALL)
  {
    goto handled;
  }

  /* Get the invoked method's name and make sure it's non-NULL. */
  if (!(call.method_name = dbus_message_get_member (message)))
  {
    a2j_dbus_error(
      &call,
      A2J_DBUS_ERROR_UNKNOWN_METHOD,
      "Received method call with empty method name");
    goto send_return;
  }

  /* Initialize our data. */
  call.context = descriptor_ptr->context;
  call.connection = connection;
  call.message = message;
  call.reply = NULL;

  /* Check if there's an interface specified for this method call. */
  interface_name = dbus_message_get_interface (message);
  if (interface_name != NULL)
  {
    /* Check if we can match the interface and method.
     * The inteface handler functions only return false if the
     * method name was unknown, otherwise they run the specified
     * method and return TRUE.
     */

    interface_ptr_ptr = descriptor_ptr->interfaces;

    while (*interface_ptr_ptr != NULL)
    {
      if (strcmp(interface_name, (*interface_ptr_ptr)->name) == 0)
      {
        if (!(*interface_ptr_ptr)->handler(&call, (*interface_ptr_ptr)->methods))
        {
          break;
        }

        goto send_return;
      }

      interface_ptr_ptr++;
    }
  }
  else
  {
    /* No interface was specified so we have to try them all. This is
     * dictated by the D-Bus specification which states that method calls
     * omitting the interface must never be rejected.
     */

    interface_ptr_ptr = descriptor_ptr->interfaces;

    while (*interface_ptr_ptr != NULL)
    {
      if ((*interface_ptr_ptr)->handler(&call, (*interface_ptr_ptr)->methods))
      {
        goto send_return;
      }

      interface_ptr_ptr++;
    }
  }

  a2j_dbus_error(
    &call,
    A2J_DBUS_ERROR_UNKNOWN_METHOD,
    "Method \"%s\" with signature \"%s\" on interface \"%s\" doesn't exist",
    call.method_name,
    dbus_message_get_signature(message),
    interface_name);

send_return:
  a2j_dbus_send_method_return(&call);

handled:
  return DBUS_HANDLER_RESULT_HANDLED;
}

void
a2j_dbus_message_handler_unregister(
    DBusConnection *connection,
    void *data)
{
    a2j_debug("Message handler was unregistered");
}

#undef descriptor_ptr

/*
 * Check if the supplied method name exists in method descriptor,
 * if it does execute it and return TRUE. Otherwise return FALSE.
 */
bool
a2j_dbus_run_method(
    struct a2j_dbus_method_call *call,
    const struct a2j_dbus_interface_method_descriptor * methods)
{
    const struct a2j_dbus_interface_method_descriptor * method_ptr;

    method_ptr = methods;

    while (method_ptr->name != NULL)
    {
        if (strcmp(call->method_name, method_ptr->name) == 0)
        {
            method_ptr->handler(call);
            return TRUE;
        }

        method_ptr++;
    }

    return FALSE;
}

DBusConnection * g_dbus_connection_ptr;
struct a2j_dbus_object_descriptor g_a2j_dbus_object_descriptor;
struct a2j_dbus_interface_descriptor * g_a2j_dbus_interfaces[] =
{
    &g_a2j_iface_introspectable,
    &g_a2j_iface_control,
    NULL
};

bool
a2j_dbus_init()
{
	DBusError dbus_error;
  int ret;
    DBusObjectPathVTable vtable =
    {
        a2j_dbus_message_handler_unregister,
        a2j_dbus_message_handler,
        NULL
    };

	dbus_error_init(&dbus_error);
  g_dbus_connection_ptr = dbus_bus_get(DBUS_BUS_SESSION, &dbus_error);
	if (dbus_error_is_set(&dbus_error))
  {
		a2j_error("Failed to get bus: %s", dbus_error.message);
		goto fail;
	}

  dbus_connection_set_exit_on_disconnect(g_dbus_connection_ptr, FALSE);

  a2j_debug("D-Bus unique name is '%s'", dbus_bus_get_unique_name(g_dbus_connection_ptr));

  ret = dbus_bus_request_name(g_dbus_connection_ptr, A2J_DBUS_SERVICE_NAME, DBUS_NAME_FLAG_DO_NOT_QUEUE, &dbus_error);
  if (ret == -1)
  {
    a2j_error("Failed to acquire bus name: %s", dbus_error.message);
    goto fail_unref_dbus_connection;
  }

  if (ret == DBUS_REQUEST_NAME_REPLY_EXISTS)
  {
    a2j_error("Requested bus name already exists");
    goto fail_unref_dbus_connection;
  }

  g_a2j_dbus_object_descriptor.context = NULL;
  g_a2j_dbus_object_descriptor.interfaces = g_a2j_dbus_interfaces;

  if (!dbus_connection_register_object_path(
        g_dbus_connection_ptr,
        A2J_DBUS_OBJECT_PATH,
        &vtable,
        &g_a2j_dbus_object_descriptor))
  {
    a2j_error("Ran out of memory trying to register D-Bus object path");
    goto fail_unref_dbus_connection;
  }

  return true;

fail_unref_dbus_connection:
  dbus_connection_unref(g_dbus_connection_ptr);

fail:

  return false;
}

bool
a2j_dbus_run(
  int timeout_milliseconds)
{
  return dbus_connection_read_write_dispatch(g_dbus_connection_ptr, timeout_milliseconds);
}

void
a2j_dbus_uninit()
{
  dbus_connection_unref(g_dbus_connection_ptr);
}

void
a2j_dbus_signal(
  const char * path,
  const char * interface,
  const char * name,
  int type,
  ...)
{
  va_list ap;
	DBusMessage * message_ptr;

  a2j_debug("Sending signal %s.%s from %s", interface, name, path);

  message_ptr = dbus_message_new_signal (path, interface, name);

  if (message_ptr == NULL)
  {
    a2j_error("Ran out of memory trying to create new signal");
    return;
  }

  va_start(ap, type);
  if (dbus_message_append_args_valist(message_ptr, type, ap))
  {
    if (!dbus_connection_send(g_dbus_connection_ptr, message_ptr, NULL))
    {
      a2j_error("Ran out of memory trying to queue signal");
    }

    dbus_connection_flush(g_dbus_connection_ptr);
  }
  else
  {
    a2j_error("Ran out of memory trying to append signal argument(s)");
  }
  va_end(ap);

  dbus_message_unref(message_ptr);
}
