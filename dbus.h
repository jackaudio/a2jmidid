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

#ifndef DBUS_H__DB007DDB_C5CF_4E9F_B73A_C3C8AC7D47DC__INCLUDED
#define DBUS_H__DB007DDB_C5CF_4E9F_B73A_C3C8AC7D47DC__INCLUDED

bool
a2j_dbus_init();

void
a2j_dbus_run(
  int timeout_milliseconds);

void
a2j_dbus_uninit();

#endif /* #ifndef DBUS_H__DB007DDB_C5CF_4E9F_B73A_C3C8AC7D47DC__INCLUDED */
