/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2007,2008,2009 Nedko Arnaudov <nedko@arnaudov.name>
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

#ifndef DBUS_INTERNAL_H__9AE08E23_C592_46FB_84BD_E3D0E8721C07__INCLUDED
#define DBUS_INTERNAL_H__9AE08E23_C592_46FB_84BD_E3D0E8721C07__INCLUDED

#define A2J_DBUS_SERVICE_NAME "org.gna.home.a2jmidid"
#define A2J_DBUS_OBJECT_PATH "/"

struct a2j_dbus_method_call
{
    void *context;
    DBusConnection *connection;
    const char *method_name;
    DBusMessage *message;
    DBusMessage *reply;
};

#define A2J_DBUS_DIRECTION_IN    false
#define A2J_DBUS_DIRECTION_OUT   true

struct a2j_dbus_interface_method_argument_descriptor
{
    const char * name;
    const char * type;
    bool direction_out;     /* A2J_DBUS_DIRECTION_XXX */
};

struct a2j_dbus_interface_method_descriptor
{
    const char * name;
    const struct a2j_dbus_interface_method_argument_descriptor * arguments;
    void (* handler)(struct a2j_dbus_method_call * call);
};

struct a2j_dbus_interface_signal_argument_descriptor
{
    const char * name;
    const char * type;
};

struct a2j_dbus_interface_signal_descriptor
{
    const char * name;
    const struct a2j_dbus_interface_signal_argument_descriptor * arguments;
};

struct a2j_dbus_interface_descriptor
{
    const char * name;

    bool
    (* handler)(
        struct a2j_dbus_method_call * call,
        const struct a2j_dbus_interface_method_descriptor * methods);

    const struct a2j_dbus_interface_method_descriptor * methods;
    const struct a2j_dbus_interface_signal_descriptor * signals;
};

struct a2j_dbus_object_descriptor
{
    struct a2j_dbus_interface_descriptor ** interfaces;
    void * context;
};

#define A2J_DBUS_METHOD_ARGUMENTS_BEGIN(method_name)                                    \
static const                                                                            \
struct a2j_dbus_interface_method_argument_descriptor method_name ## _arguments[] =      \
{

#define A2J_DBUS_METHOD_ARGUMENT(argument_name, argument_type, argument_direction_out)  \
        {                                                                               \
                .name = argument_name,                                                  \
                .type = argument_type,                                                  \
                .direction_out = argument_direction_out                                 \
        },

#define A2J_DBUS_METHOD_ARGUMENTS_END                                                   \
    A2J_DBUS_METHOD_ARGUMENT(NULL, NULL, false)                                         \
};

#define A2J_DBUS_METHODS_BEGIN                                                          \
static const                                                                            \
struct a2j_dbus_interface_method_descriptor methods_dtor[] =                            \
{

#define A2J_DBUS_METHOD_DESCRIBE(method_name, handler_name)                             \
        {                                                                               \
            .name = # method_name,                                                      \
            .arguments = method_name ## _arguments,                                     \
            .handler = handler_name                                                     \
        },

#define A2J_DBUS_METHODS_END                                                            \
        {                                                                               \
            .name = NULL,                                                               \
            .arguments = NULL,                                                          \
            .handler = NULL                                                             \
        }                                                                               \
};

#define A2J_DBUS_SIGNAL_ARGUMENTS_BEGIN(signal_name)                                    \
static const                                                                            \
struct a2j_dbus_interface_signal_argument_descriptor signal_name ## _arguments[] =      \
{

#define A2J_DBUS_SIGNAL_ARGUMENT(argument_name, argument_type)                          \
        {                                                                               \
                .name = argument_name,                                                  \
                .type = argument_type                                                   \
        },

#define A2J_DBUS_SIGNAL_ARGUMENTS_END                                                   \
        A2J_DBUS_SIGNAL_ARGUMENT(NULL, NULL)                                            \
};

#define A2J_DBUS_SIGNALS_BEGIN                                                          \
static const                                                                            \
struct a2j_dbus_interface_signal_descriptor signals_dtor[] =                            \
{

#define A2J_DBUS_SIGNAL_DESCRIBE(signal_name)                                           \
        {                                                                               \
                .name = # signal_name,                                                  \
                .arguments = signal_name ## _arguments                                  \
        },

#define A2J_DBUS_SIGNALS_END                                                            \
        {                                                                               \
                .name = NULL,                                                           \
                .arguments = NULL,                                                      \
        }                                                                               \
};

#define A2J_DBUS_IFACE_BEGIN(iface_var, iface_name)                                     \
struct a2j_dbus_interface_descriptor iface_var =                                        \
{                                                                                       \
        .name = iface_name,                                                             \
        .handler = a2j_dbus_run_method,

#define A2J_DBUS_IFACE_HANDLER(handler_func)                                            \
        .handler = handler_func,

#define A2J_DBUS_IFACE_EXPOSE_METHODS                                                   \
        .methods = methods_dtor,

#define A2J_DBUS_IFACE_EXPOSE_SIGNALS                                                   \
        .signals = signals_dtor,

#define A2J_DBUS_IFACE_END                                                              \
};

#define A2J_DBUS_ERROR_GENERIC                     A2J_DBUS_SERVICE_NAME ".error.generic"
#define A2J_DBUS_ERROR_UNKNOWN_METHOD              A2J_DBUS_SERVICE_NAME ".error.unknown_method"
#define A2J_DBUS_ERROR_INVALID_ARGS                A2J_DBUS_SERVICE_NAME ".error.invalid_args"
#define A2J_DBUS_ERROR_UNKNOWN_PORT                A2J_DBUS_SERVICE_NAME ".error.unknown_port"
#define A2J_DBUS_ERROR_BRIDGE_NOT_RUNNING          A2J_DBUS_SERVICE_NAME ".error.bridge_not_running"

void
a2j_dbus_error(
  void *dbus_call_context_ptr,
  const char *error_name,
  const char *format,
  ...);

bool
a2j_dbus_run_method(
    struct a2j_dbus_method_call * call,
    const struct a2j_dbus_interface_method_descriptor * methods);

void
a2j_dbus_construct_method_return_single(
    struct a2j_dbus_method_call * call_ptr,
    int type,
    void * arg_ptr);

void
a2j_dbus_signal(
  const char * path,
  const char * interface,
  const char * name,
  int type,
  ...);

extern struct a2j_dbus_interface_descriptor * g_a2j_dbus_interfaces[];
extern struct a2j_dbus_interface_descriptor g_a2j_iface_introspectable;

#endif /* #ifndef DBUS_INTERNAL_H__9AE08E23_C592_46FB_84BD_E3D0E8721C07__INCLUDED */
