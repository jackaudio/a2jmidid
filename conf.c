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
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <expat.h>

#include "conf.h"
#include "log.h"
#include "paths.h"

#define A2J_CONF_HEADER_TEXT                            \
  "a2jmidid settings.\n"                                \
  "You probably don't want to edit this because\n"      \
  "it will be overwritten next time a2jmidid saves.\n"

bool
a2j_settings_write_string(int fd, const char * string)
{
  size_t len;

  len = strlen(string);

  if (write(fd, string, len) != len)
  {
    a2j_error("write() failed to write config file.");
    return false;
  }

  return true;
}

bool
a2j_settings_write_option(
  int fd,
  const char * name,
  const char * content)
{
  if (!a2j_settings_write_string(fd, "  "))
  {
    return false;
  }

  if (!a2j_settings_write_string(fd, "<option name=\""))
  {
    return false;
  }

  if (!a2j_settings_write_string(fd, name))
  {
    return false;
  }

  if (!a2j_settings_write_string(fd, "\">"))
  {
    return false;
  }

  if (!a2j_settings_write_string(fd, content))
  {
    return false;
  }

  if (!a2j_settings_write_string(fd, "</option>\n"))
  {
    return false;
  }

  return true;
}

void
a2j_conf_save()
{
  int fd;
  bool ret;
  time_t timestamp;
  char timestamp_str[26];

  time(&timestamp);
  ctime_r(&timestamp, timestamp_str);
  timestamp_str[24] = 0;

  a2j_info("Saving settings to \"%s\" ...", g_a2j_conf_path);

  fd = open(g_a2j_conf_path, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd == -1)
  {
    a2j_error("open() failed to open conf filename. error is %d (%s)", errno, strerror(errno));
    goto exit;
  }

  if (!a2j_settings_write_string(fd, "<?xml version=\"1.0\"?>\n"))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, "<!--\n"))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, A2J_CONF_HEADER_TEXT))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, "-->\n"))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, "<!-- "))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, timestamp_str))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, " -->\n"))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, "<a2jmidid>\n"))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_option(fd, "jack_server_name", g_a2j_jack_server_name))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_option(fd, "export_hw_ports", g_a2j_export_hw_ports ? "true" : "false"))
  {
    goto exit_close;
  }

  if (!a2j_settings_write_string(fd, "</a2jmidid>\n"))
  {
    goto exit_close;
  }

  ret = true;

exit_close:
  close(fd);

exit:
  return;
}

#define PARSE_CONTEXT_ROOT     0
#define PARSE_CONTEXT_A2J      1
#define PARSE_CONTEXT_OPTION   2

#define MAX_STACK_DEPTH       10
#define MAX_OPTION_LENGTH     100

struct parse_context
{
  XML_Bool error;
  unsigned int element[MAX_STACK_DEPTH];
  signed int depth;
  char option[MAX_OPTION_LENGTH+1];
  int option_used;
  char * name;
};

void
a2j_conf_set_bool_option(
  const char * value_str,
  bool * value_ptr)
{
  if (strcmp(value_str, "true") == 0)
  {
    *value_ptr = true;
  }
  else if (strcmp(value_str, "false") == 0)
  {
    *value_ptr = false;
  }
  else
  {
    a2j_error("ignoring unknown bool value \"%s\"", value_str);
  }
}

void
a2j_conf_set_string_option(
  const char * input,
  char ** value)
{
  char * dup;

  dup = strdup(input);
  if (dup == NULL)
  {
    a2j_error("Out of memory");
    return;
  }

  *value = dup;
}

void
a2j_conf_set_option(
  const char * option_name,
  const char * option_value)
{
  a2j_info("setting option \"%s\" to value \"%s\"", option_name, option_value);

  if (strcmp(option_name, "jack_server_name") == 0)
  {
    a2j_conf_set_string_option(option_value, &g_a2j_jack_server_name);
  }
  else if (strcmp(option_name, "export_hw_ports") == 0)
  {
    a2j_conf_set_bool_option(option_value, &g_a2j_export_hw_ports);
  }
  else
  {
    a2j_error(
      "Unknown parameter \"%s\"",
      option_name);
    return;
  }
}

#define context_ptr ((struct parse_context *)data)

void
a2j_conf_settings_callback_chrdata(void *data, const XML_Char *s, int len)
{
  if (context_ptr->error)
  {
    return;
  }

  if (context_ptr->element[context_ptr->depth] == PARSE_CONTEXT_OPTION)
  {
    if (context_ptr->option_used + len >= MAX_OPTION_LENGTH)
    {
      a2j_error("xml parse max char data length reached");
      context_ptr->error = XML_TRUE;
      return;
    }

    memcpy(context_ptr->option + context_ptr->option_used, s, len);
    context_ptr->option_used += len;
  }
}

void
a2j_conf_settings_callback_elstart(void *data, const char *el, const char **attr)
{
  if (context_ptr->error)
  {
    return;
  }

  if (context_ptr->depth + 1 >= MAX_STACK_DEPTH)
  {
    a2j_error("xml parse max stack depth reached");
    context_ptr->error = XML_TRUE;
    return;
  }

  if (strcmp(el, "a2jmidid") == 0)
  {
    //a2j_info("<jack>");
    context_ptr->element[++context_ptr->depth] = PARSE_CONTEXT_A2J;
    return;
  }

  if (strcmp(el, "option") == 0)
  {
    //a2j_info("<option>");
    if ((attr[0] == NULL || attr[2] != NULL) || strcmp(attr[0], "name") != 0)
    {
      a2j_error("<option> XML element must contain exactly one attribute, named \"name\"");
      context_ptr->error = XML_TRUE;
      return;
    }

    context_ptr->name = strdup(attr[1]);
    if (context_ptr->name == NULL)
    {
      a2j_error("strdup() failed");
      context_ptr->error = XML_TRUE;
      return;
    }

    context_ptr->element[++context_ptr->depth] = PARSE_CONTEXT_OPTION;
    context_ptr->option_used = 0;
    return;
  }

  a2j_error("unknown element \"%s\"", el);
  context_ptr->error = XML_TRUE;
}

void
a2j_conf_settings_callback_elend(void *data, const char *el)
{
  if (context_ptr->error)
  {
    return;
  }

  //a2j_info("element end (depth = %d, element = %u)", context_ptr->depth, context_ptr->element[context_ptr->depth]);

  if (context_ptr->element[context_ptr->depth] == PARSE_CONTEXT_OPTION)
  {
    context_ptr->option[context_ptr->option_used] = 0;

    if (context_ptr->depth == 1 &&
        context_ptr->element[0] == PARSE_CONTEXT_A2J)
    {
      a2j_conf_set_option(context_ptr->name, context_ptr->option);
    }
  }

  context_ptr->depth--;

  if (context_ptr->name != NULL)
  {
    free(context_ptr->name);
    context_ptr->name = NULL;
  }
}

#undef context_ptr

void
a2j_conf_load()
{
  XML_Parser parser;
  int bytes_read;
  void *buffer;
  struct stat st;
  int fd;
  enum XML_Status xmls;
  struct parse_context context;

  a2j_info("Loading settings from \"%s\" using %s ...", g_a2j_conf_path, XML_ExpatVersion());

  if (stat(g_a2j_conf_path, &st) != 0)
  {
    if (errno == ENOENT)
    {
      a2j_info("No conf file found, using defaults...");
      return;
    }

    a2j_error("failed to stat \"%s\", error is %d (%s)", g_a2j_conf_path, errno, strerror(errno));
  }

  fd = open(g_a2j_conf_path, O_RDONLY);
  if (fd == -1)
  {
    a2j_error("open() failed to open conf filename.");
    goto exit;
  }

  parser = XML_ParserCreate(NULL);
  if (parser == NULL)
  {
    a2j_error("XML_ParserCreate() failed to create parser object.");
    goto exit_close_file;
  }

  //a2j_info("conf file size is %llu bytes", (unsigned long long)st.st_size);

  /* we are expecting that conf file has small enough size to fit in memory */

  buffer = XML_GetBuffer(parser, st.st_size);
  if (buffer == NULL)
  {
    a2j_error("XML_GetBuffer() failed.");
    goto exit_free_parser;
  }

  bytes_read = read(fd, buffer, st.st_size);
  if (bytes_read != st.st_size)
  {
    a2j_error("read() returned unexpected result.");
    goto exit_free_parser;
  }

  context.error = XML_FALSE;
  context.depth = -1;
  context.name = NULL;

  XML_SetElementHandler(parser, a2j_conf_settings_callback_elstart, a2j_conf_settings_callback_elend);
  XML_SetCharacterDataHandler(parser, a2j_conf_settings_callback_chrdata);
  XML_SetUserData(parser, &context);

  xmls = XML_ParseBuffer(parser, bytes_read, XML_TRUE);
  if (xmls == XML_STATUS_ERROR)
  {
    a2j_error("XML_ParseBuffer() failed.");
    goto exit_free_parser;
  }

exit_free_parser:
  XML_ParserFree(parser);

exit_close_file:
  close(fd);

exit:
  return;
}
