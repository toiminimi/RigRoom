#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
APP_DIR="${ROOT_DIR}/build/AppDir"

echo "=========================================="
echo " 🎛️  Building PedalBoard AppImage"
echo "=========================================="

# 1. Compile PedalBoard in Release mode
echo "--> Compiling PedalBoard binary..."
cmake -B "${BUILD_DIR}" -S "${ROOT_DIR}" -DCMAKE_BUILD_TYPE=Release

# Calculate safe parallel jobs based on available RAM (allocating ~2GB per job) to prevent OOM system lockups
NPROC=$(nproc)
TOTAL_RAM_GB=$(free -g | awk '/^Mem:/{print $2}')
if [ -z "${TOTAL_RAM_GB}" ] || [ "${TOTAL_RAM_GB}" -lt 1 ]; then TOTAL_RAM_GB=4; fi
MAX_JOBS=$(( TOTAL_RAM_GB / 2 ))
if [ "$MAX_JOBS" -lt 1 ]; then MAX_JOBS=1; fi
JOBS=$NPROC
if [ "$JOBS" -gt "$MAX_JOBS" ]; then JOBS=$MAX_JOBS; fi
echo "--> Building with ${JOBS} parallel job(s) (RAM: ${TOTAL_RAM_GB}GB, Cores: ${NPROC})..."

cmake --build "${BUILD_DIR}" --config Release -j"${JOBS}"

# 2. Setup AppDir structure
echo "--> Preparing AppDir structure..."
rm -rf "${APP_DIR}"
mkdir -p "${APP_DIR}/usr/bin"
mkdir -p "${APP_DIR}/usr/lib"
mkdir -p "${APP_DIR}/usr/plugins/platforms"
mkdir -p "${APP_DIR}/usr/share/applications"

# Copy binary & launcher
cp "${BUILD_DIR}/PedalBoard" "${APP_DIR}/usr/bin/PedalBoard"
cp "${SCRIPT_DIR}/AppRun" "${APP_DIR}/AppRun"
chmod +x "${APP_DIR}/AppRun"

# Copy Qt platform plugins if available on host system
QT_PLUGINS_DIR=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || qmake -query QT_INSTALL_PLUGINS 2>/dev/null || echo "/usr/lib64/qt6/plugins")
if [ -d "${QT_PLUGINS_DIR}/platforms" ]; then
    cp -r "${QT_PLUGINS_DIR}/platforms" "${APP_DIR}/usr/plugins/" 2>/dev/null || true
fi

# Copy desktop file & app icon
cp "${SCRIPT_DIR}/org.pedalboard.PedalBoard.desktop" "${APP_DIR}/org.pedalboard.PedalBoard.desktop"
cp "${SCRIPT_DIR}/org.pedalboard.PedalBoard.desktop" "${APP_DIR}/usr/share/applications/org.pedalboard.PedalBoard.desktop"
if [ -f "${SCRIPT_DIR}/org.pedalboard.PedalBoard.png" ]; then
    cp "${SCRIPT_DIR}/org.pedalboard.PedalBoard.png" "${APP_DIR}/org.pedalboard.PedalBoard.png"
    mkdir -p "${APP_DIR}/usr/share/icons/hicolor/256x256/apps"
    cp "${SCRIPT_DIR}/org.pedalboard.PedalBoard.png" "${APP_DIR}/usr/share/icons/hicolor/256x256/apps/org.pedalboard.PedalBoard.png"
    ln -sf "org.pedalboard.PedalBoard.png" "${APP_DIR}/.DirIcon"
fi

# 3. Fetch appimagetool if not available
APPIMAGETOOL="${ROOT_DIR}/build/appimagetool-x86_64.AppImage"
if [ ! -f "${APPIMAGETOOL}" ]; then
    echo "--> Downloading appimagetool..."
    curl -sL "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage" -o "${APPIMAGETOOL}"
    chmod +x "${APPIMAGETOOL}"
fi

# 4. Generate AppImage
OUTPUT_APPIMAGE="${ROOT_DIR}/PedalBoard-0.9.0-x86_64.AppImage"
echo "--> Generating ${OUTPUT_APPIMAGE}..."
export APPIMAGE_EXTRACT_AND_RUN=1
ARCH=x86_64 "${APPIMAGETOOL}" "${APP_DIR}" "${OUTPUT_APPIMAGE}"

echo "=========================================="
echo " ✅ AppImage Created Successfully!"
echo " Output: ${OUTPUT_APPIMAGE}"
echo " Run with: ./PedalBoard-0.9.0-x86_64.AppImage"
echo "=========================================="
