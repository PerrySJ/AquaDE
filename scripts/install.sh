#!/bin/sh
# install.sh -- AquaDE built and installed, with what it needs there already.
#
# For a Linux where everything AquaDE is built from is installed already
# (the README lists it): it builds AquaDE and installs it, and puts it on
# the login screen, the way the installers do -- into /usr/local, from the
# same build directory -- and installs nothing else. For a whole install
# on Debian 13, Ubuntu 24.04 and 26.04, Arch or Fedora -- what AquaDE needs, AquaDE,
# and the programs that go with it -- run scripts/install_session.sh
# instead; scripts/install-desktop.sh brings those programs elsewhere.
#
#   --prefix DIR   where AquaDE goes (default /usr/local; PREFIX=DIR as well)
#   --help         this
#
# Run it as yourself from the folder you got AquaDE in; it asks for sudo
# to install (run as root, it needs none). The build directory is
# build-install in the source folder (BUILD_DIR=... for another). When it
# is done, log out and pick "AquaDE" on the login screen.
set -eu

prefix="${PREFIX:-/usr/local}"
while [ $# -gt 0 ]; do
	case "$1" in
	--prefix)
		[ $# -ge 2 ] || { echo "install.sh: --prefix wants a folder" >&2; exit 2; }
		prefix=$2
		shift
		;;
	--prefix=*) prefix=${1#--prefix=} ;;
	-h|--help) sed -n '2,/^set /s/^# \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "install.sh: what is $1? (--help says what it takes)" >&2; exit 2 ;;
	esac
	shift
done
case "$prefix" in
/?*) prefix=${prefix%/} ;;
*) echo "install.sh: the prefix has to be a whole path, like /usr/local" >&2; exit 2 ;;
esac

root="$(cd "$(dirname "$0")/.." && pwd)"
build="${BUILD_DIR:-$root/build-install}"

say() { printf '%s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die() { printf 'install.sh: %s\n' "$*" >&2; exit 1; }

if ! grep -q "^project('aquade'" "$root/meson.build" 2>/dev/null; then
	die "$root does not look like AquaDE's source folder (no meson.build for it)"
fi
for tool in meson ninja pkg-config; do
	command -v "$tool" >/dev/null 2>&1 || die "there is no $tool here. Install what the README lists,
or run scripts/install_session.sh, which does that too on Debian, Ubuntu, Arch and Fedora."
done
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Root's work -- putting files under the prefix, /etc and /usr/share -- is
# done through sudo when we are not root already.
if [ "$(id -u)" -eq 0 ]; then
	as_root() { "$@"; }
else
	command -v sudo >/dev/null 2>&1 \
		|| die "installing needs root: run this as root, or install sudo and add yourself to it"
	as_root() { sudo "$@"; }
fi

# The build is done as whoever owns the source folder, even when this is
# run as root: a build directory owned by root in somebody's clone is a
# nuisance ever after.
builder=
if [ "$(id -u)" -eq 0 ]; then
	owner=$(stat -c %u "$root")
	if [ "$owner" -ne 0 ] && id -nu "$owner" >/dev/null 2>&1; then
		builder=$(id -nu "$owner")
	fi
fi
as_builder() {
	if [ -n "$builder" ]; then
		runuser -u "$builder" -- "$@"
	else
		"$@"
	fi
}

say "AquaDE, built and installed"
say
say "  source    $root"
say "  build     $build"
say "  prefix    $prefix"

# -- 1. build ---------------------------------------------------------------
step "Building AquaDE${builder:+ (as $builder, who owns the source)}"
setup() {
	as_builder meson setup "$@" --prefix="$prefix" --buildtype=release "$build" "$root" \
		> "$tmp/setup.log" 2>&1
}
nosetup() {
	cat "$tmp/setup.log"
	die "Meson could not set up $build. What it is missing is said above; the README
lists what AquaDE is built from, and scripts/install_session.sh installs it on Debian,
Ubuntu, Arch and Fedora."
}
# A directory an installer set up keeps where it found wlroots (one built
# into /opt/aquade-wlroots, say): only the prefix is changed.
if [ -f "$build/meson-private/coredata.dat" ]; then
	# set up before: the prefix may have changed, or Meson itself, and a
	# directory a different Meson set up cannot be reconfigured, only wiped
	setup --reconfigure || setup --wipe || nosetup
else
	setup || nosetup
fi
# what Meson found, from its summary
sed -n '/^aquade [0-9]/,/^Found ninja/{/^Found ninja/d;p}' "$tmp/setup.log"
as_builder ninja -C "$build" || die "the build failed; what went wrong is above"
[ -x "$build/aquade" ] || die "the build left no aquade program in $build"

# -- 2. install -------------------------------------------------------------
step "Installing into $prefix"
as_root meson install -C "$build" --no-rebuild --quiet \
	|| die "installing into $prefix failed (above)"
# Meson's note of what it installed belongs with the rest of the build
as_root chown --reference="$build" "$build/meson-logs/install-log.txt" 2>/dev/null || true
data="$prefix/share"
for f in "$prefix/bin/aquade" "$prefix/bin/aquade-session" /etc/pam.d/aquade; do
	[ -e "$f" ] || die "$f is not there after installing"
done
say "  $prefix/bin/aquade and the programs that go with it (aquade-session, aquade-settings...)"
say "  /etc/pam.d/aquade (the screen lock checks the password as login does)"

# The login screen, as the installers have it. GDM, SDDM and LightDM all
# read /usr/share/wayland-sessions, and LightDM nothing else, so the entry
# goes there, saying where aquade-session is. It stays in one place only: a
# login screen that reads both would list AquaDE twice.
sessions=/usr/share/wayland-sessions
if [ "$data" != /usr/share ]; then
	sed -e "s|^Exec=aquade-session|Exec=$prefix/bin/aquade-session|" \
		-e "s|^TryExec=aquade-session|TryExec=$prefix/bin/aquade-session|" \
		"$data/wayland-sessions/aquade.desktop" > "$tmp/aquade.desktop"
	as_root install -D -m 644 "$tmp/aquade.desktop" "$sessions/aquade.desktop"
	as_root rm -f "$data/wayland-sessions/aquade.desktop"
	as_root rmdir "$data/wayland-sessions" 2>/dev/null || true
fi
say "  $sessions/aquade.desktop (\"AquaDE\" on the login screen)"

# The portals look for aquade-portals.conf -- which portal does what on this
# desktop -- in /etc, and in XDG_DATA_DIRS, which is /usr/local/share and
# /usr/share when nothing says otherwise. D-Bus starts them in systemd's
# user environment, where XDG_DATA_DIRS is unset or keeps those two: the
# prefix's own copy is found for /usr and /usr/local, and any other prefix
# gets one in /usr/share.
case "$prefix" in
/usr|/usr/local) ;;
*) as_root install -D -m 644 "$data/xdg-desktop-portal/aquade-portals.conf" \
		/usr/share/xdg-desktop-portal/aquade-portals.conf
	say "  /usr/share/xdg-desktop-portal/aquade-portals.conf (which portal does what)"
	;;
esac

# A cache already there has to hear about what is new, or it is not found;
# without one, things are looked for as they are, and none is made.
on_path() { command -v "$1" >/dev/null 2>&1; }
if [ -f "$data/icons/hicolor/icon-theme.cache" ] && on_path gtk-update-icon-cache; then
	as_root gtk-update-icon-cache -q -f "$data/icons/hicolor" || true
fi
if [ -f "$data/applications/mimeinfo.cache" ] && on_path update-desktop-database; then
	as_root update-desktop-database -q "$data/applications" || true
fi

# This used to put AquaDE in /usr, and the installers put it in /usr/local:
# with one in each, the one first on the PATH is the one that runs
for other in /usr /usr/local; do
	if [ "$other" != "$prefix" ] && [ -x "$other/bin/aquade" ]; then
		say
		say "Note: there is another AquaDE in $other ($other/bin/aquade). With two, the"
		say "      one that runs is the one first on the PATH (usually /usr/local's)."
		say "      \"sudo ninja uninstall\" in the build it came from takes it away."
	fi
done

# aquade-session puts its own folder on the PATH and the prefix's share first
# in XDG_DATA_DIRS, so a AquaDE session finds everything wherever it is;
# other sessions find the prefix's share only if XDG_DATA_DIRS names it
case "$prefix" in
/usr|/usr/local) ;;
*) say
	say "Note: a AquaDE session finds what is in $prefix/share by itself. For other"
	say "      sessions to list AquaDE's programs (Shell Settings, the Experience"
	say "      Index) too, add it to XDG_DATA_DIRS -- in /etc/environment, say:"
	say "        XDG_DATA_DIRS=$prefix/share:/usr/local/share:/usr/share"
	;;
esac

step "Done"
say "AquaDE is installed in $prefix."
say "Log out, pick \"AquaDE\" on the login screen (the session menu: often a gear"
say "by the password box, or a list at the bottom or top of the screen), and log in."
say "Without a login screen, log in on the console and type"
case "$prefix" in
/usr|/usr/local) say "  aquade-session" ;;
*) say "  $prefix/bin/aquade-session" ;;
esac
