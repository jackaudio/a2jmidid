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

#ifndef LOG_H__76222A51_98D8_40C2_A67E_0FF38615A1DD__INCLUDED
#define LOG_H__76222A51_98D8_40C2_A67E_0FF38615A1DD__INCLUDED

#define ANSI_BOLD_ON       "\033[1m"
#define ANSI_BOLD_OFF      "\033[22m"
#define ANSI_COLOR_RED     "\033[31m"
#define ANSI_COLOR_YELLOW  "\033[33m"
#define ANSI_RESET         "\033[0m"

#define A2J_LOG_LEVEL_INFO   1
#define A2J_LOG_LEVEL_ERROR  2
#define A2J_LOG_LEVEL_DEBUG  3

void
a2j_log(
  unsigned int level,
  const char * format,
  ...);

#if defined(DEBUG)
# define a2j_debug(format, args...) a2j_log(A2J_LOG_LEVEL_DEBUG, "%s: " format "\n", __func__, ## args)
#else
# define a2j_debug(format, args...)
#endif /* DEBUG */

#define a2j_info(format, args...) a2j_log(A2J_LOG_LEVEL_INFO, format "\n", ## args)
#define a2j_error(format, args...) a2j_log(A2J_LOG_LEVEL_ERROR, ANSI_COLOR_RED "ERROR: " ANSI_RESET "%s: " format "\n", __func__, ## args)
#define a2j_warning(format, args...) a2j_log(A2J_LOG_LEVEL_ERROR, ANSI_COLOR_YELLOW "WARNING: " ANSI_RESET format "\n", ## args)

bool
a2j_log_init(
  bool use_logfile);

void
a2j_log_uninit();

#endif /* #ifndef LOG_H__76222A51_98D8_40C2_A67E_0FF38615A1DD__INCLUDED */
