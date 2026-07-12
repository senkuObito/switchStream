#!/bin/bash
# ─────────────────────────────────────────────
# SwitchStream — Rootless NRO Build Script
# ─────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DEVKITPRO="${SCRIPT_DIR}/devkitpro_temp/local_root/opt/devkitpro"
export PATH="${DEVKITPRO}/devkitA64/bin:${DEVKITPRO}/tools/bin:${PATH}"

echo "🔨 Building SwitchStream NRO using local devkitPro toolchain..."
make "$@"
