#!/bin/bash
# ─────────────────────────────────────────────
# SwitchStream — DevkitPro Installation Script
# Run this with: sudo bash install_devkitpro.sh
# ─────────────────────────────────────────────

set -e

echo "╔══════════════════════════════════════════╗"
echo "║  SwitchStream — DevkitPro Installer      ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Step 1: Install prerequisites
echo "[1/5] Installing prerequisites..."
apt-get update -qq
apt-get install -y apt-transport-https gnupg2

# Step 2: Add devkitPro GPG key
echo "[2/5] Adding devkitPro GPG key..."
mkdir -p /usr/local/share/keyring/
curl -fsSL https://apt.devkitpro.org/devkitpro-pub.gpg -o /usr/local/share/keyring/devkitpro-pub.gpg

# Step 3: Add devkitPro apt repository
echo "[3/5] Adding devkitPro repository..."
echo "deb [signed-by=/usr/local/share/keyring/devkitpro-pub.gpg] https://apt.devkitpro.org stable main" \
    > /etc/apt/sources.list.d/devkitpro.list
apt-get update -qq

# Step 4: Install devkitpro-pacman
echo "[4/5] Installing devkitpro-pacman..."
apt-get install -y devkitpro-pacman

# Step 5: Install Switch development packages
echo "[5/5] Installing Switch development packages..."
dkp-pacman -Sy --noconfirm
dkp-pacman -S --noconfirm \
    switch-dev \
    switch-sdl2 \
    switch-sdl2_ttf \
    switch-sdl2_image \
    switch-sdl2_gfx \
    switch-freetype \
    switch-curl \
    switch-mbedtls \
    switch-zlib \
    switch-libpng \
    switch-libjpeg-turbo \
    switch-bzip2 \
    switch-rapidjson \
    switch-mpv

echo ""
echo "╔══════════════════════════════════════════╗"
echo "║  ✅ Installation Complete!               ║"
echo "╚══════════════════════════════════════════╝"
echo ""
echo "Add these to your ~/.bashrc:"
echo ""
echo '  export DEVKITPRO=/opt/devkitpro'
echo '  export DEVKITARM=$DEVKITPRO/devkitARM'
echo '  export DEVKITPPC=$DEVKITPRO/devkitPPC'
echo '  export PATH=$DEVKITPRO/tools/bin:$PATH'
echo ""
echo "Then run: source ~/.bashrc"
echo "Then cd into switchstream/ and run: make"
