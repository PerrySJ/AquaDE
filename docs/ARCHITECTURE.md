# AquaDE architecture

This is the technical overview of AquaDE v0.1.0 (BETA): what the programs are,
how the compositor is put together, how the tank is drawn, which threads
exist and what they own, and where everything is kept. It is meant for
people reading or changing the code. For what AquaDE looks like and how to use
it, read the [README](../README.md); for recipes for common changes, read
[MODDING.md](MODDING.md); for every setting and file, read
[CONFIGURATION.md](CONFIGURATION.md). DEsk's full development log, in
Japanese, is [design.md](design.md).

AquaDE is forked from [DEsk](https://github.com/fyzugx/DEsk), and the code
still uses DEsk's names inside: `desk_` functions, `DESK_` variables,
`struct desk_server`, and "the room" and "the desk" for what is now the
tank and its sandy floor. This page uses the code's names where it points
at code.

## 1. Principles

These decide most questions about where code goes and what it does.

- **The tank is a place, not a skin.** Windows are sheets of diver's
  plastic on the sand, the dock is a drawer of gravel, the app list is
  seashells on a sunken bookcase, notifications are letters in a treasure
  chest. A new feature is designed by asking what the thing in the tank
  would do, not by adding a button. (DEsk's rule, kept.)
- **Make only what the tank is, or what only a compositor can do.** Settings
  panels, applets, file managers and so on are borrowed from existing free
  software (Xfce, MATE, LXQt, GNOME, NetworkManager...). Shell Settings opens
  them. The line is drawn in `src/settings.c` (top comment).
- **One light, one tank.** The tank (the floor, drawer, bookcase, Control
  Panel, treasure chest) is a single ray-traced scene. It is traced once per screen
  shape and quality level, cached on disk, and composited under everything
  else. Everything drawn over it (sheets, letters, the clock's light) is
  shaded by the same light, coming down through the water.
- **Sizes are in points.** One room unit is 45 points and 1440 x 900 is the
  1x reference. A bigger screen shows more of the tank, not bigger things
  (`desk_px_per_pt`, `DESK_SCALE`).
- **The compositor thread never waits.** Tracing, disk walks, icon
  conversion, mixer queries, PAM, the weather and sound all run on threads
  of their own and hand results back through the event loop.
- **Light enough for old machines.** The target is a 2010 laptop running Low
  at 60 fps. Every level (Ultra, High, Medium, Low, 2D Fallback) is the same
  code with less work done, never a second implementation.
- **English first, then the world.** UI words are written in English in the
  code and translated through a table (seven languages). Seasonal
  touches are ones every culture shares (a birthday, the new year).

## 2. The programs

| Program | Source | What it is |
| --- | --- | --- |
| `aquade` | `src/wm.c` + the drawing library | The Wayland compositor: the whole desktop. Also the GPU trial child (`aquade-gpu-trial`, the same executable run with `DESK_GPU_PROBE`). |
| `aquade-session` | `session/aquade-session` (sh) | What a login manager starts. Sets up the session environment (Wayland-first toolkit variables, the input method, AquaDE's dconf layer), keeps `~/.local/state/aquade/session.log`, and runs `aquade` with `DESK_SESSION=1`. |
| `aquade-session-helper` | `scripts/aquade-session-helper` (sh) | Picks and starts the parts AquaDE does not draw (polkit agent, keyring, IBus or fcitx, the network applet, udiskie, battery notices) from AquaDE's own autostart entries. |
| `aquade-settings` | `src/settings.c` | Shell Settings. A normal Wayland client drawn with the same library, writing `shell.conf`. |
| `aquade-index` | `src/index.c` | The Experience Index, from DEsk (benchmarks scored 1.0 to 9.9). `--text` for a terminal, `--suggest` for the installer's choice of Graphics level. |
| `aquade-preview` | `src/preview.c` | Renders the tank, views and parts to a PNG with no compositor: the main tool for checking drawing changes. |
| `aquade-gputest` | `src/gputest.c` | Checks the GPU path. `aquade-gputest --room` traces every sheet on the GPU and the CPU and compares them. |
| `aquade-ctl` | `scripts/aquade-ctl` (sh) | `reset` and `logout` for a stuck session, from a terminal or another VT. |
| `aquade-window` | `scripts/aquade-window` (sh) | Runs AquaDE as a window inside the current session. |
| `aquade-screenshot` | `scripts/aquade-screenshot` (sh) | Print / Alt+Print: grim, a file in Pictures, and a letter. |

### How a session starts

1. The login manager runs `aquade-session` from
   `/usr/share/wayland-sessions/aquade.desktop`.
2. `aquade-session` sets `XDG_CURRENT_DESKTOP=AquaDE`, the toolkit backends, the
   input method choice and the dconf profile, then runs `aquade`.
3. `aquade` takes an exclusive `flock` on `$XDG_RUNTIME_DIR/aquade.lock` (one
   session per user; `DESK_ALLOW_MULTIPLE=1` to override), starts wlroots, and
   paints the first frame while the tank is read from the cache or traced.
4. Once the display exists, `src/session.c` tells D-Bus and systemd where it
   is (`dbus-update-activation-environment --systemd`) and runs the XDG
   autostart entries, leaving out ones that would fight AquaDE (another
   notification daemon, an X screen locker, other desktops' polkit agents).
5. AquaDE's own autostart entries (`session/autostart/*.desktop`,
   `OnlyShowIn=AquaDE`) run `aquade-session-helper` for each borrowed part.

Logging out, restarting and powering off ask every window to close first
(`end_begin` in `wm.c`) and then go through logind (`src/power.c`).

## 3. Source map

| File | Lines | Role |
| --- | ---: | --- |
| `src/wm.c` | ~23,400 | The compositor: wlroots setup, outputs, input, windows (xdg-shell and Xwayland), the scene layers, every interactive part of the tank (drawer, views, calendar, memo, treasure chest, tray, clock, lock, the shark, the bubbles and fish, Ctrl+Alt+Del), `shell.conf` and `displays.conf`, multi-screen. |
| `src/draw.c` | ~36,600 | The drawing library: the ray-traced tank (C reference tracer, GPU driver, cache), and every cairo-drawn thing (window sheets, frames, folding, crumpling, the book and seashells, the Control Panel's live parts and sonar, letters and bottles, the calendar and memo pad, Shell Settings' form, the watertight door, the shark, bubbles, fish and crabs). |
| `src/room.glsl` | ~2,650 | The tank's tracer as a GLES 3.00 fragment shader: a line-for-line port of the C in `draw.c`, which stays the reference. |
| `src/gpu.c` | ~800 | Offscreen EGL/GBM/GLES context for the shader; picks the render node the screen uses. |
| `src/apps.c` | ~2,000 | Scans `.desktop` files on a worker thread, files them on eight shelves, finds icons, picks the standard terminal/browser/files/editor, launches programs. |
| `src/i18n.c` | ~1,700 | The word table (seven languages) and month/day names. `include/i18n.h` explains the scheme. |
| `src/fonts.c` | ~280 | Which typeface each role uses, per CJK region. |
| `src/settings.c` | ~3,200 | `aquade-settings` (Shell Settings). |
| `src/index.c` | ~4,500 | `aquade-index`. |
| `src/post.c` | ~440 | The notification daemon (`org.freedesktop.Notifications`). |
| `src/tray.c` | ~1,350 | StatusNotifierItem watcher and host (the tray slips). |
| `src/media.c` | ~800 | MPRIS client (the clock's MEDIA page, media keys). |
| `src/ime.c` | ~760 | Which input method is on (IBus, fcitx5). |
| `src/session.c` | ~470 | Autostart and the session environment. |
| `src/lock.c`, `src/user.c` | | PAM password check (on a thread); the user's name and picture for the lock door. |
| `src/power.c` | | Log out / restart / power off through logind. |
| `src/panel.c` | ~950 | The Control Panel's readings from `/proc`, `/sys` and the mixer. |
| `src/occasion.c` | ~530 | Dates (birthday, the new year...) and the weather overhead, which dims the light at the lock door (open-meteo.com, opt-out). |
| `src/agenda.c`, `src/memo.c` | | The calendar's notes and the memo pad's sheets, saved under `$XDG_DATA_HOME/aquade`. |
| `src/camera.c` | ~260 | Whether a camera or microphone is in use (the clock's two lamps). |
| `src/sfx.c`, `src/sound.c` | ~2,750 | Every sound synthesised at start-up, and a mixer thread that plays them (libpulse, or ALSA loaded at run time). |
| `src/preview.c`, `src/gputest.c` | | Developer tools (see §2). |
| `include/desk.h` | ~1,250 | The drawing library's API, as used by `wm.c`, `settings.c`, `index.c` and the tools. |
| `include/compat.h` | | From DEsk: carries settings over from its old name (DeskWM). Does nothing for AquaDE. |
| `protocols/` | | wlr-output-management and wlr-output-power-management XML. |
| `scripts/` | | Installers, session scripts, build helpers (`glsl2c.py`, `room-key.py`), `check-words.py` (the translation test). |
| `session/` | | Session entry, PAM file, autostart entries, default applications list, portal configuration. |
| `themes/` | | The icon theme (generated by `themes/make-icons.py`, redrawn for the tank) and the GTK theme (still DEsk's look). |
| `packaging/` | | A Debian equivs file for the recommended program set. |
| `docs/` | | These documents, the screenshots and DEsk's development log. |

Both large files are organised in sections marked with comments of the form
`/* ---- name ---- */` (and `/* ==== */` banners in `draw.c`). Searching for
`---- ` gives a table of contents. Each section begins with a comment saying
what it does and why.

## 4. The compositor (`src/wm.c`)

AquaDE is built on **wlroots 0.18**. The backend and renderer are whatever
wlroots picks (DRM and libinput under a login session; the Wayland or X11
backend when nested; headless for tests); the renderer is GLES2 when there
is a GPU and pixman otherwise. One `struct desk_server` holds the state.

### 4.1 The scene graph

Everything on screen is a node in one `wlr_scene`. The layers are trees
created in this order, bottom to top:

| Layer | Contents |
| --- | --- |
| `layer_side` | More tank floor on screens other than the main one (§4.6). |
| `layer_bg` | The traced tank (one buffer), moved by the camera and the drawer. |
| `layer_desk` | Live parts drawn over the tank: the clock, the Control Panel's lamps and needles, the chest's bottles, slips, the light under the pointer, and (in a tree of their own, `bub_tree`) the bubbles, fish and crab. |
| `layer_folded` | Folded windows lying on the sand (under every window). |
| `layer_win` | Windows: each a tree with its sheet frame, shadow, corner and the client's surfaces. |
| `layer_full` | A fullscreen window. |
| `layer_xover` | X11 override-redirect windows (menus, tooltips) and input-method candidate windows. |
| `layer_drawer` | The drawer, pulled out over the floor, and what lies in it. |
| `layer_parked` | Sheets being folded or opened out (over the windows while they move). |
| `layer_shelf`, `layer_console`, `layer_post` | The three views you turn to: the bookcase, the Control Panel, the treasure chest. |
| `layer_top` | The dark and the counting clock at login, a letter on a first start. |
| `layer_drag` | Whatever is being carried. |
| `layer_lock` | The watertight door (the screen lock), with black over every screen. |
| `layer_glow` | Light from fireworks on the new year. |
| `layer_dark` | The black of a screen that has gone dark when nested. |

The floor layers (`layer_bg`, `layer_desk`, `layer_drawer`) move together
when the drawer opens and during the start of a camera move; the window
layers (`layer_folded`, `layer_win`, `layer_xover`, `layer_parked`) move with
them (`desk_follow`).

### 4.2 Buffers and sprites

AquaDE draws with **cairo on the CPU** into `struct desk_buf` (a `wlr_buffer`
wrapping a cairo image surface) and hands the buffers to the scene with
`scene_put`. Anything that changes often is a *sprite*: a buffer covering
only what changed, keyed by a hash of what it shows (`struct desk_sprite`,
`sprite_paint`), so an unchanged thing is never drawn again. Over the tank,
drawing is done at an overlay resolution that depends on the level
(`desk_overlay_size`) and scaled by the scene, which keeps large screens
cheap. Every source box goes through `scene_source`, which keeps it inside
its picture to the last bit (wlroots asserts on a box that reaches past the
picture, and one worked out in floating point can by a rounding error), and
a node is given a new picture through `scene_swap`, which does the same for
the box it had. A picture's size is kept beside its node (`scene_new`,
`pic_size_keep`, read with `scene_pic_size`): on a GLES2 screen the scene
lets go of the picture (`buffer` becomes NULL) once it is on the GPU, so
nothing may read the size off `buffer`.

### 4.3 A frame

`output_frame` runs once per output frame. On the main screen (or any
screen if that one has not had a frame for 12 ms) it calls
`output_frame_work`, which advances every animation by real time: folds,
sheet shadows, corners, resizing, throwing (or the shark), the drawer, the camera moves to
the views, seashells, the calendar and memo pad, the light at login, the lock,
the clock and the tray slips. Then the scene is committed. Nothing is drawn
on a frame unless something changed; an idle session commits nothing. The
bubbles, fish and crab are not animations in this sense: a timer of their
own (`bubbles_cb`) draws them a few times a second, and only while the tank
is in view and someone has touched the machine in the last two minutes.

### 4.4 Input

Pointer events are routed in a fixed order (`process_cursor_motion`,
`server_cursor_button`, `server_cursor_axis`):

1. the lock (only the key on the door);
2. something being carried (from the drawer, or a folded sheet);
3. a window being moved or resized, the clock's volume bar, the tray card;
4. on a screen other than the main one, windows only (§4.6);
5. the open view: the treasure chest, the Control Panel, the bookcase
   (seashells, book, calendar, memo pad);
6. the tank floor itself, when no window covers the point: clock, slips,
   the ALL APPS plate, the Control Panel and chest plates, letters lying on
   the sand, the drawer handle;
7. windows: the turned-down corner, the frame's edges and heading, then the
   client.

Keyboard shortcuts are handled in `handle_keybinding`. Typing into
AquaDE's own text fields (the book's search line, calendar notes, the memo
pad) and into programs goes through the input method: AquaDE implements
`text-input-v3` and bridges it to IBus itself (the section "typing into
programs' windows, through the input method"), so candidates appear under
the text.

### 4.5 Windows

`struct desk_toplevel` represents a window from either xdg-shell or
Xwayland. A window asks for server-side decoration (xdg-decoration or KDE's
server-decoration) and gets AquaDE's sheet frame; one that draws its own
frame is laid on a sheet anyway unless `paper_for_all_windows = no`. The
top-right corner is the only control: Fold (minimise into a folded sheet),
Unfold or Refold (maximise and restore), Discard (close, crumpled, then
eaten by the shark or thrown). Closing always asks the client first.

### 4.6 More than one screen

The tank is drawn on one screen: the one at layout position 0,0
(`first_output`). Every other screen gets more of the floor
(`side_desks_paint`, `desk_draw_side_desk`): a band of the sand cut from the
traced tank and laid with mirrored repeats so it continues across the
border. Windows can be
placed there. Maximise and fullscreen use the screen the window is on; new
windows open on the screen the pointer is on. Nothing done on the main
screen (the drawer, the views) moves windows on the others. When a screen is
removed, its windows move to the nearest remaining screen
(`windows_onto_screens`). The arrangement a display tool sets is remembered
per set of screens in `displays.conf`.

### 4.7 Protocols

wlroots globals created: compositor and subcompositor, xdg-shell,
xdg-decoration and KDE server-decoration, xdg-output, xdg-activation, seat
with data-device and primary selection, data-control, text-input-v3,
viewporter, presentation-time, single-pixel-buffer, linux-dmabuf (with a
GPU renderer), screencopy (for grim), idle-notify and idle-inhibit, output-management and
output-power-management, pointer-constraints and relative-pointer (games).
Not yet provided: layer-shell, session-lock, gamma-control, input-method-v2.

### 4.8 D-Bus

With libsystemd (`-Dpost=enabled`), AquaDE uses sd-bus on the event loop's
thread:

- `org.freedesktop.Notifications` server (`post.c`): letters in the treasure chest.
- StatusNotifierWatcher (when no other is on the bus) and host (`tray.c`).
- MPRIS client (`media.c`): what is playing, media keys.
- IBus (its own bus) and fcitx5 (`ime.c`); logind for ending the session.

## 5. The drawing library (`src/draw.c`)

`draw.c` builds into the static library `aquade-draw` with `apps.c`,
`panel.c`, `agenda.c`, `fonts.c`, `i18n.c`, `memo.c`, `user.c` and (with
EGL) `gpu.c`. `include/desk.h` is its API. The compositor, Shell Settings,
the Experience Index and the preview all link it, so a thing looks the same
wherever it is drawn.

### 5.1 The tank

The tank ("the room" in the code) is a **signed distance field** (`scene_map` in `draw.c`) marched
once per sample. A sample goes through the march, the normal, materials,
bump and wear, ambient occlusion, a soft shadow from the one area light,
spherical-harmonic ambient light, one bounce of reflection and a clear coat,
and the samples are resolved with coverage, an owner vote (which object a
pixel belongs to, for hit testing) and a glow pass. The water is put on at
the end, in `room_resolve`, shared by both tracers: a blue-green tint and
haze (`WATER_TINT`, `WATER_HAZE`).

It is traced as **layers** (sheets): the tank without the drawer, the drawer
alone (pulled out), the bookcase seen from the floor, the Control Panel, and
the treasure chest. Sliding the drawer sheet over the tank sheet is what lets the
drawer open at full frame rate. The camera moves to each view are traced as
short films.

There are two tracers:

- **C** (`draw.c`): the reference, run on all CPU cores in tiles.
- **GLSL** (`room.glsl` via `gpu.c`): a port of the same code, run in an
  offscreen GLES 3.0 context on the render node the screen uses. Every
  named constant is passed from `draw.c` to the shader (`room_glsl_preamble`),
  so the two cannot disagree about geometry. A driver is first tried in a
  child process (`aquade-gpu-trial`) so a crashing driver cannot take the session
  down; if it fails, the CPU traces.

`aquade-gputest --room` compares the two tracers sheet by sheet.

### 5.2 Levels and the cache

| Level | What it does |
| --- | --- |
| Ultra | Finer trace, wider on a GPU; nothing drawn coarser while moving. |
| High | As designed (default). |
| Medium, Low | Smaller trace and overlays; sheets in motion drawn coarser. |
| 2D Fallback | Traced once, small, one sample a pixel (softened); no shadows, fades, blur or animation, and no bubbles or fish. Chosen automatically when the screen is rendered on the CPU. |

Traced sheets are kept in `$XDG_CACHE_HOME/aquade` as PNGs named by sheet,
size, tank width, level and `ROOM_KEY`. `ROOM_KEY` is a hash of
`room.glsl` and `gpu.c` made at build time (`scripts/room-key.py`), so a
change to the tank's code invalidates old sheets while ordinary updates keep
them.

### 5.3 Flat drawing

Everything else is cairo: window frames and sheets, folding (`desk_fold_*`),
crumpling, the book and seashells, letters and the bottles at the chest, the
calendar and memo pad, Shell Settings' form (`desk_shell_form_*`), the
Control Panel's live parts and sonar, the clock's displays, the shark
(`desk_swim_*`), the bubbles and the fish and crabs (`desk_draw_life`), and
the watertight door (a second, small ray tracer for the door and the walk
in). Text goes through
`fonts.c` (a Latin face and a CJK face per role) and `wrap_lines` (word
breaking, CJK breaking and line-start rules).

## 6. Threads

| Thread | Owner | What it does |
| --- | --- | --- |
| Compositor | `wm.c` | The wlroots event loop: input, protocol, drawing sprites, D-Bus. |
| Room worker and tracers | `draw.c` | Read or trace the tank for the current screen and level; CPU tracing runs on every core in tiles; the GPU context lives on the room thread. |
| GPU trial | `draw.c` | A child process, not a thread: tries a driver before trusting it. |
| Door tracer | `draw.c` | The lock screen's scene, on every core when it is drawn. |
| App scanner | `apps.c` | Walks the application directories and publishes an immutable snapshot (`desk_apps_acquire`/`release`). |
| Icon jobs | `apps.c`, `tray.c` | Find and convert icons (SVG through rsvg-convert, else ImageMagick, when needed). |
| Texture warm-up | `draw.c` | Makes the sheet and wood tiles at start-up. |
| Mixer | `sound.c` | Mixes AquaDE's sounds and feeds libpulse or ALSA. |
| Volume | `panel.c` | Asks and sets the mixer through `wpctl`. |
| Weather | `occasion.c` | One `curl` (or `wget`) to open-meteo.com every half hour, when enabled. |
| Password check | `lock.c` | PAM, which sleeps after a wrong password. |

Shared state is guarded by mutexes (`g_room_lock`, `g_render_lock`, the app
list's reference count) or is atomic (the room's progress counters, its
shape numbers). Workers never call into wlroots or the scene. They publish
a result under a lock, with a serial or generation number (a new room, a
new snapshot of the app list), and the compositor picks it up on its own
thread at its next tick; or they wake the event loop through a pipe or
eventfd it watches (the weather, the tray's icons, the password check).
DEsk v0.1 was run under ThreadSanitizer, AddressSanitizer and
UndefinedBehaviorSanitizer (§9).

Signals are caught, never blocked: a handler writes one byte down a pipe the
event loop watches (`cad_signals`), and the loop acts on it. `aquade-ctl`'s
USR1 and USR2 reset the tank and end the session; TERM, HUP and INT (logind
ending the session, ^C on AquaDE in a window) end it through the same
teardown as logging out, so what is in hand (the memo, a calendar note) is
kept. A blocked signal would be handed down through exec to every program
AquaDE starts; a handler is not.

## 7. Files

| What | Where |
| --- | --- |
| Settings | `$XDG_CONFIG_HOME/aquade/shell.conf` (the machine's defaults in `/etc/xdg/aquade/shell.conf`) |
| Screen arrangements | `$XDG_CONFIG_HOME/aquade/displays.conf` |
| Drawer contents | `$XDG_CONFIG_HOME/aquade/drawer` |
| Shelf overrides | `$XDG_CONFIG_HOME/aquade/shelves.conf` |
| Default programs | `~/.config/aquade-mimeapps.list`, then the freedesktop.org lists |
| Calendar notes, memo pad | `$XDG_DATA_HOME/aquade/calendar`, `$XDG_DATA_HOME/aquade/memo` |
| Traced tank, icons, your picture | `$XDG_CACHE_HOME/aquade/` |
| Logs | `$XDG_STATE_HOME/aquade/session.log` (and `.old`), `install.log` |
| For Shell Settings | `$XDG_RUNTIME_DIR/<WAYLAND_DISPLAY>.render` (whether the screen is drawn on the CPU) |
| One session per user | `$XDG_RUNTIME_DIR/aquade.lock` (holds the PID; `aquade-ctl` reads it) |

Every file AquaDE writes is written to a temporary name and renamed into
place. Formats are in [CONFIGURATION.md](CONFIGURATION.md).

## 8. Languages, fonts, sound

- **Words**: `TR("English")` looks the English up in `WORDS` (`src/i18n.c`),
  a table with one column per language (`enum desk_lang`: en, ja, zh, ko,
  es, fr, de). A missing translation shows the English. Keys may carry a
  context, `"clock|DAY"`. `aquade-index` keeps its own table in `index.c`.
  `meson test` checks both tables (`scripts/check-words.py`).
- **Fonts**: each role (UI, print, display, engrave, type, screen, hand) has
  a Latin list and a CJK list; the CJK list is chosen again for Chinese,
  Japanese or Korean (Noto CJK SC, JP or KR), because shared characters are
  drawn differently in each.
- **Sound**: nothing is a recording. `sfx.c` synthesises every sound from
  modes, noise, crackle and creaks at start-up, then puts it under water (a
  low-pass in `finish()`, and bubbles, gravel and stone of its own);
  `sound.c` plays variants, panned to where the thing is on screen.

## 9. Building and checking

```sh
meson setup build && meson compile -C build
meson test -C build                       # the translation tables
./build/aquade-preview -o out.png --shelf   # draw without a compositor
./build/aquade-gputest --room               # the GPU and the C trace the same tank
./scripts/aquade-window                     # run nested in the current session
```

AquaDE's changes are checked with those four: the build with no warnings,
`meson test`, the preview of what changed (with `DESK_NO_GPU=1` for the CPU
tracer), `aquade-gputest --room` after any change to the tank, and a nested
run. For DEsk v0.1, before the fork, the following were run as well:

- four build configurations with no warnings: minimal
  (`-Dpost=disabled -Dsound=disabled -Dxwayland=disabled`), `-Dwerror=true`,
  `-D_FORTIFY_SOURCE=3`, and AddressSanitizer + UndefinedBehaviorSanitizer;
- a strict warning set (`-Wshadow -Wformat=2 -Wnull-dereference
  -Wduplicated-cond -Wlogical-op ...`) and GCC's `-fanalyzer` over every file;
- ThreadSanitizer on the compositor, the preview and the Experience Index;
- ASan/UBSan runs of the nested compositor, Shell Settings and the Experience Index,
  including with randomly malformed configuration, data and `.desktop`
  files;
- every view rendered in all seven languages.

`scripts/verify.sh` builds the Debian 13 image in the `Dockerfile` and runs
AquaDE in it headless (not yet tried since the fork).

## 10. What is stable

AquaDE has **no plugin API**. Everything in `include/` is internal and may
change between releases. The stable interfaces are the standard ones AquaDE
implements: Wayland protocols (§4.7), freedesktop.org notifications,
StatusNotifierItem, MPRIS, XDG autostart and `.desktop` files, and the files
in §7, whose keys it means to keep reading. Changes to AquaDE itself are
made by changing its source; [MODDING.md](MODDING.md) explains how.
