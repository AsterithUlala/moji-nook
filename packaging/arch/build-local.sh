#!/usr/bin/env bash
# Build the Arch package from the committed HEAD instead of a published release.
set -euo pipefail
cd "$(dirname "$0")"
tag=$(sed -n 's/^_tag=//p' PKGBUILD)
git -C ../.. archive --format=tar.gz --prefix="moji-nook-$tag/" -o "$PWD/moji-nook-$tag.tar.gz" HEAD
makepkg -f "$@"
