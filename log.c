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

#include <sys/stat.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include "log.h"

#define DEFAULT_XDG_LOG "/.log"
#define A2J_XDG_SUBDIR "/a2j"
#define A2J_XDG_LOG "/a2j.log"

FILE * g_logfile;

char *
catdup(
	const char * str1,
	const char * str2)
{
	char * str;
	size_t str1_len;
	size_t str2_len;

	str1_len = strlen(str1);
	str2_len = strlen(str2);

	str = malloc(str1_len + str2_len + 1);
	if (str == NULL)
	{
		return NULL;
	}

	memcpy(str, str1, str1_len);
	memcpy(str + str1_len, str2, str2_len);
	str[str1_len + str2_len] = 0;

	return str;
}


bool
ensure_dir_exist(
	const char * dirname,
	int mode)
{
	struct stat st;
	if (stat(dirname, &st) != 0)
	{
		if (errno == ENOENT)
		{
			a2j_info("Directory \"%s\" does not exist. Creating...", dirname);
			if (mkdir(dirname, mode) != 0)
			{
				a2j_error("Failed to create \"%s\" directory: %d (%s)", dirname, errno, strerror(errno));
				return false;
			}
		}
		else
		{
			a2j_error("Failed to stat \"%s\": %d (%s)", dirname, errno, strerror(errno));
			return false;
		}
	}
	else
	{
		if (!S_ISDIR(st.st_mode))
		{
			a2j_error("\"%s\" exists but is not directory.", dirname);
			return false;
		}
	}

	return true;
}

void a2j_log_init() __attribute__ ((constructor));
void a2j_log_init()
{
	char * log_filename;
	size_t log_len;
	char * a2j_log_dir;
	size_t a2j_log_dir_len; /* without terminating '\0' char */
	const char * home_dir;
	char * xdg_log_home;

	home_dir = getenv("HOME");
	if (home_dir == NULL)
	{
		a2j_error("Environment variable HOME not set");
		goto exit;
	}

	xdg_log_home = catdup(home_dir, DEFAULT_XDG_LOG);
	if (xdg_log_home == NULL)
	{
		a2j_error("Out of memory");
		goto exit;
	}

	a2j_log_dir = catdup(xdg_log_home, A2J_XDG_SUBDIR);
	if (a2j_log_dir == NULL)
	{
		a2j_error("Out of memory");
		goto free_log_home;
	}

	if (!ensure_dir_exist(xdg_log_home, 0700))
	{
		goto free_log_dir;
	}

	if (!ensure_dir_exist(a2j_log_dir, 0700))
	{
		goto free_log_dir;
	}

	a2j_log_dir_len = strlen(a2j_log_dir);

	log_len = strlen(A2J_XDG_LOG);

	log_filename = malloc(a2j_log_dir_len + log_len + 1);
	if (log_filename == NULL)
	{
		a2j_error("Out of memory");
		goto free_log_dir;
	}

	memcpy(log_filename, a2j_log_dir, a2j_log_dir_len);
	memcpy(log_filename + a2j_log_dir_len, A2J_XDG_LOG, log_len);
	log_filename[a2j_log_dir_len + log_len] = 0;

	g_logfile = fopen(log_filename, "a");
	if (g_logfile == NULL)
	{
		a2j_error("Cannot open a2jmidid log file \"%s\": %d (%s)\n", log_filename, errno, strerror(errno));
	}

	free(log_filename);

free_log_dir:
	free(a2j_log_dir);

free_log_home:
	free(xdg_log_home);

exit:
	return;
}

void a2j_log_uninit()  __attribute__ ((destructor));
void a2j_log_uninit()
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

	time(&timestamp);
	ctime_r(&timestamp, timestamp_str);
	timestamp_str[24] = 0;

	fprintf(stream, "%s: ", timestamp_str);

	va_start(ap, format);
	vfprintf(stream, format, ap);
	fflush(stream);
	va_end(ap);
}
