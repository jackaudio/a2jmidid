= Overview =
Main goal of this project main is to ease usage of legacy, not
JACK-ified apps, in a JACK MIDI enabled system.

There are two ways to use legacy ALSA sequencer applications in JACK
MIDI system.

The first approach is to use automatic bridging. For every ALSA
sequencer port you get one JACK MIDI port. If ALSA sequencer port is
both input and output one, you get two JACK MIDI ports, one input and
output.

The second approach is to static bridges. You start application that
creates one ALSA sequencer port and one JACK MIDI port. Such bridge is
unidirectional.

First approach works almost always except when legacy ALSA sequencer
application does not create ports and/or wants existing port to exist
so it can capture from or playback to it. Such programs are using the
feature of ALSA sequencer framework that allows sending and receiving
MIDI events to/from port, without creating connection to it.

= a2jmidid =
a2jmidid is daemon that implements automatic bridging. For every ALSA
sequencer port you get one jack midi port. If ALSA sequencer port is
both input and output one, you get two JACK MIDI ports, one input and
output.

a2jmidid has two modes of operation, running bridge manually and
background D-Bus service.

To start a2jmidid in manual mode, just run it (and don't supply the
special "undocumented" dbus parameter). a2jmidid will start bridging,
and you will get output on stdout and stderr. You can stop the bridge
using ctrl-c.

Usually you want to bridge software ports and not bridge hardware
ports (they are handled by JACK itself). In case you want to force
a2jmidid to bridge hardware ports, you can use the export-hw option:

a2jmidid -e

or

a2jmidid --export-hw

In D-Bus service mode, a2jmidid works in background. When service
access is requested by some application (like a2j_control), dbus
session bus daemon activates the object by executing the service
executable. The object has methods for starting and stopping the
bridging. You can use a2j_control to do this:

a2j_control start
a2j_control stop

You can deactivate (that may cause later reactivation) the service
like this:

a2j_control exit

You can query the bridge status using this command:

a2j_control status

There also methods (and corresponding a2j_control commands) that can
be used to query mapping information.

a2jmidid implementation is based on jack-alsamidi-0.5 that is [almost]
same as jackd ALSA "seq" MIDI backend), both created by Dmitry
Baikov.

= a2jmidi_bridge =
a2jmidi_bridge is static bridge that creates one ALSA sequencer
playback port and one JACK MIDI capture port. MIDI events sent to ALSA
sequencer playback port can be read from the JACK MIDI capture port.

a2jmidi_bridge has optional argument that allows overriding name used
for JACK and ALSA client:

a2jmidi_bridge "my precious bridge"

a2jmidi_bridge implementation is based on alsaseq2jackmidi by Sean
Bolton.

= j2amidi_bridge =
j2amidi_bridge is static bridge that creates one JACK MIDI playback
port and one ALSA sequencer capture port. MIDI events sent to JACK
MIDI playback port can be read from the ALSA sequencer capture port.

j2amidi_bridge has optional argument that allows overriding name used
for JACK and ALSA client:

j2amidi_bridge "my precious bridge"

j2amidi_bridge implementation is based on jackmidi2alsaseq by Lars
Luthman.

= Contact info =
If someone wants to contribute please, contact me (Nedko Arnaudov), or
send patches, or request inclusion (Gna! a2jmidid project).

Packagers are more than welcome too.
