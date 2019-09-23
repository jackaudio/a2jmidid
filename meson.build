project(
  'a2jmidid',
  'c',
  default_options: [
    'prefix=/usr/local',
    'b_pie=true',
    'b_lto=true'
  ],
  meson_version: '>=0.50.0',
  license: 'GPL2+',
  version: '8')

cc = meson.get_compiler('c')

# dependency definitions
dep_jack = dependency('jack')
dep_alsa = dependency('alsa')
lib_dl = cc.find_library('dl')
lib_pthread = cc.find_library('pthread')
deps_a2jmidid = [dep_alsa, dep_jack, lib_dl, lib_pthread]

# source definitions
src_a2jmidi_bridge = ['a2jmidi_bridge.c']
src_j2amidi_bridge = ['j2amidi_bridge.c']
src_a2jmidid = [
        'a2jmidid.c',
        'log.c',
        'port.c',
        'port_thread.c',
        'port_hash.c',
        'paths.c',
        #'conf.c',
        'jack.c',
        'list.c']

# config.h input
conf_data = configuration_data()
conf_data.set('version', meson.project_version())

git = find_program('git', required : false)
if git.found()
  git_version = run_command('git', 'describe', '--long', '--tags')
  if git_version.returncode() == 0
    conf_data.set('version', git_version.stdout().strip())
  endif
endif

if get_option('disable-dbus')
  conf_data.set10('dbus', false)
else
  dep_dbus = dependency('dbus-1')
  deps_a2jmidid += [dep_dbus]
  dbus_data = configuration_data()
  dbus_data.set('bindir', join_paths(get_option('prefix'), get_option('bindir')))
  dbus_data.set('dbus_service_dir', join_paths(get_option('prefix'), '/share/dbus-1/services'))
  configure_file(
    input: 'org.gna.home.a2jmidid.service.in',
    output: 'org.gna.home.a2jmidid.service',
    install: true,
    install_dir: dbus_data.get('dbus_service_dir'),
    configuration: dbus_data
  )
  conf_data.set10('dbus', true)
  src_a2jmidid += [
    'dbus.c',
    'dbus_iface_introspectable.c',
    'dbus_iface_control.c',
    'sigsegv.c']
  install_man('man/a2j.1')
  install_man('man/a2j_control.1')
  configure_file(
    input: 'a2j',
    output: 'a2j',
    copy: true,
    install: true,
    install_dir: dbus_data.get('bindir'))
  configure_file(
    input: 'a2j_control',
    output: 'a2j_control',
    copy: true,
    install: true,
    install_dir: dbus_data.get('bindir'))
endif

config_header = configure_file(
  input: 'config.h.in',
  output: 'config.h',
  configuration: conf_data)
src_a2jmidid += [config_header]

# executables to compile
executable(
  'a2jmidi_bridge',
  sources: src_a2jmidi_bridge,
  dependencies: [dep_alsa, dep_jack],
  install: true)
executable(
  'j2amidi_bridge',
  sources: src_j2amidi_bridge,
  dependencies: [dep_alsa, dep_jack],
  install: true)
executable(
  'a2jmidid',
  sources: src_a2jmidid,
  dependencies: deps_a2jmidid,
  install: true)

# installing man pages
install_man('man/a2jmidi_bridge.1')
install_man('man/a2jmidid.1')
install_man('man/j2amidi_bridge.1')
