#!/bin/sh
# verify.sh -- AquaDE built in Docker (the Dockerfile at the top of the
# tree) and run there with no screen and no GPU; its pictures land in out/.
# With no GPU the room is traced on the CPU, so the run takes minutes.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
if [ -S "${HOME}/.colima/docker.sock" ]; then
	export DOCKER_HOST="unix://${HOME}/.colima/docker.sock"
fi
mkdir -p "$root/out"
docker build -t aquade "$root"
docker run --rm -v "$root/out:/out" aquade
echo "wrote:"
ls -l "$root/out"
