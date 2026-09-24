PKGCONF = pkg-config
PKGS = wayland-server wlroots-0.20

CFLAGS = -Wall `${PKGCONF} --cflags ${PKGS}` -DWLR_USE_UNSTABLE
LDFLAGS = `${PKGCONF} --libs ${PKGS}`

CC = cc
