# AquaDE built and tried in Debian 13, with no screen and no GPU:
# scripts/verify.sh builds this and runs it, and what runs is
# scripts/entrypoint.sh (headless, pixman), which writes its pictures to /out.
#
# The build stage installs what AquaDE needs by running install_debian.sh
# itself (--bare, into /usr; as root it needs no sudo), so the two cannot
# drift apart. The image that runs gets AquaDE and the packages its
# programs' libraries come from, as the build found them.
FROM debian:trixie-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

WORKDIR /src
# the whole tree, but for what .dockerignore leaves out (the build
# directories, out, .git and salvage)
COPY . /src

RUN sh scripts/install_debian.sh --yes --bare --prefix /usr \
	&& meson install -C build-install --no-rebuild --quiet --destdir /dest \
	&& rm -rf /var/lib/apt/lists/*

# the packages the libraries of the installed programs come from (by the
# path ldd found, else the file it points to: the libraries are in /usr/lib
# and found through /lib as well)
RUN for f in /dest/usr/bin/aquade*; do \
		ldd "$f" 2>/dev/null | awk '$2 == "=>" && $3 ~ /^\// { print $3 }'; \
	done | sort -u | while read -r lib; do \
		dpkg -S "$lib" 2>/dev/null || dpkg -S "$(readlink -f "$lib")" 2>/dev/null || true; \
	done | grep -v '^diversion ' | sed -n 's/^\([^:,]*\)[:,].*/\1/p' | sort -u > /runtime-packages \
	&& grep -qx 'libwlroots-0.18' /runtime-packages \
	&& tr '\n' ' ' < /runtime-packages && echo

FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive \
	XDG_RUNTIME_DIR=/tmp/xdg \
	WLR_RENDERER=pixman \
	WLR_LIBINPUT_NO_DEVICES=1 \
	XDG_SESSION_TYPE=wayland \
	XDG_SESSION_DESKTOP=AquaDE \
	XDG_CURRENT_DESKTOP=AquaDE \
	DESKTOP_SESSION=AquaDE

# what the programs are linked with; the typefaces install_debian.sh puts
# in, and DejaVu for foot's monospace; grim, which --screenshot uses; and
# foot, a Wayland program to put on the desk
COPY --from=build /runtime-packages /tmp/runtime-packages
RUN apt-get update && apt-get install -y --no-install-recommends \
		$(cat /tmp/runtime-packages) \
		fonts-noto-core fonts-noto-cjk fonts-noto-color-emoji fonts-urw-base35 fonts-klee \
		fonts-dejavu-core grim foot \
	&& rm -rf /var/lib/apt/lists/* /tmp/runtime-packages

COPY --from=build /dest/usr /usr
COPY --from=build /dest/etc /etc
COPY --from=build /dest /opt/aquade-destdir
COPY scripts/entrypoint.sh /usr/local/bin/aquade-verify
RUN chmod +x /usr/local/bin/aquade-verify

WORKDIR /out
VOLUME ["/out"]
CMD ["/usr/local/bin/aquade-verify"]
