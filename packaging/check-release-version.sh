#!/bin/bash
# Verify that a release tag matches the version recorded in the tree.
#
# The AppImage takes its version from CMakeLists.txt, never from the tag, so a
# forgotten version bump would publish an artifact named after the previous
# release without anything failing. Run this before tagging; release.yml also
# runs it before building.
#
#   ./packaging/check-release-version.sh v0.12.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

CMAKELISTS="${ROOT_DIR}/CMakeLists.txt"
APPDATA="${SCRIPT_DIR}/org.rigroom.RigRoom.appdata.xml"
CHANGELOG="${ROOT_DIR}/CHANGELOG.md"

fail() {
    echo "ERROR: $1" >&2
    exit 1
}

if [ $# -ne 1 ]; then
    echo "Usage: $(basename "$0") vX.Y.Z" >&2
    exit 1
fi

TAG="$1"
if [[ ! "${TAG}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)$ ]]; then
    fail "tag '${TAG}' is not of the form vX.Y.Z."
fi
VERSION="${BASH_REMATCH[1]}"
echo "--> Checking the tree against ${TAG} (version ${VERSION})"

# 1. CMakeLists.txt drives the binary version and the AppImage file name.
CMAKE_VERSION=$(sed -n 's/^project(RigRoom VERSION \([0-9.]*\).*/\1/p' "${CMAKELISTS}")
[ -n "${CMAKE_VERSION}" ] || fail "could not read the version from ${CMAKELISTS}."
[ "${CMAKE_VERSION}" = "${VERSION}" ] || fail \
    "CMakeLists.txt says ${CMAKE_VERSION}, the tag says ${VERSION}. Bump project(RigRoom VERSION ...) first."
echo "    CMakeLists.txt      ${CMAKE_VERSION}"

# 2. The newest <release> entry feeds software centres and update checks.
APPDATA_VERSION=$(sed -n 's/.*<release version="\([0-9.]*\)".*/\1/p' "${APPDATA}" | head -1)
[ -n "${APPDATA_VERSION}" ] || fail "could not read a <release> entry from ${APPDATA}."
[ "${APPDATA_VERSION}" = "${VERSION}" ] || fail \
    "the newest <release> in the appdata is ${APPDATA_VERSION}, the tag says ${VERSION}. Add an entry, newest first."
echo "    appdata.xml         ${APPDATA_VERSION}"

# 3. The changelog section has to be closed before tagging, not after.
CHANGELOG_HEADING=$(grep -m1 '^## ' "${CHANGELOG}" | sed 's/^## //')
[ -n "${CHANGELOG_HEADING}" ] || fail "could not find a section heading in ${CHANGELOG}."
if [[ ! "${CHANGELOG_HEADING}" =~ ^${VERSION}\ -\ [0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
    fail "the top CHANGELOG.md section is '${CHANGELOG_HEADING}', expected '${VERSION} - YYYY-MM-DD'. Close the Unreleased section first."
fi
echo "    CHANGELOG.md        ${CHANGELOG_HEADING}"

# 4. appimagetool runs with NO_APPSTREAM=1, so validate here when we can.
# The AppImage builder image does not ship appstreamcli; skip it there.
if command -v appstreamcli >/dev/null 2>&1; then
    if appstreamcli validate "${APPDATA}" >/dev/null 2>&1; then
        echo "    appdata validation  ok"
    else
        echo "WARNING: appstreamcli reported problems in ${APPDATA}:" >&2
        appstreamcli validate "${APPDATA}" >&2 || true
    fi
fi

echo "--> ${TAG} is consistent with the tree."
