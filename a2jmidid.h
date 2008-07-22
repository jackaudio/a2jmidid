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

#ifndef A2JMIDID_H__A9B1D7AF_8702_48D4_899B_86B25FA4F54C__INCLUDED
#define A2JMIDID_H__A9B1D7AF_8702_48D4_899B_86B25FA4F54C__INCLUDED

bool
a2j_start();

bool
a2j_stop();

bool
a2j_is_started();

extern bool g_keep_walking;

#endif /* #ifndef A2JMIDID_H__A9B1D7AF_8702_48D4_899B_86B25FA4F54C__INCLUDED */
