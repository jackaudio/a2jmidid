= Configure it =

./waf configure

This will configure for installation to /usr/local prefix.
If you want to use other prefix, use --prefix option:

./waf configure --prefix=/usr

For full list of options, run:

./waf configure --help

There are two custom options:

 * "--disable-dbus will" force disable dbus support, even if dependencies are present
 * "--enable-pkg-config-dbus-service-dir" will force D-Bus service install
   dir to be one returned by pkg-config. This is usually needed when
   prefix is /usr/local because dbus daemon scans /usr for service
   files but does not in /usr/local

= Build it =

./waf

You can use -j option to enable building on more than one CPU:

./waf -j 4

= Install it =

./waf install

You probably want to run later as superuser to install system-wide
