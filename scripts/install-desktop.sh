#!/bin/sh
# install-desktop.sh -- the free programs that make AquaDE a whole desktop.
#
# AquaDE draws the desk; what you do on it is done with free software that
# already exists, chosen to go with it: the classic GTK programs Xfce and
# MATE are made of. They are plain windows -- a menu bar, a toolbar, the
# work -- that ask AquaDE for their frame, so each lies on the desk as one
# sheet of paper; a program that draws its own title bar into the window,
# as GNOME's newer ones do, puts a second frame on the sheet. They run on
# Wayland as they are, are light, and follow AquaDE's default-applications
# list. (The few tools with no classic counterpart -- disks, displays,
# sound levels -- draw their own.) The usual free parts of a Linux desktop
# do the rest: networks, Bluetooth, sound, printing, passwords, the
# portals, the input method, typefaces; and Qt programs are dressed as the
# GTK ones. See the README's "As a desktop" for what each is for.
#
#   scripts/install-desktop.sh            show what it would install, and ask
#   scripts/install-desktop.sh --extras   and the office suite, mail, a scanner...
#   scripts/install-desktop.sh --dry-run  only show
#   scripts/install-desktop.sh --yes      do not ask
#
# apt (Debian, Ubuntu), dnf (Fedora), pacman (Arch) and zypper (openSUSE):
# each thing below is a list of names it goes by; one of them installed
# already will do, else the first one the package manager knows is the one
# installed. Nothing is removed. The Debian, Ubuntu, Arch and Fedora names
# are the ones scripts/install_debian.sh, install_ubuntu.sh, install_arch.sh
# and install_fedora.sh install, and were tried there; the openSUSE ones are
# what that distribution is known to call them, and were not. On Debian 13,
# Ubuntu, Arch and Fedora, the same programs come with AquaDE itself from
# scripts/install_session.sh.
set -eu

extras=0 dry=0 yes=0
for a in "$@"; do
	case "$a" in
	--extras) extras=1 ;;
	--dry-run) dry=1 ;;
	--yes|-y) yes=1 ;;
	-h|--help) sed -n '2,/^set /s/^# \{0,1\}//p' "$0"; exit 0 ;;
	*) echo "install-desktop.sh: what is $a? (--help says what it takes)" >&2; exit 2 ;;
	esac
done

# group | what it is for | names, one installed counting, else the first
# known one [| for which package managers only: elsewhere it is part of
# something else -- Arch's gnome-keyring has its PAM module, and Fedora's
# and Arch's Qt its GTK theme -- or not needed (pacman alone stops to ask
# which JACK)]
LIST='
core|terminal (Xfce Terminal)|xfce4-terminal
core|files (Thunar)|thunar Thunar
core|files: disks, trash and network places|gvfs
core|files: disks and memory sticks|udisks2
core|files: memory sticks opened when put in|thunar-volman
core|files: thumbnails|tumbler
core|text editor (Mousepad)|mousepad
core|pictures (Ristretto)|ristretto
core|PDF and documents (Atril)|atril
core|archives (Engrampa)|engrampa
core|archives: zip|zip
core|archives: unzip|unzip
core|music and video (Parole)|parole
core|web browser (Firefox)|firefox-esr firefox MozillaFirefox
core|asking for passwords (MATE polkit agent)|mate-polkit
core|keeping passwords (keyring)|gnome-keyring
core|keyring: unlocked at login|libpam-gnome-keyring gnome-keyring-pam|apt dnf zypper
core|networks (NetworkManager)|network-manager NetworkManager networkmanager
core|networks: the applet in the tray|network-manager-applet network-manager-gnome NetworkManager-applet
core|networks: connection editor|nm-connection-editor network-manager-gnome NetworkManager-connection-editor
core|Bluetooth (Blueman)|blueman
core|memory sticks: mounted, with a letter (udiskie)|udiskie
core|tray slips for udiskie and other indicators|gir1.2-ayatanaappindicator3-0.1 libayatana-appindicator libayatana-appindicator-gtk3 typelib-1_0-AyatanaAppIndicator3-0_1
core|a letter when the battery runs low|poweralertd batsignal
core|a letter when there are updates|package-update-indicator|apt
core|sound (PipeWire, or the PulseAudio you have)|pipewire-pulse pipewire-pulseaudio pulseaudio
core|sound: session manager|wireplumber
core|sound: JACK programs, through PipeWire|pipewire-jack jack2|pacman
core|sound: devices and levels|pavucontrol
core|printers|system-config-printer
core|printing service|cups
core|disks (GNOME Disks)|gnome-disk-utility
core|disk usage (QDirStat)|qdirstat baobab
core|colour profiles: the service|colord
core|date, time and users (from LXQt)|lxqt-admin
core|your name and picture (Mugshot)|mugshot
core|passwords and keys (Seahorse)|seahorse
core|adding and removing programs|gnome-packagekit
core|backups (Back In Time)|backintime-qt deja-dup
core|full-colour icons (Back In Time wants them)|adwaita-icon-theme-legacy yaru-theme-icon
core|what the computer is doing|xfce4-taskmanager
core|about this computer|hardinfo2 cpu-x
core|about this computer: the graphics part|mesa-utils glx-utils Mesa-demo-x
core|Qt 6 programs on Wayland|qt6-wayland qt6-qtwayland libQt6WaylandClient6
core|Qt 5 programs on Wayland|qtwayland5 qt5-wayland qt5-qtwayland libqt5-qtwayland
core|displays (wdisplays)|wdisplays
core|default programs (Xfce MIME settings)|xfce4-settings
core|theme, icons and cursor (nwg-look; GNOME Tweaks on Fedora)|nwg-look gnome-tweaks
core|Qt 6 programs dressed as the GTK ones|qt6-gtk-platformtheme qt6-qtbase-gui qt6-platformtheme-gtk3|apt dnf zypper
core|Qt 5 programs dressed as the GTK ones|qt5-gtk-platformtheme qt5-qtbase-gui libqt5-qtbase-platformtheme-gtk3|apt dnf zypper
core|portals (file choosers, screen sharing)|xdg-desktop-portal
core|portals: screenshots and screen sharing|xdg-desktop-portal-wlr
core|portals: GTK file chooser|xdg-desktop-portal-gtk
core|fonts: Latin and more (Noto)|fonts-noto-core google-noto-sans-fonts noto-fonts noto-sans-fonts
core|fonts: Chinese, Japanese, Korean (Noto CJK)|fonts-noto-cjk google-noto-sans-cjk-fonts noto-fonts-cjk noto-sans-cjk-fonts
core|fonts: emoji|fonts-noto-color-emoji google-noto-color-emoji-fonts noto-fonts-emoji noto-coloremoji-fonts
core|fonts: for the desk itself (URW base 35)|fonts-urw-base35 urw-base35-fonts gsfonts
core|fonts: handwriting on the memo pad (Klee One)|fonts-klee|apt
core|home folders (Documents, Pictures...)|xdg-user-dirs
core|opening files and links|xdg-utils
core|the weather at the front door (curl)|curl
core|X11 programs (Xwayland)|xwayland xorg-x11-server-Xwayland xorg-xwayland
core|clipboard on the command line|wl-clipboard
core|screenshots|grim
core|screenshots: a part of the screen|slurp
core|notifications from scripts|libnotify-bin libnotify-tools libnotify
core|session bus per login|dbus-user-session|apt
extras|office: documents (LibreOffice Writer)|libreoffice-writer libreoffice-fresh
extras|office: spreadsheets (LibreOffice Calc)|libreoffice-calc libreoffice-fresh
extras|office: presentations (LibreOffice Impress)|libreoffice-impress libreoffice-fresh
extras|office: drawn with GTK, like the rest|libreoffice-gtk3|apt dnf zypper
extras|mail and calendar (Thunderbird)|thunderbird MozillaThunderbird
extras|calculator (Galculator)|galculator
extras|what the computer is doing (Xfce Task Manager)|xfce4-taskmanager
extras|scanner (Simple Scan)|simple-scan
'

# and for a Japanese desktop: the Japanese converter -- IBus, which AquaDE
# hands the keys to itself, with Mozc, else Anthy (Arch keeps Mozc in the
# AUR) -- and the office suite's Japanese. As in the installers, someone
# who has fcitx5 has chosen it, and aquade-session-helper would pick IBus
# over it once that is installed.
case "${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}" in
ja*)
	if ! command -v fcitx5 >/dev/null 2>&1; then
		LIST="$LIST
core|input method (IBus)|ibus
core|input method: Japanese (Mozc, else Anthy)|ibus-mozc ibus-anthy
"
	fi
	LIST="$LIST
extras|office: Japanese|libreoffice-l10n-ja libreoffice-langpack-ja libreoffice-fresh-ja
"
	;;
esac

if command -v apt-get >/dev/null 2>&1; then
	pm=apt
	# (a name apt has only heard of has nothing to install, though apt-cache
	# does not count it a miss)
	known() { apt-cache show --no-all-versions "$1" 2>/dev/null | grep -q '^Package:'; }
	have() { dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q "install ok installed"; }
	install() { sudo apt-get install --no-install-recommends "$@"; }
elif command -v dnf >/dev/null 2>&1; then
	pm=dnf
	known() { dnf -q info "$1" >/dev/null 2>&1; }
	have() { rpm -q "$1" >/dev/null 2>&1; }
	install() { sudo dnf install "$@"; }
elif command -v pacman >/dev/null 2>&1; then
	pm=pacman
	known() { pacman -Si "$1" >/dev/null 2>&1 || pacman -Qi "$1" >/dev/null 2>&1; }
	# (installed, or something that stands in for it, as install_arch.sh has it)
	have() { pacman -T "$1" >/dev/null 2>&1; }
	install() { sudo pacman -S --needed "$@"; }
elif command -v zypper >/dev/null 2>&1; then
	pm=zypper
	known() { zypper -q info "$1" 2>/dev/null | grep -q "^Name"; }
	have() { rpm -q "$1" >/dev/null 2>&1; }
	install() { sudo zypper install "$@"; }
else
	echo "install-desktop.sh: no apt, dnf, pacman or zypper here; the list is in this script" >&2
	exit 1
fi

list=$(mktemp)
trap 'rm -f "$list"' EXIT
echo "AquaDE desktop: what goes with it ($pm)"
echo
printf '%s\n' "$LIST" | while IFS='|' read -r group what names only; do
	[ -n "$group" ] || continue
	[ "$group" = extras ] && [ "$extras" -eq 0 ] && continue
	if [ -n "$only" ]; then
		case " $only " in
		*" $pm "*) ;;
		*) continue ;;
		esac
	fi
	pick=""
	for n in $names; do
		if have "$n"; then pick=$n; break; fi
	done
	if [ -n "$pick" ]; then
		printf '  %-50s %s\n' "$what" "$pick (installed)"
		continue
	fi
	for n in $names; do
		if known "$n"; then pick=$n; break; fi
	done
	if [ -z "$pick" ]; then
		printf '  %-50s %s\n' "$what" "(not to be had here)"
	else
		printf '  %-50s %s\n' "$what" "$pick"
		grep -qx "$pick" "$list" || echo "$pick" >> "$list"
	fi
done
echo
if [ ! -s "$list" ]; then
	echo "Everything is installed already."
	exit 0
fi
pkgs=$(tr '\n' ' ' < "$list")
echo "To install: $pkgs"
if [ "$dry" -eq 1 ]; then
	exit 0
fi
if [ "$yes" -eq 0 ]; then
	printf 'Install them? [y/N] '
	read -r answer
	case "$answer" in
	y|Y|yes) ;;
	*) echo "Nothing installed."; exit 0 ;;
	esac
fi
# shellcheck disable=SC2086
install $pkgs
