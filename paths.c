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

#include <sys/stat.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "paths.h"
#include "log.h"

#define DEFAULT_XDG_LOG "/.log"
#define DEFAULT_XDG_CONF "/.config"
#define A2J_XDG_SUBDIR "/a2j"
#define A2J_XDG_LOG "/a2j.log"
#define A2J_XDG_CONF "/a2j.conf"

char * g_a2j_log_path = NULL;
char * g_a2j_conf_path = NULL;

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

char *
a2j_path_init(
  const char * home_dir,
  const char * purpose_subdir,
  const char * file)
{
  char * dir1;
  char * dir2;
  char * filepath;

  filepath = NULL;

	dir1 = catdup(home_dir, purpose_subdir);
	if (dir1 == NULL)
	{
		a2j_error("Out of memory");
		goto exit;
	}

	dir2 = catdup(dir1, A2J_XDG_SUBDIR);
	if (dir2 == NULL)
	{
		a2j_error("Out of memory");
		goto free_dir1;
	}

	if (!ensure_dir_exist(dir1, 0700))
	{
		goto free_dir1;
	}

	if (!ensure_dir_exist(dir2, 0700))
	{
		goto free_dir2;
	}

  filepath = catdup(dir2, file);
  if (filepath == NULL)
  {
    a2j_error("Out of memory");
  }

free_dir2:
  free(dir2);

free_dir1:
  free(dir1);

exit:
  return filepath;
}

bool
a2j_paths_init()
{
	const char * home_dir;

	home_dir = getenv("HOME");
	if (home_dir == NULL)
	{
		a2j_error("Environment variable HOME not set");
		goto exit;
	}

  g_a2j_log_path = a2j_path_init(home_dir, DEFAULT_XDG_LOG, A2J_XDG_LOG);
	if (g_a2j_log_path == NULL)
	{
		goto exit;
	}

  g_a2j_conf_path = a2j_path_init(home_dir, DEFAULT_XDG_CONF, A2J_XDG_CONF);
	if (g_a2j_conf_path == NULL)
	{
		goto free_log_path;
	}

  return true;

free_log_path:
    free(g_a2j_log_path);

exit:
  return false;
}

void
a2j_paths_uninit()
{
  if (g_a2j_conf_path != NULL)
  {
    free(g_a2j_conf_path);
  }

  if (g_a2j_log_path != NULL)
  {
    free(g_a2j_log_path);
  }
}
