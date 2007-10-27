#
# This file is part of a2jmidid
#
# Copyright (C) 2007 Nedko Arnaudov <nedko@arnaudov.name>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#

bin_PROGRAMS = a2jmidid

a2jmidid_SOURCES = a2jmidid.c jack_compat.h

a2jmidid_CFLAGS = @JACK_CFLAGS@ @ALSA_CFLAGS@ -D_GNU_SOURCE -Wall -fno-strict-aliasing
if DEV_VERSION
a2jmidid_CFLAGS +=  -Werror
endif

a2jmidid_LDADD = @JACK_LIBS@ @ALSA_LIBS@
