#!/bin/bash
# ─────────────────────────────────────────────
# SwitchStream — Local Simulator Runner
# ─────────────────────────────────────────────

set -e

# Step 1: Install dependencies if missing
if ! dpkg -s libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev rapidjson-dev libmpv-dev >/dev/null 2>&1; then
    echo "📦 Installing missing dependencies (requires sudo)..."
    sudo apt-get update
    sudo apt-get install -y libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev rapidjson-dev libmpv-dev
else
    echo "✅ All dependencies are already installed."
fi

# Step 2: Compile the simulator
echo "🛠️ Compiling SwitchStream Simulator..."
make -f Makefile.linux clean
make -f Makefile.linux -j$(nproc)

# Step 3: Run the simulator
echo "🚀 Running SwitchStream Simulator..."
./switchstream_sim
