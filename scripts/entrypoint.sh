#!/bin/sh
set -eu
mkdir -p /out /tmp/xdg
export XDG_RUNTIME_DIR=/tmp/xdg
export XDG_SESSION_TYPE=wayland
export WLR_RENDERER="${WLR_RENDERER:-pixman}"
export WLR_LIBINPUT_NO_DEVICES=1
chmod 700 /tmp/xdg
# the desk's settings and cache, the container's own: the lamp lit at once,
# so the picture is of the desk and not of the dark room it starts in; and
# a cache folder that is there (the desk makes only its own folder in it),
# so the room aquade-preview traces below is kept for the desk after it
export XDG_CONFIG_HOME=/tmp/aquade-config XDG_CACHE_HOME=/tmp/aquade-cache
mkdir -p "$XDG_CONFIG_HOME/aquade" "$XDG_CACHE_HOME"
printf 'lights_at_login = no\n' > "$XDG_CONFIG_HOME/aquade/shell.conf"

echo ">> installed Wayland session"
ls -l /usr/bin/aquade /usr/bin/aquade-session /usr/bin/aquade-preview
ls -l /usr/share/wayland-sessions/aquade.desktop
ls -l /usr/share/xdg-desktop-portal/aquade-portals.conf
if [ -d /opt/aquade-destdir ]; then
	rm -rf /out/session-root
	cp -a /opt/aquade-destdir /out/session-root
fi
printf '%s\n' \
	'/usr/bin/aquade' \
	'/usr/bin/aquade-session' \
	'/usr/bin/aquade-preview' \
	'/usr/share/wayland-sessions/aquade.desktop' \
	'/usr/share/xdg-desktop-portal/aquade-portals.conf' \
	> /out/session-files.txt

echo ">> rendering cairo preview"
# With no GPU the room is traced on the CPU: minutes of every core the
# container has, once, for the first picture; the rest find it in the cache.
aquade-preview -o /out/preview.png -s 1600x1000
aquade-preview -o /out/drawer-closed.png -s 1600x1000 --closed

echo ">> starting compositor (headless)"
# The room is the one aquade-preview traced; the screenshot waits for the
# desk to come up on it (DESK_VERIFY_DELAY, in milliseconds).
aquade --headless --size 1600x1000 \
	--dump /out/chrome.png \
	--screenshot /out/desktop.png \
	--delay "${DESK_VERIFY_DELAY:-10000}" \
	--exit-after-screenshot \
	-s "foot" || true

if [ ! -f /out/desktop.png ] && [ -f /out/chrome.png ]; then
	cp /out/chrome.png /out/desktop.png
fi

echo ">> outputs:"
ls -l /out || true
