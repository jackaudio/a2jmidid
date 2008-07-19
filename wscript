#! /usr/bin/env python
# encoding: utf-8

import os
from Configure import g_maxlen
import Params

APPNAME='a2jmidid'
VERSION='2'

# these variables are mandatory ('/' are converted automatically)
srcdir = '.'
blddir = 'build'

def display_msg(msg, status = None, color = None):
    sr = msg
    global g_maxlen
    g_maxlen = max(g_maxlen, len(msg))
    if status:
        print "%s :" % msg.ljust(g_maxlen),
        Params.pprint(color, status)
    else:
        print "%s" % msg.ljust(g_maxlen)

def set_options(opt):
    opt.tool_options('compiler_cc')
    opt.add_option('--enable-pkg-config-dbus-service-dir', action='store_true', default=False, help='force D-Bus service install dir to be one returned by pkg-config')

def configure(conf):
    conf.check_tool('compiler_cc')

    conf.check_pkg('alsa', mandatory=True)
    conf.check_pkg('jack', vnum="0.109.0", mandatory=True)
    conf.check_pkg('dbus-1', mandatory=True, pkgvars=['session_bus_services_dir'])

    if Params.g_options.enable_pkg_config_dbus_service_dir:
        conf.env['DBUS_SERVICES_DIR'] = conf.env['DBUS-1_SESSION_BUS_SERVICES_DIR'][0]
    else:
        conf.env['DBUS_SERVICES_DIR'] = os.path.normpath(conf.env['PREFIX'] + '/share/dbus-1/services')

    print
    #display_msg("==================")
    #print
    display_msg("Install prefix", conf.env['PREFIX'], 'CYAN')
    display_msg('D-Bus service install directory', conf.env['DBUS_SERVICES_DIR'], 'CYAN')
    if conf.env['DBUS_SERVICES_DIR'] != conf.env['DBUS-1_SESSION_BUS_SERVICES_DIR'][0]:
        print
        print Params.g_colors['RED'] + "WARNING: D-Bus session services directory as reported by pkg-config is"
        print Params.g_colors['RED'] + "WARNING:",
        print Params.g_colors['CYAN'] + conf.env['DBUS-1_SESSION_BUS_SERVICES_DIR'][0]
        print Params.g_colors['RED'] + 'WARNING: but service file will be installed in'
        print Params.g_colors['RED'] + "WARNING:",
        print Params.g_colors['CYAN'] + conf.env['DBUS_SERVICES_DIR']
        print Params.g_colors['RED'] + 'WARNING: You may need to adjust your D-Bus configuration after installing'
        print 'WARNING: You can override dbus service install directory'
        print 'WARNING: with --enable-pkg-config-dbus-service-dir option to this script'
        print Params.g_colors['NORMAL'],
    print

def build(bld):
    prog = bld.create_obj('cc', 'program')
    prog.source = [
        'a2jmidid.c',
        'log.c',
        'port.c',
        'port_thread.c',
        ]
    prog.target = 'a2jmidid'
    prog.uselib = 'ALSA JACK DBUS-1'
