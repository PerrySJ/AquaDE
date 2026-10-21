#pragma once

#include <stdbool.h>

/* AquaDE as the whole desktop, started by desk-session (which sets
 * DESK_SESSION=1) from a display manager or a console -- not nested in
 * another desktop, in a window, or under test, where all of this is the
 * other desktop's business and none of it happens.
 *
 * What a desktop does at login besides drawing itself: tell D-Bus and
 * systemd where its display is, so that what they start for it (portals,
 * the keyring, a program's helpers) can find it; and start what the XDG
 * autostart directories ask for -- the network and Bluetooth applets, the
 * authentication agent, the input method, the keyring -- see session.c. */
bool desk_session_is_desktop(void);
/* Once, with WAYLAND_DISPLAY (and DISPLAY, if there is Xwayland) set. */
struct wl_event_loop;
void desk_session_start(struct wl_event_loop *loop);
/* At its end: what was for this session only taken back. */
