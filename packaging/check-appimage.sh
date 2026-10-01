#!/bin/bash
# Checks a built AppImage: it must be a real type-2 AppImage (not a script
# wrapper), and OpenSSL must sit only in usr/lib/fallback, so native plugins
# loaded into RigRoom get the host's newer OpenSSL.
# Usage: packaging/check-appimage.sh dist/RigRoom-X.Y.Z-x86_64.AppImage
set -euo pipefail

appimage="${1:?usage: $0 <file.AppImage>}"
fail() { echo "check-appimage: $*" >&2; exit 1; }

[ -f "$appimage" ] || fail "$appimage not found"
[ "$(head -c 4 "$appimage" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || fail "$appimage is not an ELF file"
[ "$(dd if="$appimage" bs=1 skip=8 count=3 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "414902" ] \
    || fail "$appimage has no AppImage type 2 magic"

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT
appimage_path="$(readlink -f "$appimage")"
# A downloaded release file has no execute bit; run a copy instead of
# changing the caller's file.
if [ ! -x "$appimage_path" ]; then
    cp "$appimage_path" "$workdir/check.AppImage"
    chmod +x "$workdir/check.AppImage"
    appimage_path="$workdir/check.AppImage"
fi
(cd "$workdir" && "$appimage_path" --appimage-extract >/dev/null)
root="$workdir/squashfs-root"
[ -x "$root/AppRun" ] || fail "no AppRun in $appimage"

bundled_openssl="$(find "$root/usr/lib" -maxdepth 1 \( -name 'libssl.so*' -o -name 'libcrypto.so*' \) -printf '%f ')"
[ -z "$bundled_openssl" ] || fail "OpenSSL in usr/lib (would shadow the host's for plugins): $bundled_openssl"

# The fallback libjack stub must define every JACK symbol RigRoom uses, or the
# AppImage won't start on systems without JACK/PipeWire.
used="$(nm -D --undefined-only "$root/usr/bin/RigRoom" | awk '{print $2}' | grep '^jack_' | sed 's/@.*//' | sort -u)"
stub="$(nm -D --defined-only "$root/usr/lib/fallback/libjack.so.0" | awk '{print $3}' | grep '^jack_' | sort -u)"
missing="$(comm -23 <(echo "$used") <(echo "$stub") | tr '\n' ' ')"
[ -z "${missing// }" ] || fail "fallback libjack lacks: $missing(add them to packaging/libjack_fallback.c)"

echo "check-appimage: $appimage OK"
