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

#ifndef LOG_H__76222A51_98D8_40C2_A67E_0FF38615A1DD__INCLUDED
#define LOG_H__76222A51_98D8_40C2_A67E_0FF38615A1DD__INCLUDED

void
a2j_error(
  const char * format,
  ...);

void
a2j_info(
  const char * format,
  ...);

void
a2j_debug(
  const char * format,
  ...);

#endif /* #ifndef LOG_H__76222A51_98D8_40C2_A67E_0FF38615A1DD__INCLUDED */
