#!/bin/sh

install -vDm 755 a2j -t "${DESTDIR}/${MESON_INSTALL_PREFIX}/bin"
install -vDm 755 a2j_control -t "${DESTDIR}/${MESON_INSTALL_PREFIX}/bin"
