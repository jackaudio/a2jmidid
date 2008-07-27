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

#ifndef PATHS_H__7D9E0C44_6826_464C_819D_A9A05B70E3D6__INCLUDED
#define PATHS_H__7D9E0C44_6826_464C_819D_A9A05B70E3D6__INCLUDED

extern char * g_a2j_log_path;
extern char * g_a2j_conf_path;

bool
a2j_paths_init();

void
a2j_paths_uninit();

#endif /* #ifndef PATHS_H__7D9E0C44_6826_464C_819D_A9A05B70E3D6__INCLUDED */
