#!/bin/sh
# Build AppImage for any architecture
# Usage: build-appimage.sh <arch>

set -e

ARCH=${1:-x86_64}
APPIMAGE_NAME="InfixDemo-${ARCH}.AppImage"

echo "Building AppImage for ${ARCH}..."

# Create AppDir structure
mkdir -p AppDir/usr/bin
mkdir -p AppDir/usr/share/applications
mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps

# Copy binary
cp demo AppDir/usr/bin/

# Create desktop file
cat > AppDir/usr/share/applications/demo.desktop << 'EOF'
[Desktop Entry]
Type=Application
Name=Infix Demo
Exec=demo
Icon=demo
Categories=Game;
EOF

# Copy icon
cp jack.png AppDir/usr/share/icons/hicolor/256x256/apps/demo.png

# Create AppRun
cat > AppDir/AppRun << 'EOF'
#!/bin/sh
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
exec "${HERE}/usr/bin/demo" "$@"
EOF

chmod +x AppDir/AppRun

# Download AppImage runtime for the target architecture
RUNTIME_URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/runtime-${ARCH}"
echo "Downloading AppImage runtime for ${ARCH}..."
wget -q -O runtime "${RUNTIME_URL}"

# Create squashfs filesystem
echo "Creating squashfs filesystem..."
mksquashfs AppDir filesystem.squashfs -root-owned -noappend

# Combine runtime + squashfs to create proper AppImage
echo "Creating ${APPIMAGE_NAME}..."
cat runtime filesystem.squashfs > "${APPIMAGE_NAME}"
chmod +x "${APPIMAGE_NAME}"

# Cleanup
rm -rf AppDir runtime filesystem.squashfs

echo "AppImage created: ${APPIMAGE_NAME}"
