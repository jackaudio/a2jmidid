========
a2jmidid
========

This project aims to ease the usage of legacy, non |jack| enabled applications,
in a |jack| MIDI enabled system, when using |jack2|

There are two ways to use legacy |alsa| sequencer applications in a |jack| MIDI
system:

**Automatic bridging**: For every |alsa| sequencer port you get one |jack| MIDI
port. If an |alsa| sequencer port is both an input and an output, you get two
|jack| MIDI ports, one input and output.
(*This approach works almost always except when the legacy ALSA sequencer
application does not create ports and/or wants to use an existing port to
capture from or playback to. Such programs are using a feature of the |alsa|
sequencer framework that allows sending and receiving MIDI events to/from a
port, without creating a connection to it.*)

**Static bridges**: You start an application that creates one |alsa| sequencer
port and one |jack| MIDI port. Such a bridge is unidirectional.

For details on how to build and install this project, look at `INSTALLATION.rst
<INSTALLATION.rst>`_.

a2jmidid
--------

a2jmidid is a daemon that implements **automatic bridging**.

It has two modes of operation: Running a bridge manually or as a backgrounded
|dbus| service.

Start daemon
____________

To start *a2jmidid* in manual mode, just run the executable.
*a2jmidid* will start bridging, and you will get output on stdout and stderr.
You can stop the bridge using *ctrl-c*.

Usually you want to bridge software ports and not bridge hardware
ports (they are handled by |jack| itself). In case you want to force
*a2jmidid* to bridge hardware ports nonetheless, you can use the according
flags::

  a2jmidid -e

or::

  a2jmidid --export-hw

Start D-Bus service
___________________

In D-Bus service mode, a2jmidid works in the background. When service access is
requested by some application (such as *a2j_control*), the |dbus| session bus
daemon activates the object by executing the service executable.

The object has methods for starting and stopping the
bridging. You can use *a2j_control* to do this::

  a2j_control --start
  a2j_control --stop

You can deactivate (that may cause later reactivation) the service
like this::

  a2j_control --exit

You can query the bridge status using this command::

  a2j_control --status

There also methods (and corresponding a2j_control commands) that can
be used to query mapping information::

  a2j_control --help

The *a2jmidid* implementation is based on *jack-alsamidi-0.5*, which is
(almost) identical to the jackd |alsa| *seq* MIDI backend), both created by
Dmitry Baikov.

a2jmidi_bridge
--------------

*a2jmidi_bridge* creates a **static bridge** between one |alsa| sequencer
playback port and one |jack| MIDI capture port. MIDI events sent to the |alsa|
sequencer playback port can be read from the |jack| MIDI capture port.

*a2jmidi_bridge* has an optional argument that allows overriding the name used
for the |jack| and |alsa| client::

  a2jmidi_bridge "my precious bridge"

The *a2jmidi_bridge* implementation is based on *alsaseq2jackmidi* by Sean
Bolton.

j2amidi_bridge
--------------

*j2amidi_bridge* creates a **static bridge** between one |jack| MIDI playback
port and one |alsa| sequencer capture port. MIDI events sent to |jack|
MIDI playback port can be read from the |alsa| sequencer capture port.

*j2amidi_bridge* has an optional argument that allows overriding the name used
for the |jack| and |alsa| client::

  j2amidi_bridge "my precious bridge"

The *j2amidi_bridge* implementation is based on jackmidi2alsaseq by Lars
Luthman.

.. |jack| raw:: html

  <a href="http://jackaudio.org" target="_blank">JACK</a>

.. |jack2| raw:: html

  <a href="https://github.com/jackaudio/jack2" target="_blank">jack2</a>

.. |dbus| raw:: html

  <a href="https://www.freedesktop.org/wiki/Software/dbus/" target="_blank">D-Bus</a>

.. |alsa| raw:: html

  <a href="https://alsa-project.org/wiki/Main_Page" target="_blank">ALSA</a>

