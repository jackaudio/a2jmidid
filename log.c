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
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "log.h"
#include "paths.h"

FILE * g_logfile = NULL;

bool
a2j_log_init(
  bool use_logfile)
{
  if (use_logfile)
  {
    g_logfile = fopen(g_a2j_log_path, "a");
    if (g_logfile == NULL)
    {
      a2j_error("Cannot open a2jmidid log file \"%s\": %d (%s)", g_a2j_log_path, errno, strerror(errno));
      return false;
    }
  }

  return true;
}

void
a2j_log_uninit()
{
	if (g_logfile != NULL)
	{
		fclose(g_logfile);
	}
}

void
a2j_log(
	unsigned int level,
	const char * format,
	...)
{
	va_list ap;
	FILE * stream;
	time_t timestamp;
	char timestamp_str[26];

	if (g_logfile != NULL)
	{
		stream = g_logfile;
	}
	else
	{
		switch (level)
		{
		case A2J_LOG_LEVEL_DEBUG:
		case A2J_LOG_LEVEL_INFO:
			stream = stdout;
			break;
		case A2J_LOG_LEVEL_ERROR:
		default:
			stream = stderr;
		}
	}

	if (g_logfile != NULL)
  {
    time(&timestamp);
    ctime_r(&timestamp, timestamp_str);
    timestamp_str[24] = 0;

    fprintf(stream, "%s: ", timestamp_str);
  }

	va_start(ap, format);
	vfprintf(stream, format, ap);
	fflush(stream);
	va_end(ap);
}
