/* -*- Mode: C ; c-basic-offset: 2 -*- */
/*
 * ALSA SEQ < - > JACK MIDI bridge
 *
 * Copyright (c) 2008,2009,2010 Nedko Arnaudov <nedko@arnaudov.name>
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
#include <sys/stat.h>

#include "log.h"
#include "paths.h"

static ino_t g_log_file_ino;
static FILE * g_logfile = NULL;

static bool a2j_log_open(void)
{
    struct stat st;
    int ret;
    int retry;

    if (g_logfile != NULL)
    {
        ret = stat(g_a2j_log_path, &st);
        if (ret != 0 || g_log_file_ino != st.st_ino)
        {
            fclose(g_logfile);
        }
        else
        {
            return true;
        }
    }

    for (retry = 0; retry < 10; retry++)
    {
        g_logfile = fopen(g_a2j_log_path, "a");
        if (g_logfile == NULL)
        {
            fprintf(stderr, "Cannot open a2jmidid log file \"%s\": %d (%s)\n", g_a2j_log_path, errno, strerror(errno));
            return false;
        }

        ret = stat(g_a2j_log_path, &st);
        if (ret == 0)
        {
            g_log_file_ino = st.st_ino;
            return true;
        }

        fclose(g_logfile);
        g_logfile = NULL;
    }

    fprintf(stderr, "Cannot stat just opened a2jmidid log file \"%s\": %d (%s). %d retries\n", g_a2j_log_path, errno, strerror(errno), retry);
    return false;
}

bool a2j_log_init(bool use_logfile)
{
  if (use_logfile)
  {
    return a2j_log_open();
  }

  return true;
}

void a2j_log_uninit(void)
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

	if (g_logfile != NULL && a2j_log_open())
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
