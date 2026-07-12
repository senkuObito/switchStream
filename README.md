# SwitchStream

Lightweight Stremio client for jailbroken Nintendo Switch.

## Features
- Browse catalogs from Stremio addons
- Search movies & series across all installed addons
- View details, ratings, descriptions
- Stream selection from multiple addon sources
- Watch history & bookmarks (saved to SD card)
- Install/remove addons by URL
- Dark theme, controller-native UI

## Requirements
- Nintendo Switch with CFW (Atmosphère)
- devkitPro with switch-dev packages
- Network connection

## Building

```bash
# Install devkitPro packages
sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf \
    switch-sdl2_image switch-freetype switch-curl \
    switch-mbedtls switch-zlib switch-libpng switch-libjpeg-turbo

# Build
make

# Deploy via nxlink
nxlink --address <SWITCH_IP> switchstream.nro
```

## Controls
| Button | Action |
|--------|--------|
| D-Pad  | Navigate |
| A      | Select / Play |
| B      | Back |
| Y      | Search / Add addon |
| X      | Bookmark / Remove addon |
| L      | Library |
| R      | Addons |
| +      | Exit |

## Adding Addons
1. Go to Addons screen (press R)
2. Press Y to enter addon URL
3. Enter the manifest.json URL (e.g. `https://addon-url/manifest.json`)

## Architecture
```
source/
├── main.cpp           # Entry point
├── app.h/cpp          # App state machine + SDL2 rendering
├── core/
│   ├── addon_client   # Stremio protocol HTTP client
│   ├── addon_manager  # Multi-addon aggregation
│   └── library        # Watch history persistence
├── net/
│   ├── http_client    # libcurl wrapper
│   └── image_cache    # LRU poster cache (32MB cap)
└── player/            # mpv integration (TODO)
```

## Lightweight Design
- **No heavy UI frameworks** — raw SDL2 rendering
- **rapidjson** — 10x faster than nlohmann/json
- **32MB image cache** with LRU eviction
- **Lazy loading** — only visible content is fetched
- **System font** — no bundled fonts (uses Switch shared font)
- **~2MB binary** estimated (excluding mpv/ffmpeg libs)
- **No exceptions/RTTI** — smaller binary, faster execution

## License
MIT
