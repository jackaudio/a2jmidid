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

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

#define LOG_LEVEL_INFO   1
#define LOG_LEVEL_ERROR  2
#define LOG_LEVEL_DEBUG  3

void
a2j_log_stdout_stderr(
  int level,
  const char * message)
{
  FILE * out;

  switch (level)
  {
  case LOG_LEVEL_INFO:
    out = stdout;
    break;
  case LOG_LEVEL_ERROR:
    out = stderr;
    break;
#ifdef DEBUG
  case LOG_LEVEL_DEBUG:
    out = stdout;
    break;
#endif
  default:
    return;
  }

  fprintf(out, "%s\n", message);
  fflush(out);
}

void
a2j_log(
  int level,
  const char * prefix,
  const char * format,
  va_list ap)
{
  char buffer[300];
  size_t len;

  if (prefix != NULL)
  {
    len = strlen(prefix);
    memcpy(buffer, prefix, len);
  }
  else
  {
    len = 0;
  }

  vsnprintf(buffer + len, sizeof(buffer) - len, format, ap);

  a2j_log_stdout_stderr(level, buffer);
}

void
a2j_error(
  const char * format,
  ...)
{
	va_list ap;

	va_start(ap, format);
	a2j_log(LOG_LEVEL_ERROR, NULL, format, ap);
	va_end(ap);
}

void
a2j_info(
  const char * format,
  ...)
{
	va_list ap;

	va_start(ap, format);
	a2j_log(LOG_LEVEL_INFO, NULL, format, ap);
	va_end(ap);
}

void
a2j_debug(
  const char * format,
  ...)
{
	va_list ap;

	va_start(ap, format);
	a2j_log(LOG_LEVEL_DEBUG, NULL, format, ap);
	va_end(ap);
}
