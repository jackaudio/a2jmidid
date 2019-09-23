============
Installation
============

*a2jmidid* uses the |meson| build system.


Configure and build
-------------------

To configure the project, |meson|'s |meson_universal_options| (e.g. *prefix*)
can be used to prepare a build directory::

  meson --prefix=/usr build

One additional - project specific - option enables for building without |dbus|
support::

  meson --prefix=/usr -Ddisable-dbus=true build

To build the application |ninja| is required::

  ninja -C build

Install
-------

|meson| is able to install the project components to the system directories
(when run as root), while honoring the *DESTDIR* environment variable::

  DESTDIR="${pkgdir}" meson install -C build

.. |meson| raw:: html

  <a href="https://mesonbuild.com/" target="_blank">Meson</a>

.. |meson_universal_options| raw:: html

  <a href="https://mesonbuild.com/Builtin-options.html#universal-options" target="_blank">universal options</a>

.. |dbus| raw:: html

  <a href="https://www.freedesktop.org/wiki/Software/dbus/" target="_blank">D-Bus</a>

.. |ninja| raw:: html

  <a href="https://ninja-build.org/" target="_blank">Ninja</a>

