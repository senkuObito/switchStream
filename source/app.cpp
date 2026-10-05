// SwitchStream — App implementation (Init, Input, Render loop)
#include "app.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <thread>
#include <future>
#include <sstream>
#include <fstream>
#include <regex>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#ifdef __SWITCH__
#include <switch.h>
#endif
#include "torrent/torrent.h"
#include "torrent/torrent_stream.h"

// Switch screen: 1280x720
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// Color palette — modern dark slate theme
static constexpr SDL_Color BG_COLOR       = {14,  17,  24,  255}; // deep midnight navy (#0e1118)
static constexpr SDL_Color CARD_COLOR     = {22,  27,  38,  255}; // modern dark card (#161b26)
static constexpr SDL_Color CARD_HL        = {32,  40,  56,  255}; // dark highlight
static constexpr SDL_Color ACCENT         = {0,   229, 255, 255}; // vibrant electric cyan (#00e5ff)
static constexpr SDL_Color ACCENT_MUTED   = {0,   160, 190, 255}; // muted cyan
static constexpr SDL_Color TEXT_PRIMARY    = {245, 248, 255, 255}; // crisp bright white
static constexpr SDL_Color TEXT_SECONDARY  = {145, 158, 180, 255}; // soft slate gray
static constexpr SDL_Color NAV_BG         = {14,  17,  24,  255};
static constexpr SDL_Color NAV_CONTAINER  = {22,  27,  39,  240}; // pill container (#161b27)
static constexpr SDL_Color NAV_BORDER     = {42,  50,  72,  180}; // pill border (#2a3248)

// Poster dimensions
static constexpr int POSTER_W = 150;
static constexpr int POSTER_H = 225;
static constexpr int POSTER_GAP = 16;
static constexpr int ROW_HEIGHT = 286;

struct DiscoverAddon {
    std::string name;
    std::string description;
    std::string url;
};

static const std::vector<DiscoverAddon> DISCOVER_ADDONS = {
    // --- Catalog Addons (provide home page content + search) ---
    {"Cinemeta", "The official addon for movie and series catalogs & search.", "https://v3-cinemeta.strem.io/manifest.json"},
    {"yastream", "Stream Asian dramas, series and movies directly with multiple providers.", "https://yastream.tamthai.de/manifest.json"},
    {"K-Drama Crush", "Asian and Korean drama catalog (drama feeds).", "https://83e20802dcf1-kdramacrush.baby-beamup.club/manifest.json"},
    {"IndiaStreams", "Trending movies and shows from Indian platforms (Tamil, Hindi, etc.).", "https://indiastreams.rdata.in/manifest.json"},
    {"Indian Regional Catalog", "Indian regional movies catalog by language (Tamil, Telugu, Hindi).", "https://83e20802dcf1-indian-streams.baby-beamup.club/manifest.json"},
    {"Streaming Catalogs", "Catalogs from Netflix, Disney+, HBO, Prime, etc.", "https://7a82163c306e-streaming-catalogs.baby-beamup.club/manifest.json"},
    {"CyberFlix Catalogs", "Movie and series catalogs sorted by streaming provider.", "https://cyberflix.elfhosted.com/manifest.json"},
    {"TMDB Collections", "Movie collections grouped by franchise.", "https://61ab9c85a149-tmdb-collections.baby-beamup.club/manifest.json"},
    {"Dramayo", "Asian dramas, series and movies catalog & streams.", "https://dramayo.stream/manifest.json"},
    {"AnimeStream", "Anime streaming catalog and metadata provider.", "https://animestream-addon.keypop3750.workers.dev/manifest.json"},

    // --- Stream Addons (provide playable stream links) ---
    {"Torrentio", "Torrent streams from YTS, RARBG, 1337x, etc.", "https://torrentio.strem.fun/manifest.json"},
    {"Filtorrent", "Curated & shaped Torrentio + TorrentsDB streams with resolution filtering.", "https://ce8c71dcef3b-filtorrent.baby-beamup.club/manifest.json"},
    {"InMax", "Indian and regional movies & series streams from TamilMV / TamilBlasters.", "https://inmax-tmv.onrender.com/manifest.json"},
    {"IndTorrents", "Indian torrent streams for regional movies and series.", "https://indtorrents.codecrafts.workers.dev/manifest.json"},
    {"ThePirateBay+", "ThePirateBay+ torrent streams provider.", "https://thepiratebay-plus.strem.fun/manifest.json"},
    {"TorrentsDB", "TorrentsDB multi-tracker torrent stream search.", "https://torrentsdb.com/manifest.json"},
    {"StreamViX | ElfHosted", "StreamViX multi-source HTTP streams provider.", "https://streamvix.hayd.uk/manifest.json"},
    {"NoDebrid", "Direct HTTP/HLS streams without requiring Debrid subscriptions.", "https://nodebrid.fly.dev/manifest.json"},
    {"StreamAsia", "Asian drama, movie and series streams from Dramacool.", "https://stremio-dramacool-addon.xyz/manifest.json"},
    {"Flix Streams", "HTTP streams from multiple sources.", "https://flixnest.app/flix-streams/manifest.json"},
    {"Nebula Streams", "HTTP streams from multiple sources.", "https://nebulastreams.onrender.com/manifest.json"},
    {"MovieBox", "HTTP streams for movies and series.", "https://moviebox-cfa7.onrender.com/manifest.json"},
    {"Sword Watch", "HTTP streams (currently offline - Vercel deployment disabled).", "https://sword-watch.vercel.app/manifest.json"},
    {"Comet | ElfHosted", "Fast torrent/debrid stream search.", "https://comet.elfhosted.com/manifest.json"},
    {"KnightCrawler", "Alternative torrent and debrid stream search.", "https://knightcrawler.elfhosted.com/manifest.json"},
    {"Debrid Search", "Search and stream files directly from your Debrid torrent cloud cache.", "https://debrid-search.strem.fun/manifest.json"},
    {"YouTube", "Watch YouTube content directly.", "https://youtube-v2.strem.fun/manifest.json"},
    {"WatchHub", "Official streaming links (Netflix, Prime, etc.).", "https://watchhub.strem.io/manifest.json"},

    // --- Utility Addons ---
    {"OpenSubtitles v3", "Subtitles in 50+ languages.", "https://opensubtitles-v3.strem.io/manifest.json"}
};

namespace ss {

static bool isFourKOrHigher(const Stream& s) {
    std::string hay = s.name + " " + s.title;
    for (char& c : hay) c = (char)tolower((unsigned char)c);

    if (hay.find("2160") != std::string::npos ||
        hay.find("uhd") != std::string::npos ||
        hay.find("4320") != std::string::npos ||
        hay.find("8k") != std::string::npos) {
        return true;
    }

    size_t pos = 0;
    while ((pos = hay.find("4k", pos)) != std::string::npos) {
        bool leftOk = (pos == 0 || !isalnum((unsigned char)hay[pos - 1]));
        bool rightOk = (pos + 2 >= hay.size() || !isalnum((unsigned char)hay[pos + 2]));
        if (leftOk && rightOk) return true;
        pos += 2;
    }

    return false;
}

App::App()
    : m_addonClient(m_http)
    , m_addonManager(m_addonClient)
{}

App::~App() {
    shutdown();
}

bool App::init() {
    // SDL init
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) return false;
    if (TTF_Init() < 0) return false;
    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP;
    if ((IMG_Init(imgFlags) & (IMG_INIT_PNG | IMG_INIT_JPG)) == 0) return false;

#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/switchstream", 0777);
    mkdir("sdmc:/switch/switchstream/imgcache", 0777);
#else
    system("mkdir -p ./switchstream_data/imgcache");
#endif

    m_downloadWorkerRunning = true;
    m_downloadWorkers.clear();
    for (int i = 0; i < NUM_DOWNLOAD_WORKERS; ++i) {
        m_downloadWorkers.emplace_back(&App::downloadWorkerLoop, this);
    }
    
    m_workerRunning = true;
    m_detailWorkerThread = std::thread(&App::detailWorkerLoop, this);

    // Use windowed mode on Linux for easier debugging, fullscreen on Switch
#ifdef __SWITCH__
    m_window = SDL_CreateWindow("SwitchStream",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
#else
    m_window = SDL_CreateWindow("SwitchStream (Simulator)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
#endif
    if (!m_window) return false;

    m_renderer = SDL_CreateRenderer(m_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) return false;

    // Hide the default SDL mouse cursor so touch doesn't feel like a mouse pointer
    SDL_ShowCursor(SDL_DISABLE);

#ifdef __SWITCH__
    // Load system font (Switch has a shared font we can use)
    PlFontData fontData;
    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    SDL_RWops* fontRW = SDL_RWFromMem(fontData.address, fontData.size);
    m_fontNormal = TTF_OpenFontRW(fontRW, 0, 20);
    fontRW = SDL_RWFromMem(fontData.address, fontData.size);
    m_fontSmall  = TTF_OpenFontRW(fontRW, 0, 16);
    fontRW = SDL_RWFromMem(fontData.address, fontData.size);
    m_fontLarge  = TTF_OpenFontRW(fontRW, 0, 28);
#else
    // On Linux, try common system fonts
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/libreoffice/opens___.ttf"
    };
    const char* selectedPath = nullptr;
    for (const char* path : fontPaths) {
        FILE* f = fopen(path, "r");
        if (f) {
            fclose(f);
            selectedPath = path;
            break;
        }
    }
    if (selectedPath) {
        m_fontNormal = TTF_OpenFont(selectedPath, 20);
        m_fontSmall  = TTF_OpenFont(selectedPath, 16);
        m_fontLarge  = TTF_OpenFont(selectedPath, 28);
    }
#endif

    if (!m_fontNormal || !m_fontSmall || !m_fontLarge) return false;

    // Image cache — 128MB max (lightweight, fits 250+ posters smoothly)
    m_imageCache = new ImageCache(m_renderer, 128);

    // Load saved data
    m_addonManager.loadConfig(CONFIG_FILE);
    m_library.load(LIB_FILE);

    {
        const std::vector<std::string> defaultAddons = {
            "https://v3-cinemeta.strem.io/manifest.json",
            "https://torrentio.strem.fun/manifest.json",
            "https://ce8c71dcef3b-filtorrent.baby-beamup.club/manifest.json",
            "https://dramayo.stream/manifest.json",
            "https://cyberflix.elfhosted.com/manifest.json",
            "https://yastream.tamthai.de/manifest.json",
            "https://83e20802dcf1-kdramacrush.baby-beamup.club/manifest.json",
            "https://free.flixnest.app/manifest.json",
            "https://opensubtitles-v3.strem.io/manifest.json",
            "https://watchhub.strem.io/manifest.json",
            "https://badboysxs-morpheus.hf.space/manifest.json",
            "https://stremio.yukistreams.xyz/manifest.json",
            "https://sword-watch.vercel.app/manifest.json",
            "https://nagare.nexioapp.org/manifest.json"
        };
        bool updatedAddons = false;

        // Automatically uninstall deprecated/removed addons (Pengu, Anime Kitsu, AIOStreams)
        for (const auto& a : m_addonManager.getAddons()) {
            std::string tUrl = a.transportUrl;
            std::string aName = a.manifest.name;
            std::string aId = a.manifest.id;
            for (char& c : tUrl) c = (char)tolower((unsigned char)c);
            for (char& c : aName) c = (char)tolower((unsigned char)c);
            for (char& c : aId) c = (char)tolower((unsigned char)c);

            bool isPengu = (tUrl.find("pengu") != std::string::npos || aName.find("pengu") != std::string::npos || aId.find("pengu") != std::string::npos);
            bool isKitsu = (tUrl.find("anime-kitsu") != std::string::npos || aName.find("kitsu") != std::string::npos || aId.find("kitsu") != std::string::npos);
            bool isAio = (tUrl.find("aiostream") != std::string::npos || aName.find("aiostream") != std::string::npos || aId.find("aiostream") != std::string::npos);
            bool isMediaFusion = (tUrl.find("mediafusion") != std::string::npos || aName.find("mediafusion") != std::string::npos || aId.find("mediafusion") != std::string::npos);

            if (isPengu || isKitsu || isAio || isMediaFusion) {
                printf("[App] Removing deprecated/removed addon: %s (%s)\n", a.manifest.name.c_str(), a.transportUrl.c_str());
                m_addonManager.removeAddon(a.transportUrl);
                m_addonManager.removeAddon(a.manifest.id);
                updatedAddons = true;
            }
        }

        auto currentAddons = m_addonManager.getAddons();
        for (const auto& url : defaultAddons) {
            bool found = false;
            for (const auto& a : currentAddons) {
                if (a.transportUrl == url) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                printf("Registering missing default addon: %s\n", url.c_str());
                InstalledAddon a;
                a.transportUrl = url;
                a.enabled = true;
                m_addonManager.addAddon(a);
                updatedAddons = true;
            }
        }
        if (updatedAddons) {
            m_addonManager.saveConfig(CONFIG_FILE);
        }
    }

    // QA Control: check if more than 5 stream addons are enabled on app open
    if (m_addonManager.getEnabledStreamAddonCount() > 5 &&
        !m_addonManager.getSuppressStreamAddonWarning()) {
        m_showStreamWarningPopup = true;
        m_streamWarningIndex = 0; // Focus "Cancel" by default
        printf("[App] QA Control: %d stream addons enabled (> 5). Displaying performance advisory popup.\n",
               m_addonManager.getEnabledStreamAddonCount());
    }

    // 1. Try loading cached home catalogs from disk for instant startup (< 5ms)
    std::vector<CatalogRow> cachedRows;
    if (loadHomeCache(HOME_CACHE, cachedRows)) {
        std::lock_guard<std::mutex> lock(m_homeMutex);
        m_homeCatalogs = std::move(cachedRows);
        prequeuePosters(m_homeCatalogs, 3);
        m_loadingHome = false;
        printf("[App] Instantly loaded %zu home catalog rows from disk cache!\n", m_homeCatalogs.size());
    }

    // 2. Fetch fresh home catalogs in the background (refreshed on app open)
    loadHomeCatalogs(true);

#ifdef __SWITCH__
    // Init controller
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
#else
    // Open first available game controller on Linux simulator
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            m_gameController = SDL_GameControllerOpen(i);
            if (m_gameController) {
                printf("Connected GameController: %s\n", SDL_GameControllerName(m_gameController));
                break;
            }
        }
    }

    // Start background TorrServer for local torrent streaming
    printf("Starting background TorrServer for local torrent streaming...\n");
    system("mkdir -p ./switchstream_data/torrserver_db");
    system("./torrserver -p 8090 -d ./switchstream_data/torrserver_db > /dev/null 2>&1 &");
#endif

    // Initialize mpv player instance
    if (!m_player.init(m_window, m_renderer, m_addonManager.getHwDecode())) {
        printf("Warning: mpv player initialization failed!\n");
    }

    // Enable detailed diagnostic logging from native torrent engine
    torrent_set_log([](const char* msg) {
        printf("[TorrentLog] %s\n", msg);
    });

    return true;
}

void App::run() {
#ifdef __SWITCH__
    PadState pad;
    padInitializeDefault(&pad);
    hidInitializeTouchScreen();
#endif

    while (m_running) {
        m_player.update();

        // Check if pending torrent playback is ready to start via mpv on the main thread
        bool shouldPlayTorrent = false;
        std::string torrentHeaders;
        {
            std::lock_guard<std::mutex> lock(m_torrentMutex);
            if (m_pendingTorrentPlay) {
                m_pendingTorrentPlay = false;
                shouldPlayTorrent = true;
                torrentHeaders = m_pendingTorrentHeaders;
            }
        }
        if (shouldPlayTorrent) {
            printf("[App] Starting mpv playback on main thread!\n");
            m_player.play("torrent://stream", torrentHeaders);
        }

        // If playing and finished (EOF reached after actual playback), go back to detail screen
        if (m_screen == Screen::PLAYER && m_player.isFinished() && !TorrentStream::instance().isOpening() && !m_torrentBuffering) {
            printf("[App] Playback reached EOF, returning to detail screen\n");
            m_player.stop();
            m_screen = Screen::DETAIL;
        }

#ifdef __SWITCH__
        if (!appletMainLoop()) break;
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);

        // Global: + button to exit
        if (kDown & HidNpadButton_Plus) {
            m_running = false;
            break;
        }

        // Read touch state
        static bool wasTouched = false;
        static int lastTouchX = 0;
        static int lastTouchY = 0;
        static int startTouchX = 0;
        static int startTouchY = 0;
        static uint32_t touchStartTime = 0;
        static bool touchDragging = false;
        static bool touchLongPressed = false;

        HidTouchScreenState touchScreenState = {0};
        if (hidGetTouchScreenStates(&touchScreenState, 1) && touchScreenState.count > 0) {
            int touchX = touchScreenState.touches[0].x;
            int touchY = touchScreenState.touches[0].y;

            if (!wasTouched) {
                // Touch down
                startTouchX = touchX;
                startTouchY = touchY;
                lastTouchX = touchX;
                lastTouchY = touchY;
                touchStartTime = SDL_GetTicks();
                touchDragging = false;
                touchLongPressed = false;
                m_dragAccumX = 0;
                m_dragAccumY = 0;
                printf("[Touch] Down: x=%d, y=%d\n", touchX, touchY);
            } else {
                // Touch hold/drag
                int dx = touchX - lastTouchX;
                int dy = touchY - lastTouchY;
                int totalDx = touchX - startTouchX;
                int totalDy = touchY - startTouchY;

                if (!touchDragging && (abs(totalDx) > 15 || abs(totalDy) > 15)) {
                    touchDragging = true;
                    printf("[Touch] Drag started (threshold exceeded: totalDx=%d, totalDy=%d)\n", totalDx, totalDy);
                }

                if (touchDragging) {
                    handleDrag(dx, dy);
                } else if (!touchLongPressed && (SDL_GetTicks() - touchStartTime > 600)) {
                    touchLongPressed = true;
                    printf("[Touch] LongPress timer reached (>600ms)\n");
                    handleLongPress(startTouchX, startTouchY);
                }

                lastTouchX = touchX;
                lastTouchY = touchY;
            }
            wasTouched = true;
        } else {
            if (wasTouched) {
                // Touch up
                printf("[Touch] Up: start_x=%d, start_y=%d (dragging=%d, longpressed=%d)\n",
                       startTouchX, startTouchY, touchDragging, touchLongPressed);
                if (m_screen == Screen::PLAYER && m_isScrubbing) {
                    m_player.seekAbsolute(m_scrubCurrentPos);
                    m_isScrubbing = false;
                    m_dragAccumX = 0;
                    m_dragAccumY = 0;
                    printf("[Touch] Scrub finished. Seek to %.2f\n", m_scrubCurrentPos);
                } else if (!touchDragging && !touchLongPressed) {
                    handleTouch(startTouchX, startTouchY);
                }
            }
            wasTouched = false;
        }

        handleInputForPad(kDown);
#else
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                m_running = false;
                break;
            }
            // Handle mouse button clicks / touch simulations
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                m_mouseDown = true;
                m_startMouseX = event.button.x;
                m_startMouseY = event.button.y;
                m_lastMouseX = event.button.x;
                m_lastMouseY = event.button.y;
                m_mouseStartTime = SDL_GetTicks();
                m_mouseDragging = false;
                m_mouseLongPressed = false;
                m_dragAccumX = 0;
                m_dragAccumY = 0;
                printf("[Mouse] Down: x=%d, y=%d\n", event.button.x, event.button.y);
            }
            else if (event.type == SDL_MOUSEMOTION) {
                if (m_mouseDown) {
                    int mouseX = event.motion.x;
                    int mouseY = event.motion.y;
                    int dx = mouseX - m_lastMouseX;
                    int dy = mouseY - m_lastMouseY;
                    int totalDx = mouseX - m_startMouseX;
                    int totalDy = mouseY - m_startMouseY;

                    if (!m_mouseDragging && (abs(totalDx) > 15 || abs(totalDy) > 15)) {
                        m_mouseDragging = true;
                        printf("[Mouse] Drag started (threshold exceeded: totalDx=%d, totalDy=%d)\n", totalDx, totalDy);
                    }

                    if (m_mouseDragging) {
                        handleDrag(dx, dy);
                    }

                    m_lastMouseX = mouseX;
                    m_lastMouseY = mouseY;
                }
            }
            else if (event.type == SDL_MOUSEBUTTONUP) {
                if (m_mouseDown) {
                    printf("[Mouse] Up: start_x=%d, start_y=%d (dragging=%d, longpressed=%d)\n",
                           m_startMouseX, m_startMouseY, m_mouseDragging, m_mouseLongPressed);
                    if (m_screen == Screen::PLAYER && m_isScrubbing) {
                        m_player.seekAbsolute(m_scrubCurrentPos);
                        m_isScrubbing = false;
                        m_dragAccumX = 0;
                        m_dragAccumY = 0;
                        printf("[Mouse] Scrub finished. Seek to %.2f\n", m_scrubCurrentPos);
                    } else if (!m_mouseDragging && !m_mouseLongPressed) {
                        handleTouch(m_startMouseX, m_startMouseY);
                    }
                    m_mouseDown = false;
                }
            }
            // Handle hotplugging controllers
            else if (event.type == SDL_CONTROLLERDEVICEADDED) {
                if (!m_gameController) {
                    m_gameController = SDL_GameControllerOpen(event.cdevice.which);
                    if (m_gameController) {
                        printf("Controller plugged in: %s\n", SDL_GameControllerName(m_gameController));
                    }
                }
            }
            else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
                if (m_gameController && event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(m_gameController))) {
                    SDL_GameControllerClose(m_gameController);
                    m_gameController = nullptr;
                    printf("Controller disconnected.\n");
                }
            }
            // Handle gamepad buttons
            else if (event.type == SDL_CONTROLLERBUTTONDOWN) {
                u64 key = 0;
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:   key |= HidNpadButton_Down; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:     key |= HidNpadButton_Up; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:   key |= HidNpadButton_Left; break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:  key |= HidNpadButton_Right; break;
                    case SDL_CONTROLLER_BUTTON_A:           key |= HidNpadButton_A; break;
                    case SDL_CONTROLLER_BUTTON_B:           key |= HidNpadButton_B; break;
                    case SDL_CONTROLLER_BUTTON_X:           key |= HidNpadButton_X; break;
                    case SDL_CONTROLLER_BUTTON_Y:           key |= HidNpadButton_Y; break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:key |= HidNpadButton_L; break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:key |= HidNpadButton_R; break;
                    case SDL_CONTROLLER_BUTTON_START:
                        if (m_screen == Screen::PLAYER) {
                            m_player.stop();
                            m_screen = Screen::DETAIL;
                        } else {
                            m_running = false;
                        }
                        break;
                }
                if (key != 0) {
                    handleInputForPad(key);
                }
            }
            // Handle keyboard keys
            else if (event.type == SDL_KEYDOWN) {
                u64 key = 0;
                switch (event.key.keysym.sym) {
                    case SDLK_DOWN:     key |= HidNpadButton_Down; break;
                    case SDLK_UP:       key |= HidNpadButton_Up; break;
                    case SDLK_LEFT:     key |= HidNpadButton_Left; break;
                    case SDLK_RIGHT:    key |= HidNpadButton_Right; break;
                    case SDLK_RETURN:
                    case SDLK_SPACE:    key |= HidNpadButton_A; break;
                    case SDLK_ESCAPE:
                        if (m_screen == Screen::PLAYER || m_screen == Screen::ADDONS || m_screen == Screen::DETAIL || m_screen == Screen::LIBRARY) {
                            key |= HidNpadButton_B;
                        } else {
                            m_running = false;
                        }
                        break;
                    case SDLK_BACKSPACE:key |= HidNpadButton_B; break;
                    case SDLK_s:        key |= HidNpadButton_Y; break;
                    case SDLK_b:
                    case SDLK_x:        key |= HidNpadButton_X; break;
                    case SDLK_l:        key |= HidNpadButton_L; break;
                    case SDLK_a:        key |= HidNpadButton_R; break;
                }
                if (key != 0) {
                    handleInputForPad(key);
                }
            }
        }
        if (m_mouseDown && !m_mouseDragging && !m_mouseLongPressed) {
            if (SDL_GetTicks() - m_mouseStartTime > 600) {
                m_mouseLongPressed = true;
                printf("[Mouse] LongPress timer reached (>600ms)\n");
                handleLongPress(m_startMouseX, m_startMouseY);
            }
        }
        static Screen lastScreen = Screen::HOME;
        if (m_screen != lastScreen) {
            printf("[Screen] Transition: %d -> %d\n", (int)lastScreen, (int)m_screen);
            lastScreen = m_screen;
        }
#endif

        render();
    }
}

void App::handleInputForPad(u64 kDown) {
    switch (m_screen) {
    case Screen::HOME: {
        if (m_showStreamWarningPopup) {
            if (kDown & (HidNpadButton_Left | HidNpadButton_Right)) {
                m_streamWarningIndex = 1 - m_streamWarningIndex;
            }
            if (kDown & HidNpadButton_B) {
                // Cancel
                m_showStreamWarningPopup = false;
            }
            if (kDown & HidNpadButton_X) {
                // Don't show again
                m_addonManager.setSuppressStreamAddonWarning(true);
                m_addonManager.saveConfig(CONFIG_FILE);
                m_showStreamWarningPopup = false;
            }
            if (kDown & HidNpadButton_A) {
                if (m_streamWarningIndex == 0) {
                    // Cancel
                    m_showStreamWarningPopup = false;
                } else {
                    // Don't show again
                    m_addonManager.setSuppressStreamAddonWarning(true);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    m_showStreamWarningPopup = false;
                }
            }
            break;
        }

        std::vector<CatalogRow> localCatalogs;
        {
            std::lock_guard<std::mutex> lock(m_homeMutex);
            localCatalogs = m_homeCatalogs;
        }

        if (kDown & HidNpadButton_Down) {
            m_homeRowIndex++;
            if (m_homeRowIndex >= (int)localCatalogs.size())
                m_homeRowIndex = (int)localCatalogs.size() - 1;
            m_homeColIndex = 0;
        }
        if (kDown & HidNpadButton_Up) {
            m_homeRowIndex--;
            if (m_homeRowIndex < 0) m_homeRowIndex = 0;
        }
        if (kDown & HidNpadButton_Right) {
            m_homeColIndex++;
            if (m_homeRowIndex < (int)localCatalogs.size()) {
                int maxCol = (int)localCatalogs[m_homeRowIndex].items.size() - 1;
                if (m_homeColIndex > maxCol) m_homeColIndex = maxCol;
            }
        }
        if (kDown & HidNpadButton_Left) {
            m_homeColIndex--;
            if (m_homeColIndex < 0) m_homeColIndex = 0;
        }
        if (kDown & HidNpadButton_A) {
            // Open detail for selected item
            if (m_homeRowIndex < (int)localCatalogs.size() &&
                m_homeColIndex < (int)localCatalogs[m_homeRowIndex].items.size()) {
                auto& item = localCatalogs[m_homeRowIndex].items[m_homeColIndex];
                loadDetail(item.type, item.id);
                m_screen = Screen::DETAIL;
            }
        }
        if (kDown & HidNpadButton_Y) {
            // Open search
            openSwkbd(m_searchQuery, "Search movies & series");
            if (!m_searchQuery.empty()) {
                performSearch(m_searchQuery);
                m_screen = Screen::SEARCH;
            }
        }
        if (kDown & HidNpadButton_L) m_screen = Screen::LIBRARY;
        if (kDown & HidNpadButton_R) m_screen = Screen::ADDONS;
        if (kDown & HidNpadButton_X) {
            m_settingsIndex = 0;
            m_screen = Screen::SETTINGS;
        }
        if (kDown & HidNpadButton_B) {
            if (m_homeRowIndex != 0 || m_homeColIndex != 0) {
                m_homeRowIndex = 0;
                m_homeColIndex = 0;
            } else {
                loadHomeCatalogs();
            }
        }
        break;
    }

    case Screen::SEARCH:
        if (m_loadingSearch) {
            if (kDown & HidNpadButton_B) m_screen = Screen::HOME;
            break;
        }
        if (kDown & HidNpadButton_Down) m_searchIndex++;
        if (kDown & HidNpadButton_Up) m_searchIndex--;
        if (m_searchIndex < 0) m_searchIndex = 0;
        if (m_searchIndex >= (int)m_searchResults.size())
            m_searchIndex = (int)m_searchResults.size() - 1;
        if (kDown & HidNpadButton_A && !m_searchResults.empty()) {
            auto& item = m_searchResults[m_searchIndex];
            loadDetail(item.type, item.id);
            m_screen = Screen::DETAIL;
        }
        if (kDown & HidNpadButton_B) m_screen = Screen::HOME;
        if (kDown & HidNpadButton_Y) {
            openSwkbd(m_searchQuery, "Search");
            if (!m_searchQuery.empty()) performSearch(m_searchQuery);
        }
        if (kDown & HidNpadButton_X) {
            m_searchSort = (m_searchSort == SearchSort::YEAR_DESC) ? SearchSort::DEFAULT : SearchSort::YEAR_DESC;
            {
                std::lock_guard<std::mutex> lock(m_searchMutex);
                sortSearchResults();
            }
            m_searchIndex = 0;
        }
        break;

    case Screen::DETAIL:
        {
            if (m_loadingDetail) {
                if (kDown & HidNpadButton_B) {
                    m_screen = Screen::HOME;
                    m_loadingDetail = false;
                }
                break;
            }

            std::vector<Video> currentSeasonEps;
            std::vector<Stream> localStreams;
            int numSeasons = 0;
            std::string selectedEpId;
            DetailFocus currentFocus = DetailFocus::STREAMS;
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                if (!m_detailEpisodes.empty() && !m_detailSeasons.empty()) {
                    numSeasons = (int)m_detailSeasons.size();
                    int targetSeason = m_detailSeasons[m_detailSeasonFilter];
                    for (const auto& ep : m_detailEpisodes) {
                        if (ep.season == targetSeason) {
                            currentSeasonEps.push_back(ep);
                        }
                    }
                }
                localStreams = m_detailStreams;
                currentFocus = m_detailFocus;
            }

            if (!currentSeasonEps.empty()) {
                // TV Series
                if (currentFocus == DetailFocus::EPISODES) {
                    // Season switching
                    if ((kDown & HidNpadButton_R) && numSeasons > 1) {
                        {
                            std::lock_guard<std::mutex> lock(m_streamsMutex);
                            m_detailSeasonFilter = (m_detailSeasonFilter + 1) % numSeasons;
                            m_detailEpisodeIndex = 0;
                            int targetSeason = m_detailSeasons[m_detailSeasonFilter];
                            for (const auto& ep : m_detailEpisodes) {
                                if (ep.season == targetSeason) {
                                    selectedEpId = ep.id;
                                    break;
                                }
                            }
                        }
                        if (!selectedEpId.empty()) {
                            loadEpisodeStreams(selectedEpId);
                        }
                    } else if ((kDown & HidNpadButton_L) && numSeasons > 1) {
                        {
                            std::lock_guard<std::mutex> lock(m_streamsMutex);
                            m_detailSeasonFilter = (m_detailSeasonFilter - 1 + numSeasons) % numSeasons;
                            m_detailEpisodeIndex = 0;
                            int targetSeason = m_detailSeasons[m_detailSeasonFilter];
                            for (const auto& ep : m_detailEpisodes) {
                                if (ep.season == targetSeason) {
                                    selectedEpId = ep.id;
                                    break;
                                }
                            }
                        }
                        if (!selectedEpId.empty()) {
                            loadEpisodeStreams(selectedEpId);
                        }
                    } else if (kDown & HidNpadButton_Down) {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_detailEpisodeIndex++;
                        if (m_detailEpisodeIndex >= (int)currentSeasonEps.size())
                            m_detailEpisodeIndex = (int)currentSeasonEps.size() - 1;
                    } else if (kDown & HidNpadButton_Up) {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_detailEpisodeIndex--;
                        if (m_detailEpisodeIndex < 0) m_detailEpisodeIndex = 0;
                    } else if ((kDown & HidNpadButton_Right) || (kDown & HidNpadButton_A)) {
                        std::string targetEpId;
                        {
                            std::lock_guard<std::mutex> lock(m_streamsMutex);
                            if (m_detailEpisodeIndex >= 0 && m_detailEpisodeIndex < (int)currentSeasonEps.size()) {
                                targetEpId = currentSeasonEps[m_detailEpisodeIndex].id;
                            }
                            m_detailFocus = DetailFocus::STREAMS;
                        }
                        if (!targetEpId.empty() && targetEpId != m_currentPlayingEpisodeId) {
                            loadEpisodeStreams(targetEpId);
                        }
                    } else if (kDown & HidNpadButton_B) {
                        m_screen = Screen::HOME;
                    }
                } else {
                    // STREAMS focus
                    if (kDown & HidNpadButton_Down) {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_detailStreamIndex++;
                        if (m_detailStreamIndex >= (int)m_detailStreams.size())
                            m_detailStreamIndex = m_detailStreams.empty() ? 0 : (int)m_detailStreams.size() - 1;
                    } else if (kDown & HidNpadButton_Up) {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_detailStreamIndex--;
                        if (m_detailStreamIndex < 0) m_detailStreamIndex = 0;
                    } else if (kDown & HidNpadButton_A) {
                        Stream toPlay;
                        bool hasStream = false;
                        {
                            std::lock_guard<std::mutex> lock(m_streamsMutex);
                            if (m_detailStreamIndex >= 0 && m_detailStreamIndex < (int)m_detailStreams.size()) {
                                toPlay = m_detailStreams[m_detailStreamIndex];
                                hasStream = true;
                            }
                        }
                        if (hasStream) {
                            playStream(toPlay);
                        }
                    } else if ((kDown & HidNpadButton_Left) || (kDown & HidNpadButton_B)) {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_detailFocus = DetailFocus::EPISODES;
                    }
                }
            } else {
                // Movies
                if (kDown & HidNpadButton_Down) {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailStreamIndex++;
                    if (m_detailStreamIndex >= (int)m_detailStreams.size())
                        m_detailStreamIndex = m_detailStreams.empty() ? 0 : (int)m_detailStreams.size() - 1;
                } else if (kDown & HidNpadButton_Up) {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailStreamIndex--;
                    if (m_detailStreamIndex < 0) m_detailStreamIndex = 0;
                } else if (kDown & HidNpadButton_A) {
                    Stream toPlay;
                    bool hasStream = false;
                    {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        if (m_detailStreamIndex >= 0 && m_detailStreamIndex < (int)m_detailStreams.size()) {
                            toPlay = m_detailStreams[m_detailStreamIndex];
                            hasStream = true;
                        }
                    }
                    if (hasStream) {
                        playStream(toPlay);
                    }
                } else if (kDown & HidNpadButton_B) {
                    m_screen = Screen::HOME;
                }
            }

            if (kDown & HidNpadButton_X) {
                m_library.toggleBookmark(m_detailMeta.id, m_detailMeta.type,
                                          m_detailMeta.name, m_detailMeta.poster);
                m_library.save(LIB_FILE);
            }
        }
        break;

    case Screen::LIBRARY:
        if (kDown & HidNpadButton_Down) m_libraryIndex++;
        if (kDown & HidNpadButton_Up) m_libraryIndex--;
        if (m_libraryIndex < 0) m_libraryIndex = 0;
        if (kDown & HidNpadButton_B) m_screen = Screen::HOME;
        break;

    case Screen::ADDONS: {
        auto addons = m_addonManager.getAddons();
        
        if (kDown & HidNpadButton_Right) {
            m_addonDiscoverPane = true;
        }
        if (kDown & HidNpadButton_Left) {
            m_addonDiscoverPane = false;
        }

        if (kDown & HidNpadButton_Down) {
            if (!m_addonDiscoverPane) {
                m_addonIndex++;
                if (m_addonIndex >= (int)addons.size()) m_addonIndex = addons.empty() ? 0 : (int)addons.size() - 1;
            } else {
                m_addonDiscoverIndex++;
                if (m_addonDiscoverIndex >= (int)DISCOVER_ADDONS.size()) m_addonDiscoverIndex = (int)DISCOVER_ADDONS.size() - 1;
            }
        }
        if (kDown & HidNpadButton_Up) {
            if (!m_addonDiscoverPane) {
                m_addonIndex--;
                if (m_addonIndex < 0) m_addonIndex = 0;
            } else {
                m_addonDiscoverIndex--;
                if (m_addonDiscoverIndex < 0) m_addonDiscoverIndex = 0;
            }
        }

        if (kDown & HidNpadButton_A) {
            if (m_addonDiscoverPane) {
                if (!m_loading) {
                    // Install community addon
                    if (m_addonDiscoverIndex >= 0 && m_addonDiscoverIndex < (int)DISCOVER_ADDONS.size()) {
                        std::string url = DISCOVER_ADDONS[m_addonDiscoverIndex].url;
                        
                        // Check if already installed
                        bool installed = false;
                        for (auto& a : addons) {
                            if (a.transportUrl == url) {
                                installed = true;
                                break;
                            }
                        }
                        
                        if (!installed) {
                            m_loading = true;
                            if (m_installThread.joinable()) {
                                m_installThread.join();
                            }
                            m_installThread = std::thread([this, url]() {
                                m_addonManager.installAddon(url);
                                m_addonManager.saveConfig(CONFIG_FILE);
                                loadHomeCatalogs(true);
                                m_loading = false;
                            });
                        }
                    }
                }
            } else {
                // Toggle enabled state of installed addon
                if (m_addonIndex >= 0 && m_addonIndex < (int)addons.size()) {
                    m_addonManager.toggleAddon(addons[m_addonIndex].manifest.id);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    loadHomeCatalogs(true); // Refresh catalogs immediately!
                }
            }
        }

        if (kDown & HidNpadButton_X) {
            // Remove selected addon from installed list
            if (!m_addonDiscoverPane) {
                if (m_addonIndex >= 0 && m_addonIndex < (int)addons.size()) {
                    m_addonManager.removeAddon(addons[m_addonIndex].manifest.id);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    if (m_addonIndex > 0) m_addonIndex--;
                    loadHomeCatalogs(true); // Refresh catalogs immediately!
                }
            }
        }

        if (kDown & HidNpadButton_L) {
            std::string host = m_addonManager.getTorrServerHost();
            openSwkbd(host, "Enter TorrServer URL (e.g. http://192.168.1.100:8090)");
            if (!host.empty()) {
                m_addonManager.setTorrServerHost(host);
                m_addonManager.saveConfig(CONFIG_FILE);
            }
        }

        if (kDown & HidNpadButton_Y) {
            // Add custom addon by URL
            std::string url;
            openSwkbd(url, "Enter addon manifest URL");
            if (!url.empty()) {
                m_addonManager.installAddon(url);
                m_addonManager.saveConfig(CONFIG_FILE);
                loadHomeCatalogs(true);  // refresh
            }
        }

        if (kDown & HidNpadButton_B) {
            m_screen = Screen::HOME;
            loadHomeCatalogs(true);
        }
        break;
    }

    case Screen::PLAYER: {
        uint32_t now = SDL_GetTicks();

        if (kDown) {
            if (m_showSubList) {
                m_osdShowTime = now;
                if (m_subAddonMode) {
                    if (m_subAddonLoading.load()) {
                        if (kDown & HidNpadButton_B) {
                            cancelAddonSubtitle();
                        }
                    } else {
                        std::vector<Subtitle> localSubs;
                        {
                            std::lock_guard<std::mutex> lock(m_subMutex);
                            localSubs = m_addonSubtitles;
                        }
                        if (localSubs.empty()) {
                            if (kDown & (HidNpadButton_B | HidNpadButton_A)) {
                                cancelAddonSubtitle();
                            }
                        } else {
                            if (kDown & HidNpadButton_Up) {
                                m_subAddonIndex = std::clamp(m_subAddonIndex - 1, 0, (int)localSubs.size() - 1);
                            }
                            else if (kDown & HidNpadButton_Down) {
                                m_subAddonIndex = std::clamp(m_subAddonIndex + 1, 0, (int)localSubs.size() - 1);
                            }
                            else if (kDown & HidNpadButton_A) {
                                applyAddonSubtitle(localSubs[m_subAddonIndex]);
                            }
                            else if (kDown & HidNpadButton_B) {
                                cancelAddonSubtitle();
                            }
                        }
                    }
                    break;
                } else {
                    auto tracks = m_player.getSubtitleTracks();
                    int totalItems = 1 + (int)tracks.size();
                    if (kDown & HidNpadButton_Up) {
                        m_subListIndex = std::clamp(m_subListIndex - 1, 0, totalItems - 1);
                    }
                    else if (kDown & HidNpadButton_Down) {
                        m_subListIndex = std::clamp(m_subListIndex + 1, 0, totalItems - 1);
                    }
                    else if (kDown & HidNpadButton_A) {
                        if (m_subListIndex == 0) {
                            fetchAddonSubtitles();
                        } else {
                            int trackIdx = m_subListIndex - 1;
                            if (trackIdx >= 0 && trackIdx < (int)tracks.size()) {
                                m_player.setSubtitleTrack(tracks[trackIdx].id);
                            }
                            m_showSubList = false;
                        }
                    }
                    else if (kDown & HidNpadButton_B) {
                        m_showSubList = false;
                    }
                    break;
                }
            }
            if (m_showAudioList) {
                m_osdShowTime = now;
                auto tracks = m_player.getAudioTracks();
                if (kDown & HidNpadButton_Up) {
                    m_audioListIndex = std::clamp(m_audioListIndex - 1, 0, (int)tracks.size() - 1);
                }
                else if (kDown & HidNpadButton_Down) {
                    m_audioListIndex = std::clamp(m_audioListIndex + 1, 0, (int)tracks.size() - 1);
                }
                else if (kDown & HidNpadButton_A) {
                    if (!tracks.empty()) {
                        m_player.setAudioTrack(tracks[m_audioListIndex].id);
                    }
                    m_showAudioList = false;
                }
                else if (kDown & HidNpadButton_B) {
                    m_showAudioList = false;
                }
                break;
            }
            if (m_showQualityList) {
                m_osdShowTime = now;
                std::vector<Stream> localStreams;
                {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    localStreams = m_detailStreams;
                }
                if (kDown & HidNpadButton_Up) {
                    m_qualityListIndex = std::clamp(m_qualityListIndex - 1, 0, (int)localStreams.size() - 1);
                }
                else if (kDown & HidNpadButton_Down) {
                    m_qualityListIndex = std::clamp(m_qualityListIndex + 1, 0, (int)localStreams.size() - 1);
                }
                else if (kDown & HidNpadButton_A) {
                    if (!localStreams.empty()) {
                        playStream(localStreams[m_qualityListIndex]);
                    }
                    m_showQualityList = false;
                }
                else if (kDown & HidNpadButton_B) {
                    m_showQualityList = false;
                }
                break;
            }

            bool isPlayerErr = m_torrentFailed || m_player.hasError() ||
                               (TorrentStream::instance().isActive() == false && !TorrentStream::instance().isOpening() && !m_lastPlayingMagnet.empty());
            if (isPlayerErr) {
                if (kDown & (HidNpadButton_B | HidNpadButton_A | HidNpadButton_Plus | HidNpadButton_X | HidNpadButton_Y)) {
                    printf("[App] Returning to DETAIL from player error state\n");
                    m_torrentBuffering = false;
                    m_torrentFailed = false;
                    m_lastPlayingMagnet.clear();
                    {
                        std::lock_guard<std::mutex> lock(m_torrentMutex);
                        m_torrentPollingActive = false;
                        m_pendingTorrentPlay = false;
                    }
                    if (m_torrentPollingThread.joinable()) {
                        m_torrentPollingThread.join();
                    }
                    m_player.stop();
                    m_screen = Screen::DETAIL;
                    break;
                }
            }

            if (kDown & HidNpadButton_B) {
                // If a popup overlay menu is open, B closes that menu
                if (m_showSubList || m_showAudioList || m_showQualityList) {
                    if (m_showSubList && m_subAddonMode) {
                        cancelAddonSubtitle();
                    } else {
                        m_showSubList = false;
                    }
                    m_subAddonMode = false;
                    m_showAudioList = false;
                    m_showQualityList = false;
                    m_osdShowTime = now;
                    break;
                }
                // Otherwise B exits the player immediately back to DETAIL
                printf("[App] B button pressed: stopping playback and returning to DETAIL\n");
                m_torrentBuffering = false;
                m_lastPlayingMagnet.clear();
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentPollingActive = false;
                    m_pendingTorrentPlay = false;
                }
                if (m_torrentPollingThread.joinable()) {
                    m_torrentPollingThread.join();
                }
                m_player.stop();
                m_screen = Screen::DETAIL;
                break;
            }

            m_osdShowTime = now;
            if (kDown & HidNpadButton_A) {
                m_player.togglePlay();
            }
            else if (kDown & HidNpadButton_Right) {
                m_player.seek(10.0);
            }
            else if (kDown & HidNpadButton_Left) {
                m_player.seek(-10.0);
            }
            else if (kDown & HidNpadButton_Up) {
                m_player.changeVolume(5.0);
            }
            else if (kDown & HidNpadButton_Down) {
                m_player.changeVolume(-5.0);
            }
                else if (kDown & HidNpadButton_Y) {
                    m_showSubList = true;
                    m_subListIndex = 0;
                    auto tracks = m_player.getSubtitleTracks();
                    for (int i = 0; i < (int)tracks.size(); i++) {
                        if (tracks[i].selected) {
                            m_subListIndex = i;
                            break;
                        }
                    }
                }
                else if (kDown & HidNpadButton_X) {
                    m_showAudioList = true;
                    m_audioListIndex = 0;
                    auto tracks = m_player.getAudioTracks();
                    for (int i = 0; i < (int)tracks.size(); i++) {
                        if (tracks[i].selected) {
                            m_audioListIndex = i;
                            break;
                        }
                    }
                }
                else if (kDown & HidNpadButton_L) {
                    // L button opens quality/stream switcher
                    m_showQualityList = true;
                    m_qualityListIndex = m_detailStreamIndex;
                }
        }
        break;
    }

    case Screen::SETTINGS: {
        if (kDown & HidNpadButton_Down) {
            m_settingsIndex++;
            if (m_settingsIndex >= 6) m_settingsIndex = 5;
        }
        if (kDown & HidNpadButton_Up) {
            m_settingsIndex--;
            if (m_settingsIndex < 0) m_settingsIndex = 0;
        }
        if (kDown & HidNpadButton_B) {
            m_screen = Screen::HOME;
        }
        if (kDown & HidNpadButton_A) {
            if (m_settingsIndex == 0) {
                // Toggle Enable Torrents
                m_addonManager.setEnableTorrents(!m_addonManager.getEnableTorrents());
                m_addonManager.saveConfig(CONFIG_FILE);
            } else if (m_settingsIndex == 1) {
                // Edit TorrServer Host
                std::string host = m_addonManager.getTorrServerHost();
                openSwkbd(host, "Enter TorrServer URL (e.g. http://192.168.1.100:8090)");
                if (!host.empty()) {
                    m_addonManager.setTorrServerHost(host);
                    m_addonManager.saveConfig(CONFIG_FILE);
                }
            } else if (m_settingsIndex == 2) {
                // Toggle Hardware Decoding
                bool enabled = !m_addonManager.getHwDecode();
                m_addonManager.setHwDecode(enabled);
                m_addonManager.saveConfig(CONFIG_FILE);
                m_player.setHwDec(enabled);
            } else if (m_settingsIndex == 3) {
                // Edit Preferred Subtitle Language
                std::string lang = m_addonManager.getSubtitleLang();
                openSwkbd(lang, "Enter Subtitle Language Code (e.g. en, es, fr)");
                if (!lang.empty()) {
                    m_addonManager.setSubtitleLang(lang);
                    m_addonManager.saveConfig(CONFIG_FILE);
                }
            } else if (m_settingsIndex == 4) {
                // Clean Cache & Reset
                cleanCatalogImageCache();
                std::remove(CONFIG_FILE);
                std::remove(LIB_FILE);
                m_library = Library();
                // Re-initialize config (will populate defaults since file is gone)
                m_addonManager.loadConfig(CONFIG_FILE);
                if (m_addonManager.getAddons().empty()) {
                    const std::vector<std::string> defaultAddons = {
                        "https://v3-cinemeta.strem.io/manifest.json",
                        "https://torrentio.strem.fun/manifest.json",
                        "https://ce8c71dcef3b-filtorrent.baby-beamup.club/manifest.json",
                        "https://dramayo.stream/manifest.json",
                        "https://cyberflix.elfhosted.com/manifest.json",
                        "https://yastream.tamthai.de/manifest.json",
                        "https://83e20802dcf1-kdramacrush.baby-beamup.club/manifest.json",
                        "https://free.flixnest.app/manifest.json",
                        "https://opensubtitles-v3.strem.io/manifest.json",
                        "https://watchhub.strem.io/manifest.json",
                        "https://badboysxs-morpheus.hf.space/manifest.json",
                        "https://stremio.yukistreams.xyz/manifest.json",
                        "https://sword-watch.vercel.app/manifest.json",
                        "https://nagare.nexioapp.org/manifest.json"
                    };
                    for (const auto& url : defaultAddons) {
                        m_addonManager.installAddon(url);
                    }
                    m_addonManager.saveConfig(CONFIG_FILE);
                }
                {
                    std::lock_guard<std::mutex> lock(m_homeMutex);
                    m_homeCatalogs.clear();
                }
                m_settingsIndex = 0;
                m_screen = Screen::HOME;
                loadHomeCatalogs(true);
            } else if (m_settingsIndex == 5) {
                m_screen = Screen::HOME;
            }
        }
        break;
    }

    default:
        if (kDown & HidNpadButton_B) m_screen = Screen::HOME;
        break;
    }
}

void App::handleTouch(int x, int y) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;

    switch (m_screen) {
    case Screen::HOME: {
        if (m_showStreamWarningPopup) {
            int dw = 680, dh = 270;
            int dx = (SCREEN_W - dw) / 2;
            int dy = (SCREEN_H - dh) / 2;
            int btnW = 240, btnH = 46;
            int btnY = dy + 180;
            int btn1X = dx + 65;
            int btn2X = dx + dw - 65 - btnW;

            if (x >= btn1X && x <= btn1X + btnW && y >= btnY && y <= btnY + btnH) {
                // Cancel button clicked
                m_showStreamWarningPopup = false;
                return;
            }
            if (x >= btn2X && x <= btn2X + btnW && y >= btnY && y <= btnY + btnH) {
                // Don't show again clicked
                m_addonManager.setSuppressStreamAddonWarning(true);
                m_addonManager.saveConfig(CONFIG_FILE);
                m_showStreamWarningPopup = false;
                return;
            }
            // Tap outside dialog box dismisses
            if (x < dx || x > dx + dw || y < dy || y > dy + dh) {
                m_showStreamWarningPopup = false;
                return;
            }
            return;
        }

        // ─── Header Navigation Click ───
        int navX = 280;
        if (y >= 10 && y <= 58) {
            if (x >= navX && x < navX + 100) {
                // Home tab
                m_homeRowIndex = 0;
                m_homeColIndex = 0;
                return;
            }
            if (x >= navX + 104 && x < navX + 214) {
                // Search tab
                std::string query;
                openSwkbd(query, "Search movies & series");
                if (!query.empty()) {
                    m_searchQuery = query;
                    performSearch(m_searchQuery);
                    m_screen = Screen::SEARCH;
                }
                return;
            }
            if (x >= navX + 218 && x < navX + 328) {
                m_screen = Screen::LIBRARY;
                return;
            }
            if (x >= navX + 332 && x < navX + 442) {
                m_screen = Screen::ADDONS;
                return;
            }
            if (x >= navX + 446 && x < navX + 576) {
                m_settingsIndex = 0;
                m_screen = Screen::SETTINGS;
                return;
            }
            if (x >= SCREEN_W - 140 && x <= SCREEN_W - 20) {
                m_running = false;
                return;
            }
        }

        // ─── Catalog Posters Grid ───
        std::vector<CatalogRow> localCatalogs;
        {
            std::lock_guard<std::mutex> lock(m_homeMutex);
            localCatalogs = m_homeCatalogs;
        }
        if (localCatalogs.empty() || m_loadingHome) return;

        int startY = 74;
        int visibleRows = 2;
        int firstRow = m_homeRowIndex > 0 ? m_homeRowIndex - 1 : 0;
        int maxVisibleCols = (SCREEN_W - 80) / (POSTER_W + POSTER_GAP);

        for (int r = firstRow; r < (int)localCatalogs.size() && r < firstRow + visibleRows + 1; r++) {
            auto& row = localCatalogs[r];
            int rowY = startY + (r - firstRow) * ROW_HEIGHT;
            int cardY = rowY + 34;

            int startCol = 0;
            if (r == m_homeRowIndex && m_homeColIndex >= maxVisibleCols)
                startCol = m_homeColIndex - maxVisibleCols + 1;

            for (int c = startCol; c < (int)row.items.size() && c < startCol + maxVisibleCols; c++) {
                int cardX = 40 + (c - startCol) * (POSTER_W + POSTER_GAP);
                if (x >= cardX && x < cardX + POSTER_W && y >= cardY && y < cardY + POSTER_H) {
                    auto& item = row.items[c];
                    m_homeRowIndex = r;
                    m_homeColIndex = c;
                    loadDetail(item.type, item.id);
                    m_screen = Screen::DETAIL;
                    return;
                }
            }
        }
        break;
    }

    case Screen::SEARCH: {
        if (m_loadingSearch) {
            if (x >= 170 && x <= 250 && y >= 50 && y <= 75) {
                m_screen = Screen::HOME;
            }
            return;
        }

        // Header buttons
        if (y >= 50 && y <= 75) {
            if (x >= 40 && x <= 160) {
                std::string query;
                openSwkbd(query, "Search movies & series");
                if (!query.empty()) {
                    m_searchQuery = query;
                    performSearch(m_searchQuery);
                    m_screen = Screen::SEARCH;
                }
                return;
            }
            if (x >= 170 && x <= 250) {
                m_screen = Screen::HOME;
                return;
            }
            if (x >= 1040 && x <= 1240) {
                m_searchSort = (m_searchSort == SearchSort::YEAR_DESC) ? SearchSort::DEFAULT : SearchSort::YEAR_DESC;
                {
                    std::lock_guard<std::mutex> lock(m_searchMutex);
                    sortSearchResults();
                }
                m_searchIndex = 0;
                return;
            }
        }

        // List items
        std::vector<MetaItem> localResults;
        {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            localResults = m_searchResults;
        }
        if (localResults.empty()) return;

        int maxVisible = (SCREEN_H - 100) / 50;
        int startIdx = 0;
        if (m_searchIndex >= maxVisible) startIdx = m_searchIndex - maxVisible + 1;

        int itemY = 90;
        for (int i = startIdx; i < (int)localResults.size() && i < startIdx + maxVisible; i++) {
            if (x >= 30 && x <= SCREEN_W - 30 && y >= itemY - 2 && y <= itemY + 44) {
                m_searchIndex = i;
                loadDetail(localResults[i].type, localResults[i].id);
                m_screen = Screen::DETAIL;
                return;
            }
            itemY += 50;
        }
        break;
    }

    case Screen::DETAIL: {
        if (m_loadingDetail) return;

        // Bottom Bar prompts
        if (y >= SCREEN_H - 45) {
            bool hasEpisodes = false;
            bool inEpisodes = false;
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                hasEpisodes = !m_detailEpisodes.empty();
                inEpisodes = (m_detailFocus == DetailFocus::EPISODES);
            }
            if (x < SCREEN_W / 2 - 40) {
                if (hasEpisodes && inEpisodes) {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailFocus = DetailFocus::STREAMS;
                } else {
                    Stream toPlay;
                    bool hasStream = false;
                    {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        if (m_detailStreamIndex >= 0 && m_detailStreamIndex < (int)m_detailStreams.size()) {
                            toPlay = m_detailStreams[m_detailStreamIndex];
                            hasStream = true;
                        }
                    }
                    if (hasStream) playStream(toPlay);
                }
                return;
            } else if (x >= SCREEN_W / 2 - 40 && x <= SCREEN_W / 2 + 50) {
                if (hasEpisodes && !inEpisodes) {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailFocus = DetailFocus::EPISODES;
                } else {
                    m_screen = Screen::HOME;
                }
                return;
            } else {
                m_library.toggleBookmark(m_detailMeta.id, m_detailMeta.type,
                                          m_detailMeta.name, m_detailMeta.poster);
                m_library.save(LIB_FILE);
                return;
            }
        }

        // Poster tap: Bookmark
        if (x >= 55 && x <= 325 && y >= 80 && y <= 485) {
            m_library.toggleBookmark(m_detailMeta.id, m_detailMeta.type,
                                      m_detailMeta.name, m_detailMeta.poster);
            m_library.save(LIB_FILE);
            return;
        }

        bool isLoading = false;
        std::vector<Stream> localStreams;
        int currentSelIndex = 0;
        int currentEpIndex = 0;
        std::vector<Video> localEpisodes;
        std::vector<int> localSeasons;
        int currentSeasonFilter = 0;
        {
            std::lock_guard<std::mutex> lock(m_streamsMutex);
            isLoading = m_loadingStreams;
            localStreams = m_detailStreams;
            currentSelIndex = m_detailStreamIndex;
            currentEpIndex = m_detailEpisodeIndex;
            localEpisodes = m_detailEpisodes;
            localSeasons = m_detailSeasons;
            currentSeasonFilter = m_detailSeasonFilter;
        }

        std::vector<Video> currentSeasonEps;
        if (!localSeasons.empty() && !localEpisodes.empty()) {
            int targetSeason = localSeasons[currentSeasonFilter];
            for (const auto& ep : localEpisodes) {
                if (ep.season == targetSeason) {
                    currentSeasonEps.push_back(ep);
                }
            }
        } else {
            currentSeasonEps = localEpisodes;
        }

        // Season pill tap
        if (!localSeasons.empty() && x >= 355 && x <= 560 && y >= 280 && y <= 330) {
            std::string selectedEpId;
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                m_detailSeasonFilter = (m_detailSeasonFilter + 1) % localSeasons.size();
                m_detailEpisodeIndex = 0;
                int targetSeason = localSeasons[m_detailSeasonFilter];
                for (const auto& ep : localEpisodes) {
                    if (ep.season == targetSeason) {
                        selectedEpId = ep.id;
                        break;
                    }
                }
            }
            if (!selectedEpId.empty()) {
                loadEpisodeStreams(selectedEpId);
            }
            return;
        }

        // Middle column episode cards tap
        if (!currentSeasonEps.empty() && x >= 355 && x <= 815 && y >= 325 && y <= 620) {
            int epListY = 330;
            int epItemH = 36;
            int epGap = 6;
            int maxVisibleEps = (615 - epListY) / (epItemH + epGap);
            if (maxVisibleEps < 3) maxVisibleEps = 3;

            int startEp = 0;
            if (currentEpIndex >= maxVisibleEps / 2) {
                startEp = currentEpIndex - maxVisibleEps / 2;
            }
            if (startEp + maxVisibleEps > (int)currentSeasonEps.size()) {
                startEp = (int)currentSeasonEps.size() - maxVisibleEps;
            }
            if (startEp < 0) startEp = 0;

            int clickedIndex = startEp + (y - epListY) / (epItemH + epGap);
            if (clickedIndex >= 0 && clickedIndex < (int)currentSeasonEps.size()) {
                std::string epId = currentSeasonEps[clickedIndex].id;
                {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailEpisodeIndex = clickedIndex;
                    m_detailFocus = DetailFocus::STREAMS;
                }
                if (epId != m_currentPlayingEpisodeId) {
                    loadEpisodeStreams(epId);
                }
                return;
            }
        }

        // Right column Streams panel tap
        if (x >= 845 && x <= 1230 && y >= 140 && y <= 620) {
            if (localStreams.empty() || isLoading) return;

            int streamStartY = 142;
            int cardH = 68;
            int cardGap = 10;
            int maxVisibleStreams = (540 - 74) / (cardH + cardGap);
            if (maxVisibleStreams < 3) maxVisibleStreams = 3;

            int startStreamIdx = 0;
            if (currentSelIndex >= maxVisibleStreams) {
                startStreamIdx = currentSelIndex - maxVisibleStreams + 1;
            }

            int clickedStreamIdx = startStreamIdx + (y - streamStartY) / (cardH + cardGap);
            if (clickedStreamIdx >= 0 && clickedStreamIdx < (int)localStreams.size()) {
                {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailStreamIndex = clickedStreamIdx;
                    m_detailFocus = DetailFocus::STREAMS;
                }
                playStream(localStreams[clickedStreamIdx]);
                return;
            }
        }
        break;
    }

    case Screen::LIBRARY: {
        if (x >= 40 && x <= 120 && y >= 50 && y <= 75) {
            m_screen = Screen::HOME;
            return;
        }

        auto items = m_library.getRecentlyWatched(20);
        if (items.empty()) return;

        int itemY = 90;
        for (int i = 0; i < (int)items.size() && itemY < SCREEN_H - 60; i++) {
            if (x >= 30 && x <= SCREEN_W - 30 && y >= itemY - 2 && y <= itemY + 44) {
                m_libraryIndex = i;
                loadDetail(items[i].type, items[i].id);
                m_screen = Screen::DETAIL;
                return;
            }
            itemY += 50;
        }
        break;
    }

    case Screen::ADDONS: {
        if (x >= 1100 && x <= 1220 && y >= 50 && y <= 75) {
            m_screen = Screen::HOME;
            loadHomeCatalogs(true);
            return;
        }

        if (y >= 50 && y <= 75) {
            if (x >= 600 && x <= 750) {
                std::string url;
                openSwkbd(url, "Enter addon manifest URL");
                if (!url.empty()) {
                    m_addonManager.installAddon(url);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    loadHomeCatalogs(true);
                }
                return;
            }
        }

        // Left pane: Installed Addons
        if (x >= 40 && x <= 620 && y >= 80 && y <= 600) {
            m_addonDiscoverPane = false;
            auto addons = m_addonManager.getAddons();
            if (addons.empty()) return;

            int maxVisible = 7;
            int startIndex = 0;
            if (m_addonIndex >= maxVisible) {
                startIndex = m_addonIndex - maxVisible + 1;
            }

            int itemY = 80 + 45;
            for (int i = startIndex; i < (int)addons.size() && (itemY + 60) <= (80 + 520 - 20); i++) {
                if (x >= 45 && x <= 615 && y >= itemY - 2 && y <= itemY + 54) {
                    m_addonIndex = i;
                    m_addonManager.toggleAddon(addons[i].manifest.id);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    loadHomeCatalogs(true);
                    return;
                }
                itemY += 60;
            }
        }
        // Right pane: Discover Addons
        else if (x >= 660 && x <= 1240 && y >= 80 && y <= 600) {
            m_addonDiscoverPane = true;
            int maxVisible = 7;
            int startIndex = 0;
            if (m_addonDiscoverIndex >= maxVisible) {
                startIndex = m_addonDiscoverIndex - maxVisible + 1;
            }

            int itemY = 80 + 45;
            for (int i = startIndex; i < (int)DISCOVER_ADDONS.size() && (itemY + 60) <= (80 + 520 - 20); i++) {
                if (x >= 665 && x <= 1235 && y >= itemY - 2 && y <= itemY + 54) {
                    m_addonDiscoverIndex = i;
                    m_addonManager.installAddon(DISCOVER_ADDONS[i].url);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    loadHomeCatalogs(true);
                    return;
                }
                itemY += 60;
            }
        }
        break;
    }

    case Screen::SETTINGS: {
        if (x >= 40 && x <= 120 && y >= 20 && y <= 75) {
            m_screen = Screen::HOME;
            return;
        }

        for (int i = 0; i < 6; i++) {
            int itemMinY = 99 + i * 88;
            int itemMaxY = itemMinY + 76;
            if (x >= 55 && x <= 1225 && y >= itemMinY && y <= itemMaxY) {
                m_settingsIndex = i;
                if (m_settingsIndex == 0) {
                    m_addonManager.setEnableTorrents(!m_addonManager.getEnableTorrents());
                    m_addonManager.saveConfig(CONFIG_FILE);
                } else if (m_settingsIndex == 1) {
                    std::string host = m_addonManager.getTorrServerHost();
                    openSwkbd(host, "Enter TorrServer URL (e.g. http://192.168.1.100:8090)");
                    if (!host.empty()) {
                        m_addonManager.setTorrServerHost(host);
                        m_addonManager.saveConfig(CONFIG_FILE);
                    }
                } else if (m_settingsIndex == 2) {
                    bool enabled = !m_addonManager.getHwDecode();
                    m_addonManager.setHwDecode(enabled);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    m_player.setHwDec(enabled);
                } else if (m_settingsIndex == 3) {
                    std::string lang = m_addonManager.getSubtitleLang();
                    openSwkbd(lang, "Enter Subtitle Language Code (e.g. en, es, fr)");
                    if (!lang.empty()) {
                        m_addonManager.setSubtitleLang(lang);
                        m_addonManager.saveConfig(CONFIG_FILE);
                    }
                } else if (m_settingsIndex == 4) {
                    cleanCatalogImageCache();
                    std::remove(CONFIG_FILE);
                    std::remove(LIB_FILE);
                    m_library = Library();
                    m_addonManager.loadConfig(CONFIG_FILE);
                    if (m_addonManager.getAddons().empty()) {
                        const std::vector<std::string> defaultAddons = {
                            "https://v3-cinemeta.strem.io/manifest.json",
                            "https://torrentio.strem.fun/manifest.json",
                            "https://ce8c71dcef3b-filtorrent.baby-beamup.club/manifest.json",
                            "https://dramayo.stream/manifest.json",
                            "https://cyberflix.elfhosted.com/manifest.json",
                            "https://yastream.tamthai.de/manifest.json",
                            "https://83e20802dcf1-kdramacrush.baby-beamup.club/manifest.json",
                            "https://free.flixnest.app/manifest.json",
                            "https://opensubtitles-v3.strem.io/manifest.json",
                            "https://watchhub.strem.io/manifest.json",
                            "https://badboysxs-morpheus.hf.space/manifest.json",
                            "https://stremio.yukistreams.xyz/manifest.json",
                            "https://sword-watch.vercel.app/manifest.json",
                            "https://nagare.nexioapp.org/manifest.json"
                        };
                        for (const auto& url : defaultAddons) {
                            m_addonManager.installAddon(url);
                        }
                        m_addonManager.saveConfig(CONFIG_FILE);
                    }
                    {
                        std::lock_guard<std::mutex> lock(m_homeMutex);
                        m_homeCatalogs.clear();
                    }
                    m_settingsIndex = 0;
                    m_screen = Screen::HOME;
                    loadHomeCatalogs(true);
                } else if (m_settingsIndex == 5) {
                    m_screen = Screen::HOME;
                }
                return;
            }
        }
        break;
    }

    case Screen::PLAYER: {
        double pos = m_player.getPosition();
        bool isPlayerErr = m_torrentFailed || m_player.hasError() ||
                           (TorrentStream::instance().isActive() == false && !TorrentStream::instance().isOpening() && !m_lastPlayingMagnet.empty());
        if (isPlayerErr) {
            printf("[Touch] Tap on player error screen: returning to DETAIL\n");
            m_torrentBuffering = false;
            m_torrentFailed = false;
            m_lastPlayingMagnet.clear();
            {
                std::lock_guard<std::mutex> lock(m_torrentMutex);
                m_torrentPollingActive = false;
                m_pendingTorrentPlay = false;
            }
            if (m_torrentPollingThread.joinable()) {
                m_torrentPollingThread.join();
            }
            m_player.stop();
            m_screen = Screen::DETAIL;
            return;
        }

        if (pos <= 0.01 || m_torrentBuffering) {
            // Loading screen: back button touch handler
            int cardY = SCREEN_H/2 - 170;
            int cardH = 340;
            int btnW = 240;
            int btnH = 36;
            int btnX = SCREEN_W/2 - btnW/2;
            int btnY = cardY + cardH - 55;
            if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
                m_torrentBuffering = false;
                m_lastPlayingMagnet.clear();
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentPollingActive = false;
                    m_pendingTorrentPlay = false;
                }
                if (m_torrentPollingThread.joinable()) {
                    m_torrentPollingThread.join();
                }
                m_player.stop();
                m_screen = Screen::DETAIL;
            }
            return;
        }

        uint32_t now = SDL_GetTicks();

        // 1. Check Subtitle / Audio list overlays touch events first
        if (m_showSubList) {
            int subW = 560;
            int subH = 430;
            int subX = SCREEN_W/2 - subW/2;
            int subY = SCREEN_H/2 - subH/2;

            if (x < subX || x > subX + subW || y < subY || y > subY + subH) {
                // Click outside closes dialog
                if (m_subAddonMode) {
                    cancelAddonSubtitle();
                } else {
                    m_showSubList = false;
                }
                m_osdShowTime = now;
                return;
            }

            if (m_subAddonMode) {
                if (m_subAddonLoading.load()) {
                    cancelAddonSubtitle();
                    m_osdShowTime = now;
                    return;
                }

                std::vector<Subtitle> localSubs;
                {
                    std::lock_guard<std::mutex> lock(m_subMutex);
                    localSubs = m_addonSubtitles;
                }

                if (localSubs.empty()) {
                    cancelAddonSubtitle();
                    m_osdShowTime = now;
                    return;
                }

                int maxVis = 6;
                int startIdx = 0;
                if (m_subAddonIndex >= maxVis) startIdx = m_subAddonIndex - maxVis + 1;

                int rowY = subY + 80;
                for (int i = startIdx; i < (int)localSubs.size() && i < startIdx + maxVis; i++) {
                    if (x >= subX + 24 && x <= subX + subW - 24 && y >= rowY && y <= rowY + 44) {
                        m_subAddonIndex = i;
                        applyAddonSubtitle(localSubs[i]);
                        m_osdShowTime = now;
                        return;
                    }
                    rowY += 50;
                }
            } else {
                // Option 0: Add subtitle from addon button
                int btnAddY = subY + 58;
                if (x >= subX + 24 && x <= subX + subW - 24 && y >= btnAddY && y <= btnAddY + 44) {
                    fetchAddonSubtitles();
                    m_osdShowTime = now;
                    return;
                }

                auto tracks = m_player.getSubtitleTracks();
                int maxVis = 5;
                int startIdx = 0;
                if (m_subListIndex > 0) {
                    int trackSelected = m_subListIndex - 1;
                    if (trackSelected >= maxVis) startIdx = trackSelected - maxVis + 1;
                }

                int rowY = subY + 138;
                for (int i = startIdx; i < (int)tracks.size() && i < startIdx + maxVis; i++) {
                    if (x >= subX + 24 && x <= subX + subW - 24 && y >= rowY && y <= rowY + 42) {
                        m_subListIndex = 1 + i;
                        m_player.setSubtitleTrack(tracks[i].id);
                        m_showSubList = false;
                        printf("[Touch] Subtitle track %d selected via click\n", tracks[i].id);
                        m_osdShowTime = now;
                        return;
                    }
                    rowY += 48;
                }
            }
            return;
        }

        if (m_showAudioList) {
            int audW = 480;
            int audH = 400;
            int audX = SCREEN_W/2 - audW/2;
            int audY = SCREEN_H/2 - audH/2;

            if (x >= audX && x <= audX + audW && y >= audY && y <= audY + audH) {
                auto tracks = m_player.getAudioTracks();
                int maxVis = 6;
                int startIdx = 0;
                if (m_audioListIndex >= maxVis) startIdx = m_audioListIndex - maxVis + 1;
                
                int rowY = audY + 65;
                for (int i = startIdx; i < (int)tracks.size() && i < startIdx + maxVis; i++) {
                    if (x >= audX + 20 && x <= audX + audW - 20 && y >= rowY && y <= rowY + 42) {
                        m_player.setAudioTrack(tracks[i].id);
                        m_showAudioList = false;
                        printf("[Touch] Audio track %d selected via click\n", tracks[i].id);
                        m_osdShowTime = now;
                        return;
                    }
                    rowY += 48;
                }
            } else {
                // Click outside closes the dialog
                m_showAudioList = false;
                m_osdShowTime = now;
            }
            return;
        }

        if (m_showQualityList) {
            int qualW = 640;
            int qualH = 420;
            int qualX = SCREEN_W/2 - qualW/2;
            int qualY = SCREEN_H/2 - qualH/2;

            if (x >= qualX && x <= qualX + qualW && y >= qualY && y <= qualY + qualH) {
                std::vector<Stream> localStreams;
                {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    localStreams = m_detailStreams;
                }
                int maxVis = 7;
                int startIdx = 0;
                if (m_qualityListIndex >= maxVis) startIdx = m_qualityListIndex - maxVis + 1;

                int rowY = qualY + 65;
                for (int i = startIdx; i < (int)localStreams.size() && i < startIdx + maxVis; i++) {
                    if (x >= qualX + 20 && x <= qualX + qualW - 20 && y >= rowY && y <= rowY + 42) {
                        m_qualityListIndex = i;
                        m_showQualityList = false;
                        printf("[Touch] Quality stream %d selected\n", i);
                        playStream(localStreams[i]);
                        m_osdShowTime = now;
                        return;
                    }
                    rowY += 48;
                }
            } else {
                // Click outside closes quality list
                m_showQualityList = false;
                m_osdShowTime = now;
            }
            return;
        }

        // Check if user tapped Cancel / Back during loading or buffering
        if (m_player.getPosition() <= 0.01 || m_torrentBuffering) {
            int cardY = SCREEN_H/2 - 170;
            int cardH = 340;
            int btnW = 240;
            int btnH = 36;
            int btnX = SCREEN_W/2 - btnW/2;
            int btnY = cardY + cardH - 55;
            if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
                printf("[Touch] Cancel / Back tapped during stream loading\n");
                m_torrentBuffering = false;
                m_lastPlayingMagnet.clear();
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentPollingActive = false;
                }
                m_player.stop();
                m_screen = Screen::DETAIL;
                return;
            }
        }

        // 2. Check Double Tap
        if (now - m_lastTapTime < 350) {
            // Double tap detected!
            m_lastTapTime = 0; // prevent triple tap
            if (x < SCREEN_W / 3) {
                m_player.seek(-10.0);
                printf("[Touch] Double Tap: Seek -10s\n");
            } else if (x > 2 * SCREEN_W / 3) {
                m_player.seek(10.0);
                printf("[Touch] Double Tap: Seek +10s\n");
            } else {
                m_player.togglePlay();
                printf("[Touch] Double Tap: Play/Pause\n");
            }
            m_osdShowTime = now;
            return;
        }
        m_lastTapTime = now;

        // 3. Normal OSD wake up check
        bool wasOsdVisible = (now - m_osdShowTime < 4000);
        m_osdShowTime = now;
        if (!wasOsdVisible) {
            return;
        }

        int panelW = 1000;
        int panelH = 120;
        int panelX = (SCREEN_W - panelW) / 2;
        int panelY = SCREEN_H - panelH - 30;
        int ctrlY = panelY + 56;

        int btnBackW = 60;
        int btnSeekLW = 80;
        int btnPlayW = 110;
        int btnSeekRW = 80;
        int btnSubW = 60;
        int btnAudW = 60;
        int btnQualW = 60;
        int btnGap = 20;

        int totalRowW = btnBackW + btnSeekLW + btnPlayW + btnSeekRW + btnSubW + btnAudW + btnQualW + (6 * btnGap);
        int rowStartX = panelX + (panelW - totalRowW) / 2;

        int backX = rowStartX;
        int seekLX = backX + btnBackW + btnGap;
        int pillX = seekLX + btnSeekLW + btnGap;
        int seekRX = pillX + btnPlayW + btnGap;
        int utilX = seekRX + btnSeekRW + btnGap;
        int audX = utilX + btnSubW + btnGap;
        int qualX = audX + btnAudW + btnGap;

        int barX = panelX + 30;
        int barY = panelY + 26;
        int barW = panelW - 60;

        if (y >= barY - 15 && y <= barY + 25 && x >= barX && x <= barX + barW) {
            double duration = m_player.getDuration();
            if (duration > 0.0) {
                double pct = (double)(x - barX) / barW;
                if (pct < 0.0) pct = 0.0;
                if (pct > 1.0) pct = 1.0;
                m_player.seekAbsolute(pct * duration);
            }
            return;
        }

        if (y >= ctrlY && y <= ctrlY + 30) {
            if (x >= backX && x <= backX + btnBackW) {
                m_torrentBuffering = false;
                m_lastPlayingMagnet.clear();
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentPollingActive = false;
                }
                m_player.stop();
                m_screen = Screen::DETAIL;
                return;
            }
            else if (x >= seekLX && x <= seekLX + btnSeekLW) {
                m_player.seek(-10.0);
            }
            else if (x >= pillX && x <= pillX + btnPlayW) {
                m_player.togglePlay();
            }
            else if (x >= seekRX && x <= seekRX + btnSeekRW) {
                m_player.seek(10.0);
            }
            else if (x >= utilX && x <= utilX + btnSubW) {
                m_showSubList = true;
                m_subListIndex = 0;
                auto tracks = m_player.getSubtitleTracks();
                for (int i = 0; i < (int)tracks.size(); i++) {
                    if (tracks[i].selected) {
                        m_subListIndex = i;
                        break;
                    }
                }
                printf("[Touch] Subtitle menu opened\n");
            }
            else if (x >= audX && x <= audX + btnAudW) {
                m_showAudioList = true;
                m_audioListIndex = 0;
                auto tracks = m_player.getAudioTracks();
                for (int i = 0; i < (int)tracks.size(); i++) {
                    if (tracks[i].selected) {
                        m_audioListIndex = i;
                        break;
                    }
                }
                printf("[Touch] Audio menu opened\n");
            }
            else if (x >= qualX && x <= qualX + btnQualW) {
                m_showQualityList = true;
                m_qualityListIndex = m_detailStreamIndex;
                printf("[Touch] Quality menu opened\n");
            } else {
                m_osdShowTime = 0; // Tapped in button row but not on a button
            }
        } else {
            m_osdShowTime = 0; // Tapped completely outside scrubber and buttons
        }
        break;
    }

    default:
        break;
    }
}

void App::render() {
    // Process any downloaded poster images from background threads (must be done on the main thread)
    std::vector<DownloadedImage> localQueue;
    {
        std::lock_guard<std::mutex> lock(m_downloadedMutex);
        if (!m_downloadedQueue.empty()) {
            localQueue = std::move(m_downloadedQueue);
            m_downloadedQueue.clear();
        }
    }
    for (const auto& img : localQueue) {
        SDL_Texture* tex = m_imageCache->store(img.url, img.data.data(), img.data.size());
        if (tex) {
            if (!img.titleKey.empty()) {
                m_imageCache->addAlias(img.titleKey, img.url);
            }
        } else {
            std::lock_guard<std::mutex> lock(m_downloadedMutex);
            if (!img.titleKey.empty()) m_failedPosters.insert(img.titleKey);
            m_failedPosters.insert(img.url);
            std::string cacheFile = getDiskCachePath(img.url);
            if (!cacheFile.empty()) {
                std::remove(cacheFile.c_str());
            }
            if (!img.titleKey.empty()) {
                std::string titleCache = getDiskCachePath(img.titleKey);
                if (!titleCache.empty()) std::remove(titleCache.c_str());
            }
        }
    }

    if (m_screen == Screen::PLAYER) {
        renderPlayer();
        return;
    }

    SDL_SetRenderDrawColor(m_renderer, BG_COLOR.r, BG_COLOR.g, BG_COLOR.b, 255);
    SDL_RenderClear(m_renderer);

    switch (m_screen) {
    case Screen::HOME:     renderHome(); break;
    case Screen::SEARCH:   renderSearch(); break;
    case Screen::DETAIL:   renderDetail(); break;
    case Screen::LIBRARY:  renderLibrary(); break;
    case Screen::ADDONS:   renderAddons(); break;
    case Screen::SETTINGS: renderSettings(); break;
    default:               renderHome(); break;
    }

    drawNavBar();
    SDL_RenderPresent(m_renderer);
}

static std::string formatTime(double seconds) {
    if (seconds < 0.0) seconds = 0.0;
    int h = (int)(seconds / 3600);
    int m = (int)((seconds - h * 3600) / 60);
    int s = (int)seconds % 60;
    char buf[64];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", m, s);
    }
    return buf;
}

// Helper to directly resolve an IMDB ID from Cinemeta via a single, lightweight HTTP query
static std::string resolveImdbIdFromCinemeta(HttpClient& http, const std::string& type, const std::string& title) {
    if (title.empty()) return "";

    std::string cleanTitle = title;
    size_t paren = cleanTitle.find('(');
    if (paren != std::string::npos && paren > 0) {
        cleanTitle = cleanTitle.substr(0, paren);
    }
    while (!cleanTitle.empty() && (cleanTitle.back() == ' ' || cleanTitle.back() == '\t')) {
        cleanTitle.pop_back();
    }
    while (!cleanTitle.empty() && (cleanTitle.front() == ' ' || cleanTitle.front() == '\t')) {
        cleanTitle.erase(cleanTitle.begin());
    }
    if (cleanTitle.empty()) cleanTitle = title;

    std::string encoded;
    for (char c : cleanTitle) {
        if (c == ' ') encoded += "%20";
        else if (c == '&') encoded += "%26";
        else if (c == '=') encoded += "%3D";
        else if (c == '/') encoded += "%2F";
        else encoded += c;
    }

    std::string catType = (type == "movie") ? "movie" : "series";
    std::string url = "https://v3-cinemeta.strem.io/catalog/" + catType + "/top/search=" + encoded + ".json";

    auto resp = http.get(url, 15);
    if (!resp.ok() || resp.body.empty()) return "";

    rapidjson::Document doc;
    doc.Parse(resp.body.c_str());
    if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("metas") && doc["metas"].IsArray()) {
        for (auto& m : doc["metas"].GetArray()) {
            if (m.IsObject() && m.HasMember("id") && m["id"].IsString()) {
                std::string resId = m["id"].GetString();
                if (resId.rfind("tt", 0) == 0) {
                    return resId;
                }
            }
        }
    }

    return "";
}

void App::fetchAddonSubtitles() {
    m_wasPlayingBeforeSubSearch = !m_player.isPaused();
    m_player.pause();
    m_subAddonMode = true;
    m_subAddonLoading = true;
    {
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_addonSubtitles.clear();
    }
    m_subAddonIndex = 0;

    if (m_subSearchThread.joinable()) {
        m_subSearchThread.join();
    }

    int currentGen = ++m_subSearchGen;
    std::string type = m_currentPlayingType.empty() ? m_detailMeta.type : m_currentPlayingType;
    std::string id = m_currentPlayingId.empty() ? m_detailMeta.id : m_currentPlayingId;
    std::string title = m_detailMeta.name;

    m_subSearchThread = std::thread([this, type, id, title, currentGen]() {
        printf("[Subtitles] Fetching addon subtitles for type='%s', id='%s', title='%s'\n",
               type.c_str(), id.c_str(), title.c_str());

        std::vector<Subtitle> subs;

        // If ID already starts with "tt", query subtitle addons directly
        if (id.rfind("tt", 0) == 0) {
            subs = m_addonManager.getAllSubtitles(type, id);
        } else {
            // Non-IMDb ID (e.g. "kisskh:...", "kitsu:..."):
            // OpenSubtitles requires IMDb IDs. Resolve via Cinemeta!
            std::string imdbId = resolveImdbIdFromCinemeta(m_http, type, title);
            if (!imdbId.empty()) {
                std::string targetId = imdbId;
                if (type == "series") {
                    int s = 1, e = 1;
                    if (!m_detailEpisodes.empty() && m_detailEpisodeIndex >= 0 && m_detailEpisodeIndex < (int)m_detailEpisodes.size()) {
                        s = m_detailEpisodes[m_detailEpisodeIndex].season;
                        e = m_detailEpisodes[m_detailEpisodeIndex].episode;
                        if (s <= 0) s = 1;
                        if (e <= 0) e = 1;
                    }
                    targetId += ":" + std::to_string(s) + ":" + std::to_string(e);
                }
                printf("[Subtitles] Resolved IMDB ID for '%s': '%s', querying OpenSubtitles...\n",
                       title.c_str(), targetId.c_str());
                subs = m_addonManager.getAllSubtitles(type, targetId);
            }

            // If still empty, check other addons with original ID
            if (subs.empty()) {
                subs = m_addonManager.getAllSubtitles(type, id);
            }
        }

        if (m_subSearchGen.load() == currentGen) {
            std::lock_guard<std::mutex> lock(m_subMutex);
            m_addonSubtitles = std::move(subs);
            m_subAddonLoading = false;
            printf("[Subtitles] Found %zu addon subtitles\n", m_addonSubtitles.size());
        }
    });
}

void App::applyAddonSubtitle(const Subtitle& sub) {
    printf("[Subtitles] Downloading & applying subtitle: %s (url: %s)\n",
           sub.lang.c_str(), sub.url.c_str());

    std::string subDir;
#ifdef __SWITCH__
    subDir = "sdmc:/switch/switchstream";
#else
    subDir = "switchstream_data";
#endif
    std::string subPath = subDir + "/addon_sub.srt";

    auto resp = m_http.get(sub.url, 20);
    if (resp.ok() && !resp.body.empty()) {
        FILE* f = fopen(subPath.c_str(), "wb");
        if (f) {
            fwrite(resp.body.data(), 1, resp.body.size(), f);
            fclose(f);
            m_player.addSubtitle(subPath, sub.lang);
        } else {
            m_player.addSubtitle(sub.url, sub.lang);
        }
    } else {
        m_player.addSubtitle(sub.url, sub.lang);
    }

    m_subAddonMode = false;
    m_showSubList = false;
    if (m_wasPlayingBeforeSubSearch) {
        m_player.resume();
    }
}

void App::cancelAddonSubtitle() {
    m_subSearchGen++;
    m_subAddonLoading = false;
    m_subAddonMode = false;
    m_showSubList = false;
    if (m_wasPlayingBeforeSubSearch) {
        m_player.resume();
    }
}

static void drawStar(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    const int numPoints = 10;
    SDL_Point pts[11];
    double rInner = radius * 0.42;
    double angleStep = M_PI / 5.0;
    double startAngle = -M_PI / 2.0;

    for (int i = 0; i < numPoints; i++) {
        double r = (i % 2 == 0) ? (double)radius : rInner;
        double a = startAngle + i * angleStep;
        pts[i].x = cx + (int)(r * cos(a) + 0.5);
        pts[i].y = cy + (int)(r * sin(a) + 0.5);
    }
    pts[10] = pts[0];

    int minY = cy - radius, maxY = cy + radius;
    for (int y = minY; y <= maxY; y++) {
        int nodeX[10];
        int nodes = 0;
        for (int i = 0; i < numPoints; i++) {
            int j = (i + 1) % numPoints;
            if ((pts[i].y < y && pts[j].y >= y) || (pts[j].y < y && pts[i].y >= y)) {
                if (pts[j].y != pts[i].y) {
                    nodeX[nodes++] = pts[i].x + (int)((double)(y - pts[i].y) / (pts[j].y - pts[i].y) * (pts[j].x - pts[i].x));
                }
            }
        }
        for (int i = 0; i < nodes - 1; i++) {
            for (int j = i + 1; j < nodes; j++) {
                if (nodeX[i] > nodeX[j]) {
                    int tmp = nodeX[i];
                    nodeX[i] = nodeX[j];
                    nodeX[j] = tmp;
                }
            }
        }
        for (int i = 0; i < nodes; i += 2) {
            if (i + 1 < nodes) {
                SDL_RenderDrawLine(renderer, nodeX[i], y, nodeX[i + 1], y);
            }
        }
    }
}

struct StreamCardDisplay {
    std::string line1;
    std::string line2;
};

static StreamCardDisplay formatStreamCard(const Stream& s) {
    std::string sizeStr;
    std::string seedersStr;
    std::string providerStr;
    std::string qualityStr;
    std::string cleanNameStr;

    // 1. Parse s.name (often "AddonName\nQuality" or "Quality")
    std::vector<std::string> nameLines;
    {
        std::stringstream ss(s.name);
        std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            while (!line.empty() && line.front() == ' ') line.erase(line.begin());
            if (!line.empty()) nameLines.push_back(line);
        }
    }

    std::string addonFromField;
    if (!nameLines.empty()) {
        addonFromField = nameLines[0];
        if (addonFromField.size() >= 2 && addonFromField.front() == '[' && addonFromField.back() == ']') {
            addonFromField = addonFromField.substr(1, addonFromField.size() - 2);
        }
        if (nameLines.size() > 1) {
            qualityStr = nameLines[1];
        } else {
            if (nameLines[0].find("1080p") != std::string::npos ||
                nameLines[0].find("720p") != std::string::npos ||
                nameLines[0].find("4k") != std::string::npos ||
                nameLines[0].find("4K") != std::string::npos ||
                nameLines[0].find("2160p") != std::string::npos ||
                nameLines[0].find("480p") != std::string::npos) {
                qualityStr = nameLines[0];
                addonFromField.clear();
            }
        }
    }

    // 2. Parse s.title
    std::vector<std::string> titleLines;
    {
        std::stringstream ss(s.title);
        std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            while (!line.empty() && line.front() == ' ') line.erase(line.begin());
            if (!line.empty()) titleLines.push_back(line);
        }
    }

    for (const auto& line : titleLines) {
        size_t seedPos = line.find("\xf0\x9f\x91\xa4");
        if (seedPos != std::string::npos) {
            std::string sub = line.substr(seedPos + 4);
            size_t i = 0;
            while (i < sub.size() && (sub[i] == ' ' || sub[i] == ':')) i++;
            std::string num;
            while (i < sub.size() && isdigit((unsigned char)sub[i])) {
                num += sub[i++];
            }
            if (!num.empty()) seedersStr = num + " Seeds";
        } else {
            std::regex seedRegex(R"((?:seeds?|seeders?|S:)\s*[:]?\s*(\d+))", std::regex::icase);
            std::smatch m;
            if (std::regex_search(line, m, seedRegex)) {
                seedersStr = m[1].str() + " Seeds";
            }
        }

        size_t sizePos = line.find("\xf0\x9f\x92\xbe");
        if (sizePos != std::string::npos) {
            std::string sub = line.substr(sizePos + 4);
            size_t i = 0;
            while (i < sub.size() && sub[i] == ' ') i++;
            std::string sz;
            while (i < sub.size() && (isdigit((unsigned char)sub[i]) || sub[i] == '.' || sub[i] == ' ' || isalpha((unsigned char)sub[i]))) {
                sz += sub[i++];
                if (sz.find("GB") != std::string::npos || sz.find("MB") != std::string::npos ||
                    sz.find("gb") != std::string::npos || sz.find("mb") != std::string::npos) {
                    break;
                }
            }
            while (!sz.empty() && sz.back() == ' ') sz.pop_back();
            if (!sz.empty()) sizeStr = sz;
        } else {
            std::regex sizeRegex(R"((\d+(?:\.\d+)?\s*(?:GB|MB|GiB|MiB)))", std::regex::icase);
            std::smatch m;
            if (std::regex_search(line, m, sizeRegex)) {
                sizeStr = m[1].str();
            }
        }

        size_t provPos = line.find("\xe2\x9a\x99");
        if (provPos != std::string::npos) {
            std::string sub = line.substr(provPos + 3);
            if (sub.size() >= 3 && (unsigned char)sub[0] == 0xef && (unsigned char)sub[1] == 0xb8 && (unsigned char)sub[2] == 0x8f) {
                sub = sub.substr(3);
            }
            size_t i = 0;
            while (i < sub.size() && sub[i] == ' ') i++;
            std::string prov = sub.substr(i);
            while (!prov.empty() && (prov.back() == ' ' || prov.back() == '\r')) prov.pop_back();
            if (!prov.empty()) providerStr = prov;
        }

        bool hasMeta = (seedPos != std::string::npos || sizePos != std::string::npos || provPos != std::string::npos);
        bool isFlagLine = (line.find("/") != std::string::npos && (line.find("\xf0\x9f") != std::string::npos || line.size() < 25));
        if (!hasMeta && !isFlagLine && cleanNameStr.empty()) {
            cleanNameStr = line;
        }
    }

    if (providerStr.empty()) {
        providerStr = addonFromField;
    }
    if (providerStr.empty()) {
        if (!s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0) {
            providerStr = "Torrent";
        } else {
            providerStr = "Stream";
        }
    }

    if (qualityStr.empty()) {
        std::vector<std::string> qCandidates = {"4K", "2160p", "1080p", "720p", "480p"};
        for (const auto& q : qCandidates) {
            if (cleanNameStr.find(q) != std::string::npos || s.title.find(q) != std::string::npos) {
                qualityStr = q;
                break;
            }
        }
    }

    if (s.url.find(".m3u8") != std::string::npos && qualityStr.find("HLS") == std::string::npos) {
        if (!qualityStr.empty()) qualityStr += " HLS";
        else qualityStr = "HLS";
    }

    std::string line1 = providerStr;
    if (!qualityStr.empty()) {
        line1 += " • " + qualityStr;
    }

    std::string line2;
    std::string metaPart;
    if (!seedersStr.empty()) {
        metaPart += seedersStr;
    }
    if (!sizeStr.empty()) {
        if (!metaPart.empty()) metaPart += " • ";
        metaPart += sizeStr;
    }

    if (!metaPart.empty()) {
        line2 = "(" + metaPart + ")";
    } else if (!cleanNameStr.empty()) {
        line2 = "(" + cleanNameStr + ")";
    } else {
        if (!s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0) {
            line2 = "(Torrent Stream)";
        } else {
            line2 = "(Direct Stream)";
        }
    }

    if (line1.size() > 32) line1 = line1.substr(0, 30) + "...";
    if (line2.size() > 36) line2 = line2.substr(0, 34) + "...";

    return {line1, line2};
}

static std::string formatStreamDisplay(const Stream& s) {
    std::string sizeStr;
    std::string seedersStr;
    std::string providerStr;
    std::string qualityStr;
    std::string cleanNameStr;

    // 1. Parse s.name (often "AddonName\nQuality" or "Quality")
    std::vector<std::string> nameLines;
    {
        std::stringstream ss(s.name);
        std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            while (!line.empty() && line.front() == ' ') line.erase(line.begin());
            if (!line.empty()) nameLines.push_back(line);
        }
    }

    std::string addonFromField;
    if (!nameLines.empty()) {
        addonFromField = nameLines[0];
        if (nameLines.size() > 1) {
            qualityStr = nameLines[1];
        } else {
            if (nameLines[0].find("1080p") != std::string::npos ||
                nameLines[0].find("720p") != std::string::npos ||
                nameLines[0].find("4k") != std::string::npos ||
                nameLines[0].find("4K") != std::string::npos ||
                nameLines[0].find("2160p") != std::string::npos ||
                nameLines[0].find("480p") != std::string::npos) {
                qualityStr = nameLines[0];
                addonFromField.clear();
            }
        }
    }

    // 2. Parse s.title
    std::vector<std::string> titleLines;
    {
        std::stringstream ss(s.title);
        std::string line;
        while (std::getline(ss, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            while (!line.empty() && line.front() == ' ') line.erase(line.begin());
            if (!line.empty()) titleLines.push_back(line);
        }
    }

    for (const auto& line : titleLines) {
        // Check for seeders (👤 icon: \xF0\x9F\x91\xA4 or text)
        size_t seedPos = line.find("\xf0\x9f\x91\xa4");
        if (seedPos != std::string::npos) {
            std::string sub = line.substr(seedPos + 4);
            size_t i = 0;
            while (i < sub.size() && (sub[i] == ' ' || sub[i] == ':')) i++;
            std::string num;
            while (i < sub.size() && isdigit((unsigned char)sub[i])) {
                num += sub[i++];
            }
            if (!num.empty()) seedersStr = num + " seeds";
        } else {
            std::regex seedRegex(R"((?:seeds?|seeders?|S:)\s*[:]?\s*(\d+))", std::regex::icase);
            std::smatch m;
            if (std::regex_search(line, m, seedRegex)) {
                seedersStr = m[1].str() + " seeds";
            }
        }

        // Check for size (💾 icon: \xF0\x9F\x92\xBE or text)
        size_t sizePos = line.find("\xf0\x9f\x92\xbe");
        if (sizePos != std::string::npos) {
            std::string sub = line.substr(sizePos + 4);
            size_t i = 0;
            while (i < sub.size() && sub[i] == ' ') i++;
            std::string sz;
            while (i < sub.size() && (isdigit((unsigned char)sub[i]) || sub[i] == '.' || sub[i] == ' ' || isalpha((unsigned char)sub[i]))) {
                sz += sub[i++];
                if (sz.find("GB") != std::string::npos || sz.find("MB") != std::string::npos ||
                    sz.find("gb") != std::string::npos || sz.find("mb") != std::string::npos) {
                    break;
                }
            }
            while (!sz.empty() && sz.back() == ' ') sz.pop_back();
            if (!sz.empty()) sizeStr = sz;
        } else {
            std::regex sizeRegex(R"((\d+(?:\.\d+)?\s*(?:GB|MB|GiB|MiB)))", std::regex::icase);
            std::smatch m;
            if (std::regex_search(line, m, sizeRegex)) {
                sizeStr = m[1].str();
            }
        }

        // Check for provider (⚙️ icon: \xE2\x9a\x99 or text)
        size_t provPos = line.find("\xe2\x9a\x99");
        if (provPos != std::string::npos) {
            std::string sub = line.substr(provPos + 3);
            if (sub.size() >= 3 && (unsigned char)sub[0] == 0xef && (unsigned char)sub[1] == 0xb8 && (unsigned char)sub[2] == 0x8f) {
                sub = sub.substr(3);
            }
            size_t i = 0;
            while (i < sub.size() && sub[i] == ' ') i++;
            std::string prov = sub.substr(i);
            while (!prov.empty() && (prov.back() == ' ' || prov.back() == '\r')) prov.pop_back();
            if (!prov.empty()) providerStr = prov;
        }

        bool hasMeta = (seedPos != std::string::npos || sizePos != std::string::npos || provPos != std::string::npos);
        bool isFlagLine = (line.find("/") != std::string::npos && (line.find("\xf0\x9f") != std::string::npos || line.size() < 25));
        if (!hasMeta && !isFlagLine && cleanNameStr.empty()) {
            cleanNameStr = line;
        }
    }

    if (providerStr.empty()) {
        providerStr = addonFromField;
    }

    if (cleanNameStr.empty()) {
        if (!titleLines.empty() && titleLines[0].find("\xf0\x9f\x91\xa4") == std::string::npos) {
            cleanNameStr = titleLines[0];
        } else {
            cleanNameStr = s.title;
        }
    }

    std::string nameFlat;
    for (char c : cleanNameStr) {
        if (c == '\n' || c == '\r') nameFlat += ' ';
        else nameFlat += c;
    }
    while (!nameFlat.empty() && nameFlat.back() == ' ') nameFlat.pop_back();

    std::string res;
    if (!sizeStr.empty()) {
        res += "[" + sizeStr + "] ";
    }
    if (!seedersStr.empty()) {
        res += "[" + seedersStr + "] ";
    }
    if (!providerStr.empty()) {
        res += "[" + providerStr + "] ";
    }

    std::string trailingName;
    if (!qualityStr.empty()) {
        trailingName += qualityStr;
    }
    if (!nameFlat.empty()) {
        if (!trailingName.empty()) trailingName += " — ";
        trailingName += nameFlat;
    }

    if (trailingName.empty()) {
        trailingName = "Stream";
    }

    res += trailingName;
    return res;
}

void App::renderPlayer() {
    double pos = m_player.getPosition();
    bool isNativeTorrent = TorrentStream::instance().isActive() || TorrentStream::instance().isOpening() || !m_lastPlayingMagnet.empty();
    bool hasErr = m_torrentFailed || m_player.hasError();
    if (isNativeTorrent && !TorrentStream::instance().isOpening() && !TorrentStream::instance().isActive() && !m_torrentBuffering && !m_pendingTorrentPlay) {
        hasErr = true;
    }

    std::string errMsg;
    if (m_torrentFailed) {
        errMsg = m_torrentErrorMsg.empty() ? "No seeders or peers available online" : m_torrentErrorMsg;
    } else if (m_player.hasError()) {
        errMsg = m_player.getErrorMessage().empty() ? "Playback decoding failed" : m_player.getErrorMessage();
    } else if (isNativeTorrent && !TorrentStream::instance().isOpening() && !TorrentStream::instance().isActive()) {
        errMsg = "Failed to establish torrent connection (0 peers)";
    }

    // Check if torrent prebuffering is complete
    if (isNativeTorrent && m_torrentBuffering && !hasErr) {
        double cacheSecs = m_player.getCacheDuration();
        bool cacheIdle = m_player.isCacheIdle();
        // Buffer at least 2.0 seconds before starting playback (responsive, smooth streaming)
        if (cacheSecs >= 2.0 || (cacheIdle && cacheSecs > 0.5)) {
            printf("[App] Torrent pre-buffering complete (%.1f s in cache), starting playback!\n", cacheSecs);
            m_player.resume();
            m_torrentBuffering = false;
        }
    }

    bool showLoading = hasErr || (isNativeTorrent ? m_torrentBuffering : (pos <= 0.01));

    if (showLoading) {
        // Keep the render context active to prevent libmpv warning/hang
        m_player.render(SCREEN_W, SCREEN_H);
        SDL_RenderFlush(m_renderer);

        SDL_SetRenderDrawColor(m_renderer, 10, 10, 15, 255);
        SDL_RenderClear(m_renderer);

        // Center card coordinates
        int cardX = SCREEN_W/2 - 320;
        int cardY = SCREEN_H/2 - 170;
        int cardW = 640;
        int cardH = 340;

        if (hasErr) {
            // High-visibility glassmorphic error card
            drawFilledRoundRect(cardX, cardY, cardW, cardH, 14, {25, 14, 18, 250});
            drawRoundRect(cardX, cardY, cardW, cardH, 14, {255, 75, 75, 140});

            drawTextCentered("!  UNABLE TO PLAY STREAM", SCREEN_W/2, cardY + 45, {255, 90, 90, 255}, m_fontLarge);
            drawTextCentered(errMsg, SCREEN_W/2, cardY + 105, TEXT_PRIMARY, m_fontNormal);
            drawTextCentered("No active seeders or peers responded for this torrent.", SCREEN_W/2, cardY + 145, {180, 195, 215, 255}, m_fontSmall);
            drawTextCentered("Please select a different stream provider or quality option.", SCREEN_W/2, cardY + 175, TEXT_SECONDARY, m_fontSmall);

            int btnW = 300;
            int btnH = 42;
            int btnX = SCREEN_W/2 - btnW/2;
            int btnY = cardY + cardH - 65;

            drawFilledRoundRect(btnX, btnY, btnW, btnH, 8, {220, 53, 69, 220});
            drawRoundRect(btnX, btnY, btnW, btnH, 8, {255, 255, 255, 80});
            drawTextCentered("Press [B] or [A] to Return", SCREEN_W/2, btnY + 10, {255, 255, 255, 255}, m_fontNormal);

            SDL_RenderPresent(m_renderer);
            return;
        }

        // Glassmorphic card body
        drawFilledRoundRect(cardX, cardY, cardW, cardH, 14, {15, 18, 25, 240});
        drawRoundRect(cardX, cardY, cardW, cardH, 14, {255, 255, 255, 35});

        if (isNativeTorrent) {
            auto stats = TorrentStream::instance().getStats();
            drawSpinner(SCREEN_W/2, cardY + 45, 22);

            std::string headerTitle = stats.name.empty() ? "Streaming BitTorrent" : stats.name;
            if (headerTitle.size() > 42) headerTitle = headerTitle.substr(0, 39) + "...";
            drawTextCentered(headerTitle, SCREEN_W/2, cardY + 80, TEXT_PRIMARY, m_fontLarge);

            // Progress bar
            int barW = cardW - 80;
            int barH = 10;
            int barX = cardX + 40;
            int barY = cardY + 120;

            double pct = 0.0;
            std::string statDesc;
            if (TorrentStream::instance().isOpening()) {
                statDesc = stats.statusStr;
            } else if (!m_player.isFileLoaded()) {
                statDesc = "Reading media header & index...";
            } else {
                double cacheSecs = m_player.getCacheDuration();
                pct = (cacheSecs / 2.0) * 100.0;
                if (pct > 100.0) pct = 100.0;
                if (pct < 0.0) pct = 0.0;
                char b[128];
                snprintf(b, sizeof(b), "Buffering: %.1f s / 2.0 s (%.0f%%)", cacheSecs, pct);
                statDesc = b;
            }

            drawTextCentered(statDesc, SCREEN_W/2, cardY + 145, {100, 190, 255, 255}, m_fontNormal);

            // Draw progress bar track & fill
            drawFilledRoundRect(barX, barY, barW, barH, 5, {255, 255, 255, 30});
            if (pct > 0.0) {
                int fillW = (int)((barW * pct) / 100.0);
                if (fillW < 8) fillW = 8;
                drawFilledRoundRect(barX, barY, fillW, barH, 5, ACCENT);
            }

            // Peer info & download speed
            char peerBuf[128];
            snprintf(peerBuf, sizeof(peerBuf), "Peers: %d (peak: %d)  |  Speed: %.2f MB/s",
                     stats.livePeers, stats.peakPeers, stats.speedMBps);
            drawTextCentered(peerBuf, SCREEN_W/2, cardY + 180, TEXT_SECONDARY, m_fontSmall);

            if (stats.piecesTotal > 0) {
                char pieceBuf[128];
                snprintf(pieceBuf, sizeof(pieceBuf), "Pieces: %lld / %lld  |  Total: %.1f MB",
                         (long long)stats.piecesDone, (long long)stats.piecesTotal,
                         stats.totalBytes / (1024.0 * 1024.0));
                drawTextCentered(pieceBuf, SCREEN_W/2, cardY + 210, {160, 175, 195, 255}, m_fontSmall);
            }
        } else {
            drawSpinner(SCREEN_W/2, cardY + 80, 28);
            drawTextCentered("Loading video stream...", SCREEN_W/2, cardY + 140, TEXT_PRIMARY, m_fontLarge);
        }

        // Action button pill: [B] Cancel / Back
        int btnW = 240;
        int btnH = 36;
        int btnX = SCREEN_W/2 - btnW/2;
        int btnY = cardY + cardH - 55;

        drawFilledRoundRect(btnX, btnY, btnW, btnH, 8, {255, 255, 255, 20});
        drawRoundRect(btnX, btnY, btnW, btnH, 8, {255, 255, 255, 45});
        drawTextCentered("[B] Cancel / Back", SCREEN_W/2, btnY + 8, {255, 120, 120, 255}, m_fontSmall);

        SDL_RenderPresent(m_renderer);
    } else {
        // ─── Modern Glassmorphic Player OSD & Buffering ───
        uint32_t now = SDL_GetTicks();
        bool showOsd = (now - m_osdShowTime < 4000) || m_isScrubbing || m_showSubList || m_showAudioList || m_showQualityList;
        bool buffering = m_player.isBuffering();

        // 1. Render the video frame first
        m_player.render(SCREEN_W, SCREEN_H);
        SDL_RenderFlush(m_renderer);

        // 2. Draw OSD/UI overlays on top of the video frame
        if (showOsd) {
            double duration = m_player.getDuration();
            double progress = 0.0;
            double displayPos = m_isScrubbing ? m_scrubCurrentPos : pos;
            if (duration > 0.0) progress = displayPos / duration;

            // ─── Top Info Bar (glassmorphic) ───
            int topBarH = 50;
            drawFilledRect(0, 0, SCREEN_W, topBarH, {0, 0, 0, 210});
            drawFilledRect(0, topBarH - 1, SCREEN_W, 1, {255, 255, 255, 15});

            std::string title = m_detailMeta.name;
            if (title.size() > 55) title = title.substr(0, 52) + "...";
            drawText(title, 30, 12, TEXT_PRIMARY, m_fontNormal);

            std::string timeStr = formatTime(displayPos) + " / " + formatTime(duration);
            drawText(timeStr, SCREEN_W - 200, 15, {180, 200, 220, 255}, m_fontSmall);

            if (TorrentStream::instance().isActive()) {
                auto tstats = TorrentStream::instance().getStats();
                char pbuf[64];
                snprintf(pbuf, sizeof(pbuf), "%d peers | %.2f MB/s", tstats.livePeers, tstats.speedMBps);
                drawText(pbuf, SCREEN_W - 420, 15, {100, 200, 255, 255}, m_fontSmall);
            }

            // ─── Bottom Control Panel (glassmorphic floating) ───
            int panelW = 1000;
            int panelH = 120;
            int panelX = (SCREEN_W - panelW) / 2;
            int panelY = SCREEN_H - panelH - 30;

            // Frosted glass body
            drawFilledRoundRect(panelX, panelY, panelW, panelH, 14, {0, 0, 0, 220});

            // Glass specular highlights
            drawRoundRect(panelX, panelY, panelW, panelH, 14, {255, 255, 255, 25});

            // ─── Progress Bar ───
            int barX = panelX + 30;
            int barY = panelY + 26;
            int barW = panelW - 60;
            int barH = 6;

            // Track background
            drawFilledRoundRect(barX, barY, barW, barH, 3, {255, 255, 255, 20});

            // Filled progress
            int fillW = (int)(progress * barW);
            if (fillW > 0) {
                drawFilledRoundRect(barX, barY, fillW, barH, 3, {0, 180, 255, 200});

                // Thumb/scrubber
                int thumbX = barX + fillW;
                int thumbSize = 14;
                drawFilledRoundRect(thumbX - thumbSize/2 - 2, barY - thumbSize/2 + 1, thumbSize + 4, thumbSize + 2, 8, {0, 180, 255, 50});
                drawFilledRoundRect(thumbX - thumbSize/2, barY - thumbSize/2 + 2, thumbSize, thumbSize, 7, {255, 255, 240});
            }

            // ─── Control Buttons Row ───
            int ctrlY = panelY + 56;

            int btnBackW = 60;
            int btnSeekLW = 80;
            int btnPlayW = 110;
            int btnSeekRW = 80;
            int btnSubW = 60;
            int btnAudW = 60;
            int btnQualW = 60;
            int btnGap = 20;

            int totalRowW = btnBackW + btnSeekLW + btnPlayW + btnSeekRW + btnSubW + btnAudW + btnQualW + (6 * btnGap);
            int rowStartX = panelX + (panelW - totalRowW) / 2;

            int backX = rowStartX;
            int seekLX = backX + btnBackW + btnGap;
            int pillX = seekLX + btnSeekLW + btnGap;
            int seekRX = pillX + btnPlayW + btnGap;
            int utilX = seekRX + btnSeekRW + btnGap;
            int audX = utilX + btnSubW + btnGap;
            int qualX = audX + btnAudW + btnGap;

            // BACK button
            drawFilledRoundRect(backX, ctrlY, btnBackW, 30, 8, {255, 80, 80, 20});
            drawRoundRect(backX, ctrlY, btnBackW, 30, 8, {255, 100, 100, 40});
            drawTextCentered("BACK", backX + btnBackW / 2, ctrlY + 5, {255, 130, 130, 255}, m_fontSmall);

            // -10s button
            drawFilledRoundRect(seekLX, ctrlY, btnSeekLW, 30, 8, {255, 255, 255, 15});
            drawRoundRect(seekLX, ctrlY, btnSeekLW, 30, 8, {255, 255, 255, 30});
            drawTextCentered("-10s", seekLX + btnSeekLW / 2, ctrlY + 5, {200, 200, 220, 255}, m_fontSmall);

            // Play/Pause button
            bool paused = m_player.isPaused();
            if (!paused) {
                drawFilledRoundRect(pillX - 2, ctrlY - 2, btnPlayW + 4, 34, 8, {0, 180, 255, 30});
            }
            drawFilledRoundRect(pillX, ctrlY, btnPlayW, 30, 8, paused ? SDL_Color{255, 100, 100, 60} : SDL_Color{0, 180, 255, 60});
            drawRoundRect(pillX, ctrlY, btnPlayW, 30, 8, {255, 255, 255, 60});
            drawTextCentered(paused ? "PLAY" : "PAUSE", pillX + btnPlayW / 2, ctrlY + 5, paused ? SDL_Color{255, 140, 140, 255} : SDL_Color{100, 210, 255, 255}, m_fontSmall);

            // +10s button
            drawFilledRoundRect(seekRX, ctrlY, btnSeekRW, 30, 8, {255, 255, 255, 15});
            drawRoundRect(seekRX, ctrlY, btnSeekRW, 30, 8, {255, 255, 255, 30});
            drawTextCentered("+10s", seekRX + btnSeekRW / 2, ctrlY + 5, {200, 200, 220, 255}, m_fontSmall);

            // SUB button
            drawFilledRoundRect(utilX, ctrlY, btnSubW, 30, 8, {255, 255, 255, 15});
            drawRoundRect(utilX, ctrlY, btnSubW, 30, 8, {255, 255, 255, 30});
            drawTextCentered("SUB", utilX + btnSubW / 2, ctrlY + 5, {180, 180, 200, 255}, m_fontSmall);

            // AUD button
            drawFilledRoundRect(audX, ctrlY, btnAudW, 30, 8, {255, 255, 255, 15});
            drawRoundRect(audX, ctrlY, btnAudW, 30, 8, {255, 255, 255, 30});
            drawTextCentered("AUD", audX + btnAudW / 2, ctrlY + 5, {180, 180, 200, 255}, m_fontSmall);

            // QUAL button — switch between available stream quality options
            bool hasQuality = false;
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                hasQuality = m_detailStreams.size() > 1;
            }
            SDL_Color qualColor = hasQuality ? SDL_Color{120, 220, 120, 255} : SDL_Color{100, 100, 120, 255};
            SDL_Color qualBorder = hasQuality ? SDL_Color{60, 180, 60, 50}   : SDL_Color{255, 255, 255, 20};
            drawFilledRoundRect(qualX, ctrlY, btnQualW, 30, 8, qualBorder);
            drawRoundRect(qualX, ctrlY, btnQualW, 30, 8, {255, 255, 255, 30});
            drawTextCentered("QUAL", qualX + btnQualW / 2, ctrlY + 5, qualColor, m_fontSmall);

            // Controller hints (centered with extra padding)
            drawTextCentered("[A] Play/Pause   [Left/Right] Seek   [Y] Subs   [X] Audio   [L] Quality   [B] Back",
                             SCREEN_W / 2, panelY + panelH - 26, {120, 120, 140, 180}, m_fontSmall);
        }

        // 3. Swipe to Scrub Indicator
        if (m_isScrubbing) {
            int scrubW = 280;
            int scrubH = 60;
            int scrubX = SCREEN_W/2 - scrubW/2;
            int scrubY = 80;
            drawFilledRoundRect(scrubX, scrubY, scrubW, scrubH, 10, {0, 0, 0, 230});
            drawRoundRect(scrubX, scrubY, scrubW, scrubH, 10, ACCENT);
            std::string scrubStr = "Scrub: " + formatTime(m_scrubCurrentPos);
            drawText(scrubStr, scrubX + 30, scrubY + 18, TEXT_PRIMARY, m_fontNormal);
        }

        // 4. Subtitle / Audio Track List overlays
        if (m_showSubList) {
            int subW = 560;
            int subH = 430;
            int subX = SCREEN_W/2 - subW/2;
            int subY = SCREEN_H/2 - subH/2;

            drawFilledRoundRect(subX, subY, subW, subH, 16, {16, 20, 30, 248});
            drawRoundRect(subX, subY, subW, subH, 16, {40, 50, 75, 200});

            if (m_subAddonMode) {
                // ─── Addon Subtitles View (OpenSubtitles v3) ───
                drawText("Addon Subtitles (OpenSubtitles v3)", subX + 28, subY + 20, ACCENT, m_fontLarge);

                if (m_subAddonLoading.load()) {
                    drawSpinner(subX + subW / 2, subY + subH / 2 - 20, 22);
                    drawTextCentered("Searching OpenSubtitles v3...", subX + subW / 2, subY + subH / 2 + 18, TEXT_PRIMARY, m_fontNormal);
                    drawTextCentered("[B] Cancel & Resume Video", subX + subW / 2, subY + subH - 35, TEXT_SECONDARY, m_fontSmall);
                } else {
                    std::vector<Subtitle> localSubs;
                    {
                        std::lock_guard<std::mutex> lock(m_subMutex);
                        localSubs = m_addonSubtitles;
                    }

                    if (localSubs.empty()) {
                        drawTextCentered("No external subtitles found for this title.", subX + subW / 2, subY + subH / 2 - 10, TEXT_SECONDARY, m_fontNormal);
                        drawTextCentered("[B] Back & Resume Video", subX + subW / 2, subY + subH - 35, TEXT_PRIMARY, m_fontSmall);
                    } else {
                        std::string countStr = std::to_string(localSubs.size()) + " subtitles found:";
                        drawText(countStr, subX + 28, subY + 56, TEXT_SECONDARY, m_fontSmall);

                        int maxVis = 6;
                        int startIdx = 0;
                        if (m_subAddonIndex >= maxVis) startIdx = m_subAddonIndex - maxVis + 1;

                        int rowY = subY + 80;
                        for (int i = startIdx; i < (int)localSubs.size() && i < startIdx + maxVis; i++) {
                            bool sel = (i == m_subAddonIndex);
                            SDL_Color bg = sel ? CARD_HL : SDL_Color{25, 32, 46, 200};
                            drawFilledRoundRect(subX + 24, rowY, subW - 48, 44, 8, bg);
                            if (sel) {
                                drawRoundRect(subX + 24, rowY, subW - 48, 44, 8, ACCENT);
                            }

                            // Language badge pill
                            std::string lang = localSubs[i].lang;
                            for (char& c : lang) c = (char)toupper((unsigned char)c);
                            if (lang.empty()) lang = "SUB";
                            drawFilledRoundRect(subX + 36, rowY + 10, 48, 24, 4, {0, 180, 210, 50});
                            drawTextCentered(lang, subX + 60, rowY + 14, ACCENT, m_fontSmall);

                            // Subtitle title / file
                            std::string subName = localSubs[i].title;
                            if (subName.empty()) subName = localSubs[i].lang + " Subtitle #" + std::to_string(i + 1);
                            if (subName.size() > 48) subName = subName.substr(0, 45) + "..";
                            drawText(subName, subX + 96, rowY + 13, sel ? ACCENT : TEXT_PRIMARY, m_fontSmall);

                            rowY += 50;
                        }

                        // Bottom action legend
                        drawTextCentered("[A] Download & Apply   [B] Cancel & Resume", subX + subW / 2, subY + subH - 30, TEXT_SECONDARY, m_fontSmall);
                    }
                }
            } else {
                // ─── Standard Subtitles View (Add button + Embedded tracks) ───
                drawText("Subtitles", subX + 28, subY + 18, TEXT_PRIMARY, m_fontLarge);

                // Option 0: Add subtitle from addon button
                int btnAddY = subY + 58;
                bool selAdd = (m_subListIndex == 0);
                drawFilledRoundRect(subX + 24, btnAddY, subW - 48, 44, 8, selAdd ? CARD_HL : SDL_Color{28, 36, 52, 230});
                if (selAdd) {
                    drawRoundRect(subX + 24, btnAddY, subW - 48, 44, 8, ACCENT);
                } else {
                    drawRoundRect(subX + 24, btnAddY, subW - 48, 44, 8, {50, 64, 92, 180});
                }
                drawText("+ Add Subtitle from Addon (OpenSubtitles v3)", subX + 42, btnAddY + 12, selAdd ? ACCENT : TEXT_PRIMARY, m_fontNormal);

                // Divider label
                drawText("Embedded Tracks:", subX + 28, subY + 114, TEXT_SECONDARY, m_fontSmall);

                auto tracks = m_player.getSubtitleTracks();
                int maxVis = 5;
                int startIdx = 0;
                if (m_subListIndex > 0) {
                    int trackSelected = m_subListIndex - 1;
                    if (trackSelected >= maxVis) startIdx = trackSelected - maxVis + 1;
                }

                int rowY = subY + 138;
                for (int i = startIdx; i < (int)tracks.size() && i < startIdx + maxVis; i++) {
                    bool sel = (m_subListIndex == 1 + i);
                    SDL_Color bg = sel ? CARD_HL : (tracks[i].selected ? SDL_Color{0, 180, 255, 30} : SDL_Color{25, 32, 46, 200});
                    drawFilledRoundRect(subX + 24, rowY, subW - 48, 42, 6, bg);
                    if (sel) {
                        drawRoundRect(subX + 24, rowY, subW - 48, 42, 6, ACCENT);
                    }

                    std::string name = tracks[i].name;
                    if (tracks[i].selected) name = "[Active] " + name;
                    drawText(name, subX + 38, rowY + 11, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
                    rowY += 48;
                }

                // Bottom action legend
                drawTextCentered("[A] Select   [B] Close", subX + subW / 2, subY + subH - 28, TEXT_SECONDARY, m_fontSmall);
            }
        }

        if (m_showQualityList) {
            int qualW = 640;
            int qualH = 420;
            int qualX = SCREEN_W/2 - qualW/2;
            int qualY = SCREEN_H/2 - qualH/2;

            drawFilledRoundRect(qualX, qualY, qualW, qualH, 14, {0, 0, 0, 240});
            drawRoundRect(qualX, qualY, qualW, qualH, 14, {120, 220, 120, 60});

            drawText("Select Quality / Stream", qualX + 25, qualY + 20, {120, 220, 120, 255}, m_fontNormal);

            std::vector<Stream> localStreams;
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                localStreams = m_detailStreams;
            }

            int maxVis = 7;
            int startIdx = 0;
            if (m_qualityListIndex >= maxVis) startIdx = m_qualityListIndex - maxVis + 1;

            int rowY = qualY + 65;
            for (int i = startIdx; i < (int)localStreams.size() && i < startIdx + maxVis; i++) {
                bool sel = (i == m_qualityListIndex);
                SDL_Color bg = sel ? CARD_HL : SDL_Color{255, 255, 255, 10};
                drawFilledRoundRect(qualX + 20, rowY, qualW - 40, 42, 6, bg);
                if (sel) drawRoundRect(qualX + 20, rowY, qualW - 40, 42, 6, {120, 220, 120, 200});

                std::string label = formatStreamDisplay(localStreams[i]);
                if (label.size() > 72) label = label.substr(0, 69) + "...";

                drawText(label, qualX + 35, rowY + 9, sel ? SDL_Color{120, 220, 120, 255} : TEXT_PRIMARY, m_fontSmall);
                rowY += 48;
            }

            if (localStreams.empty()) {
                drawTextCentered("No alternate streams available", SCREEN_W/2, qualY + qualH/2, TEXT_SECONDARY, m_fontNormal);
            }
        }

        if (m_showAudioList) {
            int audW = 480;
            int audH = 400;
            int audX = SCREEN_W/2 - audW/2;
            int audY = SCREEN_H/2 - audH/2;

            drawFilledRoundRect(audX, audY, audW, audH, 14, {0, 0, 0, 240});
            drawRoundRect(audX, audY, audW, audH, 14, {255, 255, 255, 40});

            drawText("Select Audio Track", audX + 25, audY + 20, ACCENT, m_fontNormal);

            auto tracks = m_player.getAudioTracks();
            int maxVis = 6;
            int startIdx = 0;
            if (m_audioListIndex >= maxVis) startIdx = m_audioListIndex - maxVis + 1;
            
            int rowY = audY + 65;
            for (int i = startIdx; i < (int)tracks.size() && i < startIdx + maxVis; i++) {
                bool sel = (i == m_audioListIndex);
                SDL_Color bg = sel ? CARD_HL : (tracks[i].selected ? SDL_Color{0, 180, 255, 30} : SDL_Color{255, 255, 255, 10});
                drawFilledRoundRect(audX + 20, rowY, audW - 40, 42, 6, bg);
                if (sel) {
                    drawRoundRect(audX + 20, rowY, audW - 40, 42, 6, ACCENT);
                }
                
                std::string name = tracks[i].name;
                if (tracks[i].selected) name = "[Active] " + name;
                drawText(name, audX + 35, rowY + 9, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
                rowY += 48;
            }
        }

        if (buffering) {
            // Draw a nice centered glassmorphic buffering spinner card
            int cardW = 260;
            int cardH = 90;
            int cardX = (SCREEN_W - cardW) / 2;
            int cardY = (SCREEN_H - cardH) / 2;

            drawFilledRoundRect(cardX, cardY, cardW, cardH, 10, {0, 0, 0, 220});
            drawRoundRect(cardX, cardY, cardW, cardH, 10, {255, 255, 255, 40});
            drawSpinner(SCREEN_W / 2, cardY + 30, 14);

            double pct = m_player.getBufferingPercentage();
            std::string bufText = "Buffering... " + std::to_string((int)pct) + "%";
            drawText(bufText, SCREEN_W / 2 - 65, cardY + 55, TEXT_PRIMARY, m_fontSmall);
        }

        SDL_RenderPresent(m_renderer);
    }
}

static std::string formatCatalogTitle(const std::string& addonName, const std::string& catalogName) {
    if (addonName == "yastream") {
        std::string lowerC = catalogName;
        for (char& c : lowerC) c = (char)tolower((unsigned char)c);
        if (lowerC.find("kisskh") != std::string::npos) return "Asian & K-Dramas (KissKH)";
        if (lowerC.find("onetouchtv") != std::string::npos) return "Asian & K-Dramas (OneTouchTV)";
        return "Asian & K-Dramas (yastream)";
    }
    if (addonName.find("K-Drama Crush") != std::string::npos || addonName.find("kdrama") != std::string::npos) {
        return "Trending K-Dramas (K-Drama Crush)";
    }
    if (addonName == "Cinemeta") {
        std::string lowerC = catalogName;
        for (char& c : lowerC) c = (char)tolower((unsigned char)c);
        if (lowerC.find("movie") != std::string::npos || lowerC.find("top") != std::string::npos) return "Popular Movies";
        if (lowerC.find("series") != std::string::npos) return "Trending Series";
        return "Cinemeta — " + catalogName;
    }
    if (addonName == "CyberFlix Catalogs") {
        return "CyberFlix — " + catalogName;
    }

    // Clean up [addonName] prefix if present in catalogName
    std::string cleanCat = catalogName;
    std::string tag = "[" + addonName + "]";
    size_t tagPos = cleanCat.find(tag);
    if (tagPos != std::string::npos) {
        cleanCat.erase(tagPos, tag.size());
        while (!cleanCat.empty() && (cleanCat[0] == ' ' || cleanCat[0] == '-')) cleanCat.erase(0, 1);
    }
    return addonName + " — " + cleanCat;
}

void App::renderHome() {
    // 1. Brand Logo
    drawText("SwitchStream", 40, 18, TEXT_PRIMARY, m_fontLarge);

    // 2. Top Navigation Pill Bar
    int navX = 280, navY = 14, navW = 580, navH = 40;
    drawFilledRoundRect(navX, navY, navW, navH, 20, NAV_CONTAINER);
    drawRoundRect(navX, navY, navW, navH, 20, NAV_BORDER);

    // Tab 0: Home (Active)
    int tab0X = navX + 4;
    int tab0W = 96;
    drawFilledRoundRect(tab0X, navY + 4, tab0W, navH - 8, 16, ACCENT);
    drawTextCentered("Home", tab0X + tab0W / 2, navY + 9, {10, 14, 22, 255}, m_fontNormal);

    // Tab 1: Search
    int tab1X = navX + 104;
    int tab1W = 110;
    drawTextCentered("Search [Y]", tab1X + tab1W / 2, navY + 9, TEXT_SECONDARY, m_fontNormal);

    // Tab 2: Library
    int tab2X = navX + 218;
    int tab2W = 110;
    drawTextCentered("Library [L]", tab2X + tab2W / 2, navY + 9, TEXT_SECONDARY, m_fontNormal);

    // Tab 3: Addons
    int tab3X = navX + 332;
    int tab3W = 110;
    drawTextCentered("Addons [R]", tab3X + tab3W / 2, navY + 9, TEXT_SECONDARY, m_fontNormal);

    // Tab 4: Settings
    int tab4X = navX + 446;
    int tab4W = 126;
    drawTextCentered("Settings [X]", tab4X + tab4W / 2, navY + 9, TEXT_SECONDARY, m_fontNormal);

    // Right quick exit badge
    drawFilledRoundRect(SCREEN_W - 130, 18, 90, 32, 16, {26, 32, 46, 200});
    drawRoundRect(SCREEN_W - 130, 18, 90, 32, 16, {44, 52, 74, 180});
    drawTextCentered("[+] Exit", SCREEN_W - 85, 23, TEXT_SECONDARY, m_fontSmall);

    std::vector<CatalogRow> localCatalogs;
    {
        std::lock_guard<std::mutex> lock(m_homeMutex);
        localCatalogs = m_homeCatalogs;
    }

    if (m_loadingHome && localCatalogs.empty()) {
        drawSpinner(SCREEN_W/2, SCREEN_H/2 - 40, 24);
        drawTextCentered("Loading catalogs...", SCREEN_W/2, SCREEN_H/2 + 25, TEXT_SECONDARY);
        return;
    }

    if (localCatalogs.empty()) {
        drawText("No catalogs loaded. Press [R] to manage addons or [X] for Settings.",
                 SCREEN_W/2 - 250, SCREEN_H/2, TEXT_SECONDARY);
        return;
    }

    int startY = 74;
    int visibleRows = 2;
    int firstRow = m_homeRowIndex > 0 ? m_homeRowIndex - 1 : 0;

    for (int r = firstRow; r < (int)localCatalogs.size() && r < firstRow + visibleRows + 1; r++) {
        auto& row = localCatalogs[r];
        int y = startY + (r - firstRow) * ROW_HEIGHT;
        if (y + 40 > SCREEN_H - 40) break;

        // Clean, polished catalog title
        std::string title = formatCatalogTitle(row.addonName, row.catalogName);
        if (r == m_homeRowIndex) {
            // Glowing cyan accent bar for active row
            drawFilledRoundRect(40, y + 3, 4, 22, 2, ACCENT);
            drawText(title, 52, y, TEXT_PRIMARY, m_fontLarge);
        } else {
            drawText(title, 40, y + 3, TEXT_SECONDARY, m_fontNormal);
        }

        // Poster cards
        int x = 40;
        int maxVisible = (SCREEN_W - 80) / (POSTER_W + POSTER_GAP);
        int startCol = 0;
        if (r == m_homeRowIndex && m_homeColIndex >= maxVisible)
            startCol = m_homeColIndex - maxVisible + 1;

        for (int c = startCol; c < (int)row.items.size() && c < startCol + maxVisible; c++) {
            int cardX = x + (c - startCol) * (POSTER_W + POSTER_GAP);
            int cardY = y + 34;
            bool selected = (r == m_homeRowIndex && c == m_homeColIndex);

            // 1. Base card background
            drawFilledRoundRect(cardX, cardY, POSTER_W, POSTER_H, 8, CARD_COLOR);

            // 2. Poster image
            drawPoster(row.items[c], cardX, cardY, POSTER_W, POSTER_H);
            maskRoundedCorners(cardX, cardY, POSTER_W, POSTER_H, 8, BG_COLOR);

            // 3. Smooth dark overlay band for readable title at bottom
            drawFilledRect(cardX, cardY + POSTER_H - 36, POSTER_W, 36, {10, 14, 22, 225});
            drawFilledRect(cardX, cardY + POSTER_H - 40, POSTER_W, 4, {10, 14, 22, 120});
            maskRoundedCorners(cardX, cardY, POSTER_W, POSTER_H, 8, BG_COLOR);

            // 4. Centered title with ellipsis
            std::string name = row.items[c].name;
            if (name.size() > 19) name = name.substr(0, 17) + "..";
            drawTextCentered(name, cardX + POSTER_W / 2, cardY + POSTER_H - 26, TEXT_PRIMARY, m_fontSmall);

            // 5. Border: Glowing Cyan on selected, sleek dark border on unselected
            if (selected) {
                drawRoundRect(cardX - 3, cardY - 3, POSTER_W + 6, POSTER_H + 6, 11, {0, 229, 255, 60});
                drawRoundRect(cardX - 2, cardY - 2, POSTER_W + 4, POSTER_H + 4, 10, {0, 229, 255, 140});
                drawRoundRect(cardX - 1, cardY - 1, POSTER_W + 2, POSTER_H + 2, 9,  ACCENT);
                drawRoundRect(cardX, cardY, POSTER_W, POSTER_H, 8, ACCENT);
            } else {
                drawRoundRect(cardX, cardY, POSTER_W, POSTER_H, 8, {38, 46, 64, 180});
            }
        }
    }

    // ─── QA Control: More than 5 Stream Addons Warning Modal ───
    if (m_showStreamWarningPopup) {
        // Dim background overlay
        drawFilledRect(0, 0, SCREEN_W, SCREEN_H, {0, 0, 0, 190});

        int dw = 680, dh = 270;
        int dx = (SCREEN_W - dw) / 2;
        int dy = (SCREEN_H - dh) / 2;

        // Modal card container
        drawFilledRoundRect(dx, dy, dw, dh, 16, {20, 25, 38, 252});
        drawRoundRect(dx, dy, dw, dh, 16, {255, 185, 0, 220}); // amber warning accent

        // Warning title
        drawTextCentered("⚠️  Performance Advisory", SCREEN_W / 2, dy + 25, {255, 190, 40, 255}, m_fontLarge);

        // Warning message
        drawTextCentered("More than 5 stream addons are configured which may slow", SCREEN_W / 2, dy + 82, TEXT_PRIMARY, m_fontNormal);
        drawTextCentered("the overall performance.", SCREEN_W / 2, dy + 112, TEXT_SECONDARY, m_fontNormal);

        // Buttons
        int btnW = 240, btnH = 46;
        int btnY = dy + 180;
        int btn1X = dx + 65;
        int btn2X = dx + dw - 65 - btnW;

        // Button 1: Cancel
        bool sel1 = (m_streamWarningIndex == 0);
        drawFilledRoundRect(btn1X, btnY, btnW, btnH, 8, sel1 ? CARD_HL : CARD_COLOR);
        drawRoundRect(btn1X, btnY, btnW, btnH, 8, sel1 ? ACCENT : SDL_Color{60, 70, 95, 255});
        drawTextCentered("Cancel  [B]", btn1X + btnW / 2, btnY + 12, sel1 ? ACCENT : TEXT_PRIMARY, m_fontNormal);

        // Button 2: Don't show again
        bool sel2 = (m_streamWarningIndex == 1);
        drawFilledRoundRect(btn2X, btnY, btnW, btnH, 8, sel2 ? CARD_HL : CARD_COLOR);
        drawRoundRect(btn2X, btnY, btnW, btnH, 8, sel2 ? ACCENT : SDL_Color{60, 70, 95, 255});
        drawTextCentered("Don't show again  [X]", btn2X + btnW / 2, btnY + 12, sel2 ? ACCENT : TEXT_PRIMARY, m_fontNormal);
    }
}

void App::renderSearch() {
    drawText("Search: " + m_searchQuery, 40, 20, ACCENT, m_fontLarge);
    drawText("[Y] New search   [B] Back", 40, 55, TEXT_SECONDARY, m_fontSmall);

    std::string sortLabel = (m_searchSort == SearchSort::YEAR_DESC) ? "Sort: Year" : "Sort: Default";
    drawText("[X] " + sortLabel, SCREEN_W - 220, 55, ACCENT, m_fontSmall);

    if (m_loadingSearch) {
        drawSpinner(SCREEN_W/2, SCREEN_H/2 - 40, 22);
        drawTextCentered("Searching addons...", SCREEN_W/2, SCREEN_H/2 + 25, TEXT_SECONDARY);
        return;
    }

    std::vector<MetaItem> localResults;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        localResults = m_searchResults;
    }

    if (localResults.empty()) {
        drawText("No results found.", SCREEN_W/2 - 80, SCREEN_H/2, TEXT_SECONDARY);
        return;
    }

    int y = 90;
    int maxVisible = (SCREEN_H - 100) / 50;
    int startIdx = 0;
    if (m_searchIndex >= maxVisible) startIdx = m_searchIndex - maxVisible + 1;

    for (int i = startIdx; i < (int)localResults.size() && i < startIdx + maxVisible; i++) {
        bool sel = (i == m_searchIndex);
        if (sel) drawFilledRect(30, y - 2, SCREEN_W - 60, 46, CARD_HL);

        auto& item = localResults[i];
        
        // Disabled mini catalog image for faster search loading
        // drawPoster(item, 50, y, 28, 42);

        // Shift text to start at x = 50 (removed poster spacing)
        drawText(item.name, 50, y + 2, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
        std::string subText = item.type + "  " + item.releaseInfo;
        if (!item.addonName.empty()) subText += "  [" + item.addonName + "]";
        drawText(subText, 50, y + 24, TEXT_SECONDARY, m_fontSmall);
        
        y += 50;
    }
}

void App::renderDetail() {
    if (m_loadingDetail) {
        drawFilledRect(0, 0, SCREEN_W, SCREEN_H, {12, 16, 26, 255});
        drawSpinner(SCREEN_W / 2, SCREEN_H / 2 - 30, 22);
        drawTextCentered("Loading item details...", SCREEN_W / 2, SCREEN_H / 2 + 25, TEXT_SECONDARY, m_fontNormal);
        return;
    }

    // 1. Cinematic Blurred / Dimmed Backdrop
    SDL_Texture* bgTex = nullptr;
    if (m_imageCache) {
        if (!m_detailMeta.background.empty()) {
            bgTex = m_imageCache->get(m_detailMeta.background);
        }
        if (!bgTex && !m_detailMeta.poster.empty()) {
            bgTex = m_imageCache->get(m_detailMeta.poster);
        }
    }
    if (bgTex) {
        SDL_Rect fullScreen = {0, 0, SCREEN_W, SCREEN_H};
        SDL_RenderCopy(m_renderer, bgTex, nullptr, &fullScreen);
        drawFilledRect(0, 0, SCREEN_W, SCREEN_H, {10, 14, 22, 228});
    } else {
        drawFilledRect(0, 0, SCREEN_W, SCREEN_H, {12, 16, 26, 255});
    }

    // 2. Left Column: Large Poster Card (rounded with shadow)
    int postX = 55;
    int postY = 80;
    int postW = 270;
    int postH = 405;
    int postR = 14;

    drawFilledRoundRect(postX - 2, postY - 2, postW + 4, postH + 4, postR + 2, {0, 0, 0, 150});
    drawPoster(m_detailMeta, postX, postY, postW, postH);
    maskRoundedCorners(postX, postY, postW, postH, postR, {12, 16, 24, 255});
    drawRoundRect(postX, postY, postW, postH, postR, {55, 70, 95, 140});

    // Extract thread-safe state for middle & right panels
    bool isLoading = false;
    std::vector<Stream> localStreams;
    int currentSelIndex = 0;
    int currentEpIndex = 0;
    std::vector<Video> localEpisodes;
    std::vector<int> localSeasons;
    int currentSeasonFilter = 0;
    DetailFocus focus = DetailFocus::STREAMS;
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        isLoading = m_loadingStreams;
        localStreams = m_detailStreams;
        currentSelIndex = m_detailStreamIndex;
        currentEpIndex = m_detailEpisodeIndex;
        localEpisodes = m_detailEpisodes;
        localSeasons = m_detailSeasons;
        currentSeasonFilter = m_detailSeasonFilter;
        focus = m_detailFocus;
    }

    std::vector<Video> currentSeasonEps;
    if (!localSeasons.empty() && !localEpisodes.empty()) {
        int targetSeason = localSeasons[currentSeasonFilter];
        for (const auto& ep : localEpisodes) {
            if (ep.season == targetSeason) {
                currentSeasonEps.push_back(ep);
            }
        }
    } else {
        currentSeasonEps = localEpisodes;
    }

    // 3. Middle Column: Info & Episodes
    int infoX = 355;
    int infoW = 460;

    // Title
    std::string title = m_detailMeta.name;
    if (title.size() > 34) title = title.substr(0, 31) + "...";
    drawText(title, infoX, 80, TEXT_PRIMARY, m_fontLarge);

    // Badges Row
    int badgeX = infoX;
    int badgeY = 125;
    auto drawBadge = [this, &badgeX, badgeY](const std::string& text) {
        if (text.empty()) return;
        int tw = 0, th = 0;
        TTF_SizeUTF8(m_fontSmall, text.c_str(), &tw, &th);
        int pillW = tw + 22;
        drawFilledRoundRect(badgeX, badgeY, pillW, 26, 8, {42, 52, 72, 200});
        drawRoundRect(badgeX, badgeY, pillW, 26, 8, {65, 80, 110, 160});
        drawText(text, badgeX + 11, badgeY + 5, {220, 230, 245, 255}, m_fontSmall);
        badgeX += pillW + 10;
    };

    if (!m_detailMeta.releaseInfo.empty()) {
        drawBadge(m_detailMeta.releaseInfo);
    }
    if (!localEpisodes.empty()) {
        drawBadge(std::to_string(localEpisodes.size()) + " Episodes");
    } else if (!m_detailMeta.runtime.empty()) {
        drawBadge(m_detailMeta.runtime);
    }
    if (!m_detailMeta.genres.empty()) {
        std::string gStr = m_detailMeta.genres[0];
        if (m_detailMeta.genres.size() > 1) gStr += ", " + m_detailMeta.genres[1];
        drawBadge(gStr);
    } else if (!m_detailMeta.type.empty()) {
        std::string t = m_detailMeta.type;
        if (!t.empty()) t[0] = toupper(t[0]);
        drawBadge(t);
    }

    // Rating (Star + IMDB)
    int ratingY = 162;
    if (!m_detailMeta.imdbRating.empty()) {
        drawStar(m_renderer, infoX + 8, ratingY + 9, 8, {255, 204, 0, 255});
        drawText(m_detailMeta.imdbRating + " IMDB", infoX + 22, ratingY, {255, 255, 255, 255}, m_fontSmall);
    }

    // Synopsis / Description
    int descY = 194;
    std::string desc = m_detailMeta.description;
    int maxDescChars = localEpisodes.empty() ? 460 : 210;
    if ((int)desc.size() > maxDescChars) {
        desc = desc.substr(0, maxDescChars - 3) + "...";
    }
    int descH = drawTextWrapped(desc, infoX, descY, infoW, {175, 185, 205, 255}, m_fontSmall);
    if (descH <= 0) descH = 15;

    // Series Seasons & Episode List
    if (!localEpisodes.empty()) {
        int seasonY = descY + descH + 14;
        if (seasonY < 290) seasonY = 290;

        int curSeason = localSeasons.empty() ? 1 : localSeasons[currentSeasonFilter];
        std::string seasonStr = "Season " + std::to_string(curSeason);
        if (localSeasons.size() > 1) seasonStr += "  ◄ ► (L/R)";

        int sW = 0, sH = 0;
        TTF_SizeUTF8(m_fontSmall, seasonStr.c_str(), &sW, &sH);
        int pillW = sW + 24;
        drawFilledRoundRect(infoX, seasonY, pillW, 28, 8, {36, 48, 70, 220});
        drawRoundRect(infoX, seasonY, pillW, 28, 8, {0, 180, 220, 160});
        drawText(seasonStr, infoX + 12, seasonY + 5, {0, 229, 255, 255}, m_fontSmall);

        // Episode List
        int epListY = seasonY + 36;
        int epListH = 615 - epListY;
        int epItemH = 36;
        int epGap = 6;
        int maxVisibleEps = epListH / (epItemH + epGap);
        if (maxVisibleEps < 3) maxVisibleEps = 3;

        int startEp = 0;
        if (currentEpIndex >= maxVisibleEps / 2) {
            startEp = currentEpIndex - maxVisibleEps / 2;
        }
        if (startEp + maxVisibleEps > (int)currentSeasonEps.size()) {
            startEp = (int)currentSeasonEps.size() - maxVisibleEps;
        }
        if (startEp < 0) startEp = 0;

        int curEpY = epListY;
        for (int i = startEp; i < (int)currentSeasonEps.size() && i < startEp + maxVisibleEps; i++) {
            bool isFocused = (focus == DetailFocus::EPISODES && i == currentEpIndex);
            bool isCurrentSelected = (i == currentEpIndex);

            SDL_Color bgCol = isFocused ? SDL_Color{28, 54, 82, 240} : (isCurrentSelected ? SDL_Color{34, 44, 62, 200} : SDL_Color{22, 28, 42, 160});
            SDL_Color borderCol = isFocused ? SDL_Color{0, 229, 255, 255} : (isCurrentSelected ? SDL_Color{0, 180, 215, 120} : SDL_Color{42, 52, 72, 120});

            drawFilledRoundRect(infoX, curEpY, infoW, epItemH, 8, bgCol);
            drawRoundRect(infoX, curEpY, infoW, epItemH, 8, borderCol);
            if (isFocused) {
                drawRoundRect(infoX - 1, curEpY - 1, infoW + 2, epItemH + 2, 8, {0, 229, 255, 100});
            }

            auto& ep = currentSeasonEps[i];
            std::string epLabel = "Episode " + std::to_string(ep.episode);
            if (!ep.title.empty()) epLabel += " — " + ep.title;
            if (epLabel.size() > 42) epLabel = epLabel.substr(0, 39) + "...";

            SDL_Color txtCol = isFocused ? SDL_Color{0, 229, 255, 255} : (isCurrentSelected ? SDL_Color{255, 255, 255, 255} : SDL_Color{180, 195, 215, 255});
            drawText(epLabel, infoX + 14, curEpY + 8, txtCol, m_fontSmall);

            curEpY += epItemH + epGap;
        }
    }

    // 4. Right Column: Streams Panel (Card)
    int panelX = 845;
    int panelY = 80;
    int panelW = 385;
    int panelH = 540;
    int panelR = 16;

    drawFilledRoundRect(panelX, panelY, panelW, panelH, panelR, {16, 22, 34, 225});
    drawRoundRect(panelX, panelY, panelW, panelH, panelR, {48, 62, 88, 160});

    drawText("Streams", panelX + 22, panelY + 18, {255, 255, 255, 255}, m_fontLarge);

    if (isLoading) {
        drawSpinner(panelX + panelW / 2, panelY + panelH / 2 - 15, 18);
        drawTextCentered("Loading streams...", panelX + panelW / 2, panelY + panelH / 2 + 20, TEXT_PRIMARY, m_fontSmall);
    } else if (localStreams.empty()) {
        drawTextCentered("No streams found.", panelX + panelW / 2, panelY + panelH / 2, TEXT_SECONDARY, m_fontNormal);
    } else {
        int streamStartY = panelY + 62;
        int cardW = panelW - 36;
        int cardH = 68;
        int cardGap = 10;
        int cardR = 12;
        int maxVisibleStreams = (panelH - 74) / (cardH + cardGap);
        if (maxVisibleStreams < 3) maxVisibleStreams = 3;

        int startStreamIdx = 0;
        if (currentSelIndex >= maxVisibleStreams) {
            startStreamIdx = currentSelIndex - maxVisibleStreams + 1;
        }

        int curCardY = streamStartY;
        for (int i = startStreamIdx; i < (int)localStreams.size() && i < startStreamIdx + maxVisibleStreams; i++) {
            bool isSel = (focus == DetailFocus::STREAMS && i == currentSelIndex);
            if (localEpisodes.empty()) {
                isSel = (i == currentSelIndex);
            }

            int cardX = panelX + 18;
            StreamCardDisplay card = formatStreamCard(localStreams[i]);

            if (isSel) {
                drawFilledRoundRect(cardX, curCardY, cardW, cardH, cardR, {22, 45, 68, 245});
                drawRoundRect(cardX, curCardY, cardW, cardH, cardR, {0, 229, 255, 255});
                drawRoundRect(cardX - 1, curCardY - 1, cardW + 2, cardH + 2, cardR, {0, 229, 255, 120});

                drawText(card.line1, cardX + 16, curCardY + 11, {255, 255, 255, 255}, m_fontNormal);
                drawText(card.line2, cardX + 16, curCardY + 38, {110, 220, 240, 255}, m_fontSmall);
            } else {
                drawFilledRoundRect(cardX, curCardY, cardW, cardH, cardR, {25, 33, 48, 180});
                drawRoundRect(cardX, curCardY, cardW, cardH, cardR, {48, 60, 84, 150});

                drawText(card.line1, cardX + 16, curCardY + 11, {225, 235, 245, 255}, m_fontNormal);
                drawText(card.line2, cardX + 16, curCardY + 38, {140, 155, 175, 255}, m_fontSmall);
            }

            curCardY += cardH + cardGap;
        }
    }
}

void App::renderLibrary() {
    drawText("Library", 40, 20, ACCENT, m_fontLarge);
    drawText("[B] Back", 40, 55, TEXT_SECONDARY, m_fontSmall);

    auto items = m_library.getRecentlyWatched(20);
    if (items.empty()) {
        drawText("Nothing here yet. Start watching!", SCREEN_W/2 - 140, SCREEN_H/2, TEXT_SECONDARY);
        return;
    }

    int y = 90;
    for (int i = 0; i < (int)items.size() && y < SCREEN_H - 60; i++) {
        bool sel = (i == m_libraryIndex);
        if (sel) drawFilledRect(30, y - 2, SCREEN_W - 60, 46, CARD_HL);

        drawText(items[i].name, 50, y + 4, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
        int pct = (int)(items[i].progress * 100);
        drawText(std::to_string(pct) + "% watched", 50, y + 26, TEXT_SECONDARY, m_fontSmall);
        y += 50;
    }
}

void App::renderAddons() {
    drawText("Addon Manager", 40, 20, ACCENT, m_fontLarge);
    drawText("[Left/Right] Switch Pane   [Up/Down] Scroll   [A] Toggle/Install   [X] Uninstall   [Y] Add URL   [L] Host   [B] Back", 40, 55, TEXT_SECONDARY, m_fontSmall);

    std::string hostStr = "TorrServer Host: " + m_addonManager.getTorrServerHost();
    drawText(hostStr, 800, 25, {150, 220, 255, 255}, m_fontSmall);

    // Left Pane: Installed Addons
    int leftX = 40;
    int leftY = 80;
    int leftW = 580;
    int leftH = 520;
    
    // Draw background glassmorphic panels
    drawFilledRect(leftX, leftY, leftW, leftH, {0, 0, 0, 160}); // base panel
    drawRect(leftX, leftY, leftW, leftH, {255, 255, 255, 25}); // border
    
    // Header
    drawFilledRect(leftX, leftY, leftW, 35, {100, 120, 255, 30});
    drawText("Installed Addons", leftX + 15, leftY + 8, TEXT_PRIMARY, m_fontNormal);

    auto addons = m_addonManager.getAddons();
    
    // Right Pane: Discover Addons
    int rightX = 660;
    int rightY = 80;
    int rightW = 580;
    int rightH = 520;
    
    drawFilledRect(rightX, rightY, rightW, rightH, {0, 0, 0, 160});
    drawRect(rightX, rightY, rightW, rightH, {255, 255, 255, 25});

    drawFilledRect(rightX, rightY, rightW, 35, {100, 120, 255, 30});
    drawText("Discover Addons (Online Directory)", rightX + 15, rightY + 8, TEXT_PRIMARY, m_fontNormal);

    // Active pane highlight borders
    if (!m_addonDiscoverPane) {
        drawRect(leftX - 2, leftY - 2, leftW + 4, leftH + 4, ACCENT);
    } else {
        drawRect(rightX - 2, rightY - 2, rightW + 4, rightH + 4, ACCENT);
    }

    // Render Installed Addons
    if (addons.empty()) {
        drawText("No addons installed.", leftX + 20, leftY + 50, TEXT_SECONDARY);
    } else {
        int itemY = leftY + 45;
        // Make it scrollable
        int maxVisible = 7;
        int startIndex = 0;
        if (!m_addonDiscoverPane && m_addonIndex >= maxVisible) {
            startIndex = m_addonIndex - maxVisible + 1;
        }

        if (startIndex > 0) {
            drawText("^ More addons above...", leftX + 15, leftY + 38, TEXT_SECONDARY, m_fontSmall);
        }
        
        for (int i = startIndex; i < (int)addons.size() && (itemY + 60) <= (leftY + leftH - 20); i++) {
            bool sel = (!m_addonDiscoverPane && i == m_addonIndex);
            if (sel) {
                drawFilledRect(leftX + 5, itemY - 2, leftW - 10, 56, CARD_HL);
                drawRect(leftX + 5, itemY - 2, leftW - 10, 56, {255, 255, 255, 45});
            }
            
            std::string displayName = addons[i].manifest.name;
            if (!addons[i].enabled) {
                displayName = "[Disabled] " + displayName;
            }
            SDL_Color textColor = sel ? ACCENT : (addons[i].enabled ? TEXT_PRIMARY : TEXT_SECONDARY);
            drawText(displayName, leftX + 15, itemY + 4, textColor, m_fontNormal);
            
            std::string desc = addons[i].manifest.description;
            if (desc.size() > 65) desc = desc.substr(0, 62) + "...";
            drawText(desc, leftX + 15, itemY + 26, TEXT_SECONDARY, m_fontSmall);
            
            itemY += 60;
        }

        if (startIndex + maxVisible < (int)addons.size()) {
            drawText("v More addons below...", leftX + 15, leftY + leftH - 18, TEXT_SECONDARY, m_fontSmall);
        }
    }

    // Render Discover Addons
    int itemY = rightY + 45;
    int maxVisible = 7;
    int startIndex = 0;
    if (m_addonDiscoverPane && m_addonDiscoverIndex >= maxVisible) {
        startIndex = m_addonDiscoverIndex - maxVisible + 1;
    }

    if (startIndex > 0) {
        drawText("^ More addons above...", rightX + 15, rightY + 38, TEXT_SECONDARY, m_fontSmall);
    }
    
    for (int i = startIndex; i < (int)DISCOVER_ADDONS.size() && (itemY + 60) <= (rightY + rightH - 20); i++) {
        bool sel = (m_addonDiscoverPane && i == m_addonDiscoverIndex);
        
        // Check if this community addon is already installed
        bool installed = false;
        for (auto& a : addons) {
            if (a.transportUrl == DISCOVER_ADDONS[i].url) {
                installed = true;
                break;
            }
        }

        if (sel) {
            drawFilledRect(rightX + 5, itemY - 2, rightW - 10, 56, CARD_HL);
            drawRect(rightX + 5, itemY - 2, rightW - 10, 56, {255, 255, 255, 45});
        }

        drawText(DISCOVER_ADDONS[i].name, rightX + 15, itemY + 4, sel ? ACCENT : (installed ? SDL_Color{100, 200, 100, 255} : TEXT_PRIMARY), m_fontNormal);
        
        std::string desc = DISCOVER_ADDONS[i].description;
        if (installed) desc = "[Installed] " + desc;
        if (desc.size() > 65) desc = desc.substr(0, 62) + "...";
        drawText(desc, rightX + 15, itemY + 26, TEXT_SECONDARY, m_fontSmall);

        // If we are currently loading/installing this addon, draw a small spinner
        if (sel && m_loading) {
            drawSpinner(rightX + rightW - 40, itemY + 25, 10);
        }
        
        itemY += 60;
    }
    if (startIndex + maxVisible < (int)DISCOVER_ADDONS.size()) {
        drawText("v More addons below...", rightX + 15, rightY + rightH - 18, TEXT_SECONDARY, m_fontSmall);
    }
}
void App::renderSettings() {
    drawText("System Settings", 40, 20, ACCENT, m_fontLarge);
    drawText("[Up/Down] Navigate   [A] Change/Toggle   [B] Back to Home", 40, 55, TEXT_SECONDARY, m_fontSmall);

    int panelX = 40;
    int panelY = 90;
    int panelW = 1200;
    int panelH = 560;

    // Base glassmorphic panel
    drawFilledRect(panelX, panelY, panelW, panelH, {0, 0, 0, 160});
    drawRect(panelX, panelY, panelW, panelH, {255, 255, 255, 25});

    // 6 settings options
    struct SettingOption {
        std::string title;
        std::string description;
        std::string value;
    };

    std::vector<SettingOption> options = {
        {"Enable Torrent Streams", "Native on-device BitTorrent streaming using RAM circular buffer.", m_addonManager.getEnableTorrents() ? "ON (Enabled)" : "OFF (Disabled)"},
        {"TorrServer Host URL", "Endpoint for streaming torrent-based media files.", m_addonManager.getTorrServerHost()},
        {"Hardware Decoding", "Uses GPU-accelerated video decoding context (recommended).", m_addonManager.getHwDecode() ? "ON (Enabled)" : "OFF (Disabled)"},
        {"Preferred Subtitle Language", "Default language code for media subtitle streams.", m_addonManager.getSubtitleLang()},
        {"Clean Cache & Reset", "Deletes all cached metadata, history, and custom addons.", "Press [A] to clear"},
        {"Back to Home", "Return to main screen.", ""}
    };

    int itemY = panelY + 15;
    for (int i = 0; i < (int)options.size(); ++i) {
        bool sel = (i == m_settingsIndex);

        if (sel) {
            // Glowing border & frosted background for selected card
            drawFilledRect(panelX + 15, itemY - 6, panelW - 30, 76, CARD_HL);
            drawRect(panelX + 15, itemY - 6, panelW - 30, 76, ACCENT);
        } else {
            drawFilledRect(panelX + 15, itemY - 6, panelW - 30, 76, CARD_COLOR);
            drawRect(panelX + 15, itemY - 6, panelW - 30, 76, {255, 255, 255, 15});
        }

        // Title and description
        drawText(options[i].title, panelX + 35, itemY + 6, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
        drawText(options[i].description, panelX + 35, itemY + 38, TEXT_SECONDARY, m_fontSmall);

        // Value on the right
        if (!options[i].value.empty()) {
            SDL_Color valColor = sel ? SDL_Color{255, 255, 255, 255} : SDL_Color{150, 180, 255, 255};
            drawText(options[i].value, panelX + panelW - 400, itemY + 22, valColor, m_fontNormal);
        }

        itemY += 88;
    }
}

void App::drawNavBar() {
    int barY = SCREEN_H - 38;
    int barH = 38;

    // Background bar
    drawFilledRect(0, barY, SCREEN_W, barH, {12, 16, 24, 245});
    drawFilledRect(0, barY, SCREEN_W, 1, {32, 40, 58, 255}); // 1px subtle top border

    auto drawBtnPrompt = [this](const std::string& btn, const std::string& label, int x, int y) -> int {
        int btnW = std::max(22, (int)btn.size() * 10 + 10);
        int btnH = 20;
        // Button pill
        drawFilledRoundRect(x, y - 2, btnW, btnH, btnH / 2, {36, 44, 62, 255});
        drawRoundRect(x, y - 2, btnW, btnH, btnH / 2, {60, 72, 98, 255});
        drawTextCentered(btn, x + btnW / 2, y, {240, 245, 255, 255}, m_fontSmall);

        // Label
        drawText(label, x + btnW + 7, y, TEXT_SECONDARY, m_fontSmall);
        int labelW = (int)label.size() * 8 + 18;
        return x + btnW + labelW;
    };

    int curX = 40;
    int curY = barY + 9;

    switch (m_screen) {
    case Screen::HOME:
        curX = drawBtnPrompt("A", "Select", curX, curY);
        curX = drawBtnPrompt("B", "Refresh", curX, curY);
        curX = drawBtnPrompt("Y", "Search", curX, curY);
        curX = drawBtnPrompt("L/R", "Tabs", curX, curY);
        curX = drawBtnPrompt("+", "Exit", curX, curY);
        break;
    case Screen::SEARCH:
        curX = drawBtnPrompt("A", "Select", curX, curY);
        curX = drawBtnPrompt("B", "Back", curX, curY);
        curX = drawBtnPrompt("X", "Sort", curX, curY);
        curX = drawBtnPrompt("Y", "New Search", curX, curY);
        break;
    case Screen::DETAIL: {
        bool hasEpisodes = false;
        bool inEpisodes = false;
        {
            std::lock_guard<std::mutex> lock(m_streamsMutex);
            hasEpisodes = !m_detailEpisodes.empty();
            inEpisodes = (m_detailFocus == DetailFocus::EPISODES);
        }

        const LibraryItem* libItem = m_library.getItem(m_detailMeta.id);
        std::string bmLabel = (libItem && libItem->bookmarked) ? "Bookmarked" : "Bookmark";

        std::vector<std::pair<std::string, std::string>> prompts;
        if (hasEpisodes && inEpisodes) {
            prompts.push_back({"A", "Streams"});
            prompts.push_back({"L/R", "Season"});
            prompts.push_back({"B", "Back"});
            prompts.push_back({"X", bmLabel});
        } else {
            prompts.push_back({"A", "Play Stream"});
            prompts.push_back({"B", hasEpisodes ? "Episodes" : "Back"});
            prompts.push_back({"X", bmLabel});
        }

        int totalW = 0;
        for (const auto& p : prompts) {
            int btnW = std::max(22, (int)p.first.size() * 10 + 10);
            int labelW = (int)p.second.size() * 8 + 18;
            totalW += btnW + labelW + 24;
        }
        totalW -= 24;

        int centeredX = std::max(40, (SCREEN_W - totalW) / 2);
        for (const auto& p : prompts) {
            centeredX = drawBtnPrompt(p.first, p.second, centeredX, curY) + 24;
        }
        break;
    }
    case Screen::LIBRARY:
        curX = drawBtnPrompt("A", "Play / Resume", curX, curY);
        curX = drawBtnPrompt("B", "Back", curX, curY);
        curX = drawBtnPrompt("X", "Remove", curX, curY);
        break;
    case Screen::ADDONS:
        curX = drawBtnPrompt("A", "Toggle / Install", curX, curY);
        curX = drawBtnPrompt("B", "Back", curX, curY);
        break;
    case Screen::SETTINGS:
        curX = drawBtnPrompt("A", "Select", curX, curY);
        curX = drawBtnPrompt("B", "Back", curX, curY);
        break;
    default:
        curX = drawBtnPrompt("A", "Select", curX, curY);
        curX = drawBtnPrompt("B", "Back", curX, curY);
        break;
    }
}

// ─── Drawing helpers ─────────────────────────

void App::drawText(const std::string& text, int x, int y,
                    SDL_Color color, TTF_Font* font) {
    if (text.empty()) return;
    TTF_Font* f = font ? font : m_fontNormal;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(f, text.c_str(), color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

void App::drawTextCentered(const std::string& text, int cx, int y,
                           SDL_Color color, TTF_Font* font) {
    if (text.empty()) return;
    TTF_Font* f = font ? font : m_fontNormal;
    SDL_Surface* surface = TTF_RenderUTF8_Blended(f, text.c_str(), color);
    if (!surface) return;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    int x = cx - surface->w / 2;
    SDL_Rect dst = {x, y, surface->w, surface->h};
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}

int App::drawTextWrapped(const std::string& text, int x, int y, int wrapWidth,
                         SDL_Color color, TTF_Font* font) {
    if (text.empty()) return 0;
    TTF_Font* f = font ? font : m_fontNormal;
    SDL_Surface* surface = TTF_RenderUTF8_Blended_Wrapped(f, text.c_str(), color, wrapWidth);
    if (!surface) return 0;
    int h = surface->h;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    SDL_Rect dst = {x, y, surface->w, h};
    SDL_RenderCopy(m_renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
    return h;
}

void App::drawRect(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderDrawRect(m_renderer, &r);
}

void App::drawFilledRect(int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(m_renderer, &r);
}

void App::drawRoundRect(int x, int y, int w, int h, int r, SDL_Color color) {
    if (r <= 0) {
        drawRect(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);

    // Draw straight lines
    SDL_RenderDrawLine(m_renderer, x + r, y, x + w - r, y); // Top
    SDL_RenderDrawLine(m_renderer, x + r, y + h - 1, x + w - r, y + h - 1); // Bottom
    SDL_RenderDrawLine(m_renderer, x, y + r, x, y + h - r); // Left
    SDL_RenderDrawLine(m_renderer, x + w - 1, y + r, x + w - 1, y + h - r); // Right

    // Draw corners
    auto drawCornerOutline = [&](int cx, int cy, int rx, int ry, int quadrant) {
        int lastX = -1, lastY = -1;
        for (int angle = 0; angle <= 90; angle += 5) {
            double rad = angle * M_PI / 180.0;
            int px = (int)(rx * cos(rad) + 0.5);
            int py = (int)(ry * sin(rad) + 0.5);
            
            int targetX = 0, targetY = 0;
            if (quadrant == 0) { // top-left
                targetX = cx - px;
                targetY = cy - py;
            } else if (quadrant == 1) { // top-right
                targetX = cx + px;
                targetY = cy - py;
            } else if (quadrant == 2) { // bottom-left
                targetX = cx - px;
                targetY = cy + py;
            } else if (quadrant == 3) { // bottom-right
                targetX = cx + px;
                targetY = cy + py;
            }

            if (lastX != -1) {
                SDL_RenderDrawLine(m_renderer, lastX, lastY, targetX, targetY);
            }
            lastX = targetX;
            lastY = targetY;
        }
    };

    drawCornerOutline(x + r, y + r, r, r, 0); // top-left
    drawCornerOutline(x + w - r - 1, y + r, r, r, 1); // top-right
    drawCornerOutline(x + r, y + h - r - 1, r, r, 2); // bottom-left
    drawCornerOutline(x + w - r - 1, y + h - r - 1, r, r, 3); // bottom-right
}

void App::drawFilledRoundRect(int x, int y, int w, int h, int r, SDL_Color color) {
    if (r <= 0) {
        drawFilledRect(x, y, w, h, color);
        return;
    }
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, color.r, color.g, color.b, color.a);

    // Draw the main body rectangles
    SDL_Rect rectBody = { x + r, y, w - 2 * r, h };
    SDL_Rect rectLeft = { x, y + r, r, h - 2 * r };
    SDL_Rect rectRight = { x + w - r, y + r, r, h - 2 * r };
    SDL_RenderFillRect(m_renderer, &rectBody);
    SDL_RenderFillRect(m_renderer, &rectLeft);
    SDL_RenderFillRect(m_renderer, &rectRight);

    // Draw the 4 corners (filled quarter-circles)
    auto drawCorner = [&](int cx, int cy, int rx, int ry, int dx, int dy) {
        for (int py = 0; py <= ry; py++) {
            int px = (int)(rx * sqrt(1.0 - (double)(py * py) / (ry * ry)) + 0.5);
            SDL_Rect lineRect = { 
                cx + (dx < 0 ? -px : 0), 
                cy + dy * py, 
                px, 
                1 
            };
            SDL_RenderFillRect(m_renderer, &lineRect);
        }
    };

    drawCorner(x + r, y + r, r, r, -1, -1); // top-left
    drawCorner(x + w - r, y + r, r, r, 1, -1); // top-right
    drawCorner(x + r, y + h - r, r, r, -1, 1); // bottom-left
    drawCorner(x + w - r, y + h - r, r, r, 1, 1); // bottom-right
}

void App::maskRoundedCorners(int x, int y, int w, int h, int r, SDL_Color bgColor) {
    if (r <= 0) return;
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_renderer, bgColor.r, bgColor.g, bgColor.b, bgColor.a);
    int rSq = r * r;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int di = r - 1 - i;
            int dj = r - 1 - j;
            if (di * di + dj * dj >= rSq) {
                SDL_RenderDrawPoint(m_renderer, x + i, y + j);
                SDL_RenderDrawPoint(m_renderer, x + w - 1 - i, y + j);
                SDL_RenderDrawPoint(m_renderer, x + i, y + h - 1 - j);
                SDL_RenderDrawPoint(m_renderer, x + w - 1 - i, y + h - 1 - j);
            }
        }
    }
}

static void drawFilledCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(renderer, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void drawFilledSector(SDL_Renderer* renderer, int cx, int cy, int radius, double startAngle, double endAngle, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    
    // Normalize angles to [0, 2*PI]
    while (startAngle < 0) startAngle += 2 * M_PI;
    while (startAngle >= 2 * M_PI) startAngle -= 2 * M_PI;
    while (endAngle < 0) endAngle += 2 * M_PI;
    while (endAngle >= 2 * M_PI) endAngle -= 2 * M_PI;

    bool crossZero = (startAngle > endAngle);

    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            int distSq = x * x + y * y;
            if (distSq <= radius * radius) {
                if (distSq == 0) {
                    SDL_RenderDrawPoint(renderer, cx, cy);
                    continue;
                }
                double angle = atan2(y, x);
                if (angle < 0) angle += 2 * M_PI;

                bool inSector = false;
                if (!crossZero) {
                    inSector = (angle >= startAngle && angle <= endAngle);
                } else {
                    inSector = (angle >= startAngle || angle <= endAngle);
                }

                if (inSector) {
                    SDL_RenderDrawPoint(renderer, cx + x, cy + y);
                }
            }
        }
    }
}

void App::drawSpinner(int cx, int cy, int radius) {
    // 1. Draw base semi-transparent circle
    SDL_Color baseColor = {100, 200, 255, 45};
    drawFilledCircle(m_renderer, cx, cy, radius, baseColor);

    // 2. Draw rotating sector (slightly larger radius)
    double time = SDL_GetTicks() / 1000.0;
    double speed = 6.0; // rotation speed
    double startAngle = time * speed;
    double span = 75.0 * M_PI / 180.0; // 75 degrees span
    double endAngle = startAngle + span;

    int sectorR = radius + 3;
    if (sectorR < 5) sectorR = 5;

    SDL_Color sectorColor = {0, 180, 255, 230};
    drawFilledSector(m_renderer, cx, cy, sectorR, startAngle, endAngle, sectorColor);
}

void App::drawPoster(const MetaItem& item, int x, int y, int w, int h) {
    if (item.poster.empty() && item.name.empty()) {
        drawFilledRoundRect(x, y, w, h, 8, CARD_COLOR);
        drawRoundRect(x, y, w, h, 8, {38, 46, 64, 180});
        drawFilledRoundRect(x + w / 2 - 18, y + h / 2 - 18, 36, 36, 8, {32, 38, 54, 255});
        drawTextCentered("🎬", x + w / 2, y + h / 2 - 10, TEXT_SECONDARY, m_fontSmall);
        return;
    }

    std::string titleKey = normalizeTitleKey(item.name, item.type);

    // 1. Try title-based cache in RAM first, then URL-based cache
    SDL_Texture* tex = nullptr;
    if (!titleKey.empty()) {
        tex = m_imageCache->get(titleKey);
    }
    if (!tex && !item.poster.empty()) {
        tex = m_imageCache->get(item.poster);
    }
    if (tex) {
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        return;
    }

    // 2. If it's already loading or failed, draw the placeholder and return
    bool isLoading = false;
    bool isFailed = false;
    {
        std::lock_guard<std::mutex> lock(m_downloadedMutex);
        if ((!titleKey.empty() && m_failedPosters.find(titleKey) != m_failedPosters.end()) ||
            (!item.poster.empty() && m_failedPosters.find(item.poster) != m_failedPosters.end())) {
            isFailed = true;
        }
        if ((!titleKey.empty() && m_loadingPosters.find(titleKey) != m_loadingPosters.end()) ||
            (!item.poster.empty() && m_loadingPosters.find(item.poster) != m_loadingPosters.end())) {
            isLoading = true;
        }
    }

    if (isFailed) {
        drawFilledRoundRect(x, y, w, h, 8, CARD_COLOR);
        drawRoundRect(x, y, w, h, 8, {38, 46, 64, 180});
        drawTextCentered("No Poster", x + w / 2, y + h / 2 - 8, TEXT_SECONDARY, m_fontSmall);
        return;
    }

    if (!isLoading && !item.poster.empty()) {
        {
            std::lock_guard<std::mutex> lock(m_downloadedMutex);
            if (!titleKey.empty()) m_loadingPosters.insert(titleKey);
            m_loadingPosters.insert(item.poster);
        }

        // Add to download queue
        {
            std::lock_guard<std::mutex> lock(m_downloadQueueMutex);
            bool alreadyQueued = false;
            for (const auto& job : m_downloadQueue) {
                if (job.url == item.poster || (!titleKey.empty() && job.titleKey == titleKey)) {
                    alreadyQueued = true;
                    break;
                }
            }
            if (!alreadyQueued) {
                m_downloadQueue.push_back({item.poster, titleKey});
                m_downloadQueueCV.notify_one();
            }
        }
    }

    // Draw loading card with subtle spinner
    drawFilledRoundRect(x, y, w, h, 8, CARD_COLOR);
    drawRoundRect(x, y, w, h, 8, {38, 46, 64, 180});
    drawSpinner(x + w / 2, y + h / 2, 16);
}

std::string App::normalizeTitleKey(const std::string& title, const std::string& type) {
    if (title.empty()) return "";
    std::string norm = type.empty() ? "title:" : (type + ":");
    for (char c : title) {
        if (isalnum((unsigned char)c)) {
            norm += (char)tolower((unsigned char)c);
        }
    }
    return norm;
}

static std::string urlToHash(const std::string& key) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : key) {
        hash ^= (uint64_t)(unsigned char)c;
        hash *= 1099511628211ULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return std::string(buf);
}

std::string App::getDiskCachePath(const std::string& key) {
    if (key.empty()) return "";
    return std::string(CACHE_DIR) + "/" + urlToHash(key) + ".cache";
}

bool App::readDiskCache(const std::string& path, std::string& outData) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.seekg(0, std::ios::end);
    std::streampos sz = f.tellg();
    if (sz <= 0 || sz > 10 * 1024 * 1024) return false;
    f.seekg(0, std::ios::beg);
    outData.resize(static_cast<size_t>(sz));
    f.read(&outData[0], sz);
    return f.good();
}

void App::writeDiskCache(const std::string& path, const std::string& data) {
    if (data.empty()) return;
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    f.write(data.data(), data.size());
}

void App::prequeuePosters(const std::vector<CatalogRow>& rows, int maxRows) {
    std::lock_guard<std::mutex> lock(m_downloadQueueMutex);
    for (int r = 0; r < (int)rows.size() && r < maxRows; ++r) {
        for (const auto& item : rows[r].items) {
            if (item.poster.empty()) continue;
            std::string titleKey = normalizeTitleKey(item.name, item.type);
            if (m_imageCache && !titleKey.empty() && m_imageCache->has(titleKey)) continue;
            if (m_imageCache && m_imageCache->has(item.poster)) continue;

            bool alreadyQueued = false;
            for (const auto& job : m_downloadQueue) {
                if (job.url == item.poster || (!titleKey.empty() && job.titleKey == titleKey)) {
                    alreadyQueued = true;
                    break;
                }
            }
            if (!alreadyQueued) {
                m_downloadQueue.push_back({item.poster, titleKey});
            }
        }
    }
    m_downloadQueueCV.notify_all();
}

bool App::saveHomeCache(const std::string& path, const std::vector<CatalogRow>& catalogs) {
    if (catalogs.empty()) return false;
    rapidjson::StringBuffer sb;
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);

    writer.StartObject();
    writer.Key("catalogs");
    writer.StartArray();
    for (const auto& row : catalogs) {
        writer.StartObject();
        writer.Key("addonName");    writer.String(row.addonName.c_str());
        writer.Key("catalogName");  writer.String(row.catalogName.c_str());
        writer.Key("type");         writer.String(row.type.c_str());
        writer.Key("catalogId");    writer.String(row.catalogId.c_str());
        writer.Key("transportUrl"); writer.String(row.transportUrl.c_str());
        writer.Key("items");
        writer.StartArray();
        for (const auto& item : row.items) {
            writer.StartObject();
            writer.Key("id");          writer.String(item.id.c_str());
            writer.Key("type");        writer.String(item.type.c_str());
            writer.Key("name");        writer.String(item.name.c_str());
            writer.Key("poster");      writer.String(item.poster.c_str());
            writer.Key("releaseInfo"); writer.String(item.releaseInfo.c_str());
            writer.Key("imdbRating");  writer.String(item.imdbRating.c_str());
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << sb.GetString();
    return true;
}

bool App::loadHomeCache(const std::string& path, std::vector<CatalogRow>& outCatalogs) {
    std::ifstream file(path);
    if (!file.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    if (content.empty()) return false;

    rapidjson::Document doc;
    doc.Parse(content.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("catalogs") || !doc["catalogs"].IsArray()) {
        return false;
    }

    outCatalogs.clear();
    for (const auto& r : doc["catalogs"].GetArray()) {
        if (!r.IsObject()) continue;
        CatalogRow row;
        if (r.HasMember("addonName") && r["addonName"].IsString()) row.addonName = r["addonName"].GetString();
        if (r.HasMember("catalogName") && r["catalogName"].IsString()) row.catalogName = r["catalogName"].GetString();
        if (r.HasMember("type") && r["type"].IsString()) row.type = r["type"].GetString();
        if (r.HasMember("catalogId") && r["catalogId"].IsString()) row.catalogId = r["catalogId"].GetString();
        if (r.HasMember("transportUrl") && r["transportUrl"].IsString()) row.transportUrl = r["transportUrl"].GetString();
        if (r.HasMember("items") && r["items"].IsArray()) {
            for (const auto& m : r["items"].GetArray()) {
                if (!m.IsObject()) continue;
                MetaItem item;
                if (m.HasMember("id") && m["id"].IsString()) item.id = m["id"].GetString();
                if (m.HasMember("type") && m["type"].IsString()) item.type = m["type"].GetString();
                if (m.HasMember("name") && m["name"].IsString()) item.name = m["name"].GetString();
                if (m.HasMember("poster") && m["poster"].IsString()) item.poster = m["poster"].GetString();
                if (m.HasMember("releaseInfo") && m["releaseInfo"].IsString()) item.releaseInfo = m["releaseInfo"].GetString();
                if (m.HasMember("imdbRating") && m["imdbRating"].IsString()) item.imdbRating = m["imdbRating"].GetString();
                row.items.push_back(std::move(item));
            }
        }
        if (!row.items.empty()) {
            outCatalogs.push_back(std::move(row));
        }
    }
    return !outCatalogs.empty();
}

void App::downloadWorkerLoop() {
    while (m_downloadWorkerRunning) {
        DownloadJob job;
        {
            std::unique_lock<std::mutex> lock(m_downloadQueueMutex);
            m_downloadQueueCV.wait(lock, [this]() {
                return !m_downloadQueue.empty() || !m_downloadWorkerRunning;
            });
            if (!m_downloadWorkerRunning) break;
            job = m_downloadQueue.front();
            m_downloadQueue.erase(m_downloadQueue.begin());
        }

        std::string titleCacheFile;
        if (!job.titleKey.empty()) {
            titleCacheFile = getDiskCachePath(job.titleKey);
        }
        std::string urlCacheFile = getDiskCachePath(job.url);

        std::string imgData;
        bool loadedFromDisk = false;

        // 1. Check title-based disk cache first!
        if (!titleCacheFile.empty() && readDiskCache(titleCacheFile, imgData)) {
            loadedFromDisk = true;
        }

        // 2. Check url-based disk cache second
        if (!loadedFromDisk && !urlCacheFile.empty() && readDiskCache(urlCacheFile, imgData)) {
            loadedFromDisk = true;
        }

        // 3. If not on disk, download via HTTP with 10s timeout using dedicated image client
        if (!loadedFromDisk && !job.url.empty()) {
            auto resp = m_imageHttp.downloadBytes(job.url, 10);
            if (resp.ok() && resp.body.size() >= 128) {
                imgData = std::move(resp.body);
                // Save to disk by title key AND url key so next load instantly hits disk cache!
                if (!titleCacheFile.empty()) {
                    writeDiskCache(titleCacheFile, imgData);
                }
                if (!urlCacheFile.empty()) {
                    writeDiskCache(urlCacheFile, imgData);
                }
            }
        }

        // 4. Dispatch to main thread for texture creation
        {
            std::lock_guard<std::mutex> lock(m_downloadedMutex);
            if (!job.titleKey.empty()) m_loadingPosters.erase(job.titleKey);
            m_loadingPosters.erase(job.url);
            if (!imgData.empty()) {
                DownloadedImage img;
                img.url = job.url;
                img.titleKey = job.titleKey;
                img.data = std::move(imgData);
                m_downloadedQueue.push_back(std::move(img));
            } else {
                if (!job.titleKey.empty()) m_failedPosters.insert(job.titleKey);
                m_failedPosters.insert(job.url);
            }
        }
    }
}

// ─── Cache Management ────────────────────────
void App::cleanCatalogImageCache() {
    DIR* dir = opendir(CACHE_DIR);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != nullptr) {
            std::string name = ent->d_name;
            if (name == "." || name == "..") continue;
            std::string fullPath = std::string(CACHE_DIR) + "/" + name;
            unlink(fullPath.c_str());
        }
        closedir(dir);
    }
    std::remove(HOME_CACHE);
    if (m_imageCache) {
        m_imageCache->clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_downloadedMutex);
        m_failedPosters.clear();
        m_loadingPosters.clear();
        m_downloadedQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_downloadQueueMutex);
        m_downloadQueue.clear();
    }
    printf("[App] Catalog image cache and home cache cleaned successfully.\n");
}

// ─── Data loading ────────────────────────────

void App::loadHomeCatalogs(bool force) {
    if (m_loadingHome) {
        if (!force) return;
        m_http.cancel();
        if (m_homeLoadingThread.joinable()) {
            m_homeLoadingThread.join();
        }
        m_http.reset();
        m_loadingHome = false;
    } else {
        if (m_homeLoadingThread.joinable()) {
            m_homeLoadingThread.join();
        }
    }

    m_loadingHome = true;
    m_homeLoadingThread = std::thread([this]() {
        auto catalogs = m_addonManager.getHomeCatalogs();
        {
            std::lock_guard<std::mutex> lock(m_homeMutex);
            if (!catalogs.empty()) {
                m_homeCatalogs = std::move(catalogs);
                saveHomeCache(HOME_CACHE, m_homeCatalogs);
                prequeuePosters(m_homeCatalogs, 3);
            }
        }
        m_loadingHome = false;
    });
}

void App::performSearch(const std::string& query) {
    if (m_searchThread.joinable()) {
        m_searchThread.join();
    }
    
    m_loadingSearch = true;
    m_searchIndex = 0;
    {
        std::lock_guard<std::mutex> lock(m_searchMutex);
        m_searchResults.clear();
    }

    m_searchThread = std::thread([this, query]() {
        auto results = m_addonManager.search(query);
        {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            m_searchResults = std::move(results);
            sortSearchResults();
        }
        m_loadingSearch = false;
    });
}

void App::sortSearchResults() {
    if (m_searchSort == SearchSort::YEAR_DESC) {
        std::sort(m_searchResults.begin(), m_searchResults.end(), [](const MetaItem& a, const MetaItem& b) {
            auto getYear = [](const std::string& str) -> int {
                for (size_t i = 0; i < str.size(); i++) {
                    if (std::isdigit(static_cast<unsigned char>(str[i]))) {
                        int num = 0;
                        size_t j = i;
                        while (j < str.size() && std::isdigit(static_cast<unsigned char>(str[j]))) {
                            num = num * 10 + (str[j] - '0');
                            j++;
                        }
                        if (num >= 1900 && num <= 2100) return num;
                        i = j;
                    }
                }
                return 0;
            };
            int yA = getYear(a.releaseInfo);
            int yB = getYear(b.releaseInfo);
            if (yA != yB) return yA > yB; // Year descending
            return a.name < b.name;       // Alphabetic fallback
        });
    }
}

void App::detailWorkerLoop() {
    while (m_workerRunning) {
        int task = 0;
        std::string type, id;
        int currentGen = 0;

        {
            std::unique_lock<std::mutex> lock(m_workerMutex);
            m_workerCv.wait(lock, [this] { return m_workerTask != 0 || !m_workerRunning; });
            if (!m_workerRunning) break;
            task = m_workerTask;
            type = m_workerType;
            id = m_workerId;
            currentGen = m_workerGen;
            m_workerTask = 0;
        }

        if (task == 1) { // Meta + Streams (Movies/Series)
            MetaResponse resp;
            MetaItem loadedMeta;
            if (m_addonManager.getMeta(type, id, resp)) {
                loadedMeta = resp.meta;
            }

            if (!loadedMeta.videos.empty()) {
                std::vector<Video> sortedEps = loadedMeta.videos;
                std::sort(sortedEps.begin(), sortedEps.end(), [](const Video& a, const Video& b) {
                    if (a.season != b.season) return a.season < b.season;
                    return a.episode < b.episode;
                });

                // Build sorted unique season list
                std::vector<int> seasons;
                for (auto& v : sortedEps) {
                    if (seasons.empty() || seasons.back() != v.season)
                        seasons.push_back(v.season);
                }

                std::string firstEpId;
                if (!sortedEps.empty()) firstEpId = sortedEps[0].id;

                {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    if (m_detailGeneration.load() != currentGen) continue;
                    m_detailEpisodes    = std::move(sortedEps);
                    m_detailSeasons     = std::move(seasons);
                    m_detailSeasonFilter = 0;
                    m_detailEpisodeIndex = 0;
                    m_detailFocus        = DetailFocus::EPISODES;
                    m_detailMeta        = std::move(loadedMeta);
                    m_currentPlayingEpisodeId = firstEpId;
                    m_loadingStreams    = true;
                    m_loadingDetail     = false;
                }

                if (!firstEpId.empty()) {
                    auto rawStreams = m_addonManager.getAllStreams(type, firstEpId);
                    std::vector<Stream> loadedStreams;
                    for (const auto& s : rawStreams) {
                        bool isTorrentStream = !s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0;
                        if (isTorrentStream) {
                            if (!m_addonManager.getEnableTorrents()) continue;
                            if (isFourKOrHigher(s)) continue;
                            static const std::string DEFAULT_MAGNET_TRACKERS =
                                "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"
                                "&tr=udp%3A%2F%2Fopen.demonii.com%3A1337%2Fannounce"
                                "&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce"
                                "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
                                "&tr=udp%3A%2F%2Fexplodie.org%3A6969%2Fannounce";

                            if (!s.url.empty()) {
                                Stream conv = s;
                                if (conv.url.rfind("magnet:", 0) == 0 && conv.url.find("opentrackr.org") == std::string::npos) {
                                    conv.url += DEFAULT_MAGNET_TRACKERS;
                                }
                                loadedStreams.push_back(conv);
                            } else if (!s.infoHash.empty()) {
                                Stream conv = s;
                                conv.url = "magnet:?xt=urn:btih:" + s.infoHash + DEFAULT_MAGNET_TRACKERS;
                                loadedStreams.push_back(conv);
                            }
                        } else {
                            if (!s.url.empty()) {
                                bool isHttp = s.url.rfind("http", 0) == 0;
                                if (!isHttp) continue;
                                loadedStreams.push_back(s);
                            } else if (!s.ytId.empty()) {
                                Stream conv = s;
                                conv.url = "https://www.youtube.com/watch?v=" + s.ytId;
                                loadedStreams.push_back(conv);
                            }
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        if (m_detailGeneration.load() != currentGen) continue;
                        m_detailStreams = std::move(loadedStreams);
                        m_loadingStreams = false;
                    }
                } else {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_loadingStreams = false;
                }
                continue;
            }

            auto rawStreams = m_addonManager.getAllStreams(type, id);
            std::vector<Stream> loadedStreams;
            for (const auto& s : rawStreams) {
                bool isTorrentStream = !s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0;
                if (isTorrentStream) {
                    if (!m_addonManager.getEnableTorrents()) continue;
                    if (isFourKOrHigher(s)) {
                        printf("[Streams] Skipping 4K/UHD torrent stream: %s (%s)\n", s.name.c_str(), s.title.c_str());
                        continue;
                    }
                    static const std::string DEFAULT_MAGNET_TRACKERS =
                        "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"
                        "&tr=udp%3A%2F%2Fopen.demonii.com%3A1337%2Fannounce"
                        "&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce"
                        "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
                        "&tr=udp%3A%2F%2Fexplodie.org%3A6969%2Fannounce";

                    if (!s.url.empty()) {
                        Stream conv = s;
                        if (conv.url.rfind("magnet:", 0) == 0 && conv.url.find("opentrackr.org") == std::string::npos) {
                            conv.url += DEFAULT_MAGNET_TRACKERS;
                        }
                        loadedStreams.push_back(conv);
                    } else if (!s.infoHash.empty()) {
                        Stream conv = s;
                        conv.url = "magnet:?xt=urn:btih:" + s.infoHash + DEFAULT_MAGNET_TRACKERS;
                        loadedStreams.push_back(conv);
                    }
                } else {
                    if (!s.url.empty()) {
                        bool isHttp = s.url.rfind("http", 0) == 0;
                        if (!isHttp) continue;
                        loadedStreams.push_back(s);
                    } else if (!s.ytId.empty()) {
                        Stream conv = s;
                        conv.url = "https://www.youtube.com/watch?v=" + s.ytId;
                        loadedStreams.push_back(conv);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                if (m_detailGeneration.load() != currentGen) continue;
                m_detailMeta = std::move(loadedMeta);
                m_detailStreams = std::move(loadedStreams);
                m_loadingStreams = false;
                m_loadingDetail = false;
            }

        } else if (task == 2) { // Episode Streams
            auto rawStreams = m_addonManager.getAllStreams(type, id);
            std::vector<Stream> loadedStreams;
            for (const auto& s : rawStreams) {
                bool isTorrentStream = !s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0;
                if (isTorrentStream) {
                    if (!m_addonManager.getEnableTorrents()) continue;
                    if (isFourKOrHigher(s)) {
                        printf("[Streams] Skipping 4K/UHD torrent stream: %s (%s)\n", s.name.c_str(), s.title.c_str());
                        continue;
                    }
                    static const std::string DEFAULT_MAGNET_TRACKERS =
                        "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"
                        "&tr=udp%3A%2F%2Fopen.demonii.com%3A1337%2Fannounce"
                        "&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce"
                        "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
                        "&tr=udp%3A%2F%2Fexplodie.org%3A6969%2Fannounce";

                    if (!s.url.empty()) {
                        Stream modified = s;
                        if (modified.url.rfind("magnet:", 0) == 0 && modified.url.find("opentrackr.org") == std::string::npos) {
                            modified.url += DEFAULT_MAGNET_TRACKERS;
                        }
                        loadedStreams.push_back(modified);
                    } else if (!s.infoHash.empty()) {
                        Stream modified = s;
                        modified.url = "magnet:?xt=urn:btih:" + s.infoHash + DEFAULT_MAGNET_TRACKERS;
                        loadedStreams.push_back(modified);
                    }
                } else {
                    if (!s.url.empty() || !s.externalUrl.empty()) {
                        loadedStreams.push_back(s);
                    }
                }
            }
            
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                if (m_detailGeneration.load() != currentGen) continue;
                m_detailStreams = std::move(loadedStreams);
                m_loadingStreams = false;
            }
        }
    }
}

void App::loadDetail(const std::string& type, const std::string& id) {
    m_loadingDetail = true;
    m_loadingStreams = true;
    m_detailEpisodeSelected = false;
    m_currentPlayingEpisodeId.clear();
    m_detailEpisodeIndex = 0;
    m_detailStreamIndex = 0;
    m_detailSeasonFilter = 0;
    int currentGen;
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_detailStreams.clear();
        m_detailEpisodes.clear();
        m_detailFocus = DetailFocus::STREAMS;
        currentGen = ++m_detailGeneration;
    }

    m_detailMeta = MetaItem(); // clear old

    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerTask = 1; // Meta + Streams
        m_workerType = type;
        m_workerId = id;
        m_workerGen = currentGen;
    }
    m_workerCv.notify_one();
}

void App::loadEpisodeStreams(const std::string& epId) {
    if (epId.empty()) return;
    int currentGen;
    std::string epType;
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_currentPlayingEpisodeId = epId;
        m_detailStreams.clear();
        m_detailStreamIndex = 0;
        m_loadingStreams = true;
        currentGen = ++m_detailGeneration;
        epType = m_detailMeta.type;
    }

    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerTask = 2; // Episode streams
        m_workerType = epType;
        m_workerId = epId;
        m_workerGen = currentGen;
    }
    m_workerCv.notify_one();
}

void App::startTorrentPolling() {
    {
        std::lock_guard<std::mutex> lock(m_torrentMutex);
        if (m_torrentPollingActive) return;
        m_torrentPollingActive = true;
        m_torrentStatString = "Connecting to peers...";
        m_torrentPeers = 0;
        m_torrentSpeed = 0.0;
        m_torrentPreloadPercent = -1;
    }

    if (m_torrentPollingThread.joinable()) {
        m_torrentPollingThread.join();
    }

    m_torrentPollingThread = std::thread([this]() {
        printf("[App] Native Torrent stats polling thread started.\n");
        while (true) {
            if (m_shuttingDown) break;

            bool active = false;
            {
                std::lock_guard<std::mutex> lock(m_torrentMutex);
                active = m_torrentPollingActive;
            }

            if (!active || m_screen != Screen::PLAYER) {
                break;
            }

            if (m_player.getPosition() > 0.01) {
                break;
            }

            if (TorrentStream::instance().isActive()) {
                auto stats = TorrentStream::instance().getStats();
                int pct = -1;
                if (stats.piecesTotal > 0) {
                    pct = (int)((stats.piecesDone * 100) / stats.piecesTotal);
                }
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentStatString = stats.statusStr;
                    m_torrentPeers = stats.livePeers;
                    m_torrentSpeed = stats.speedMBps * 1024.0 * 1024.0;
                    m_torrentPreloadPercent = pct;
                }
            }

            // Interruptible sleep — wakes immediately on shutdown signal
            {
                std::unique_lock<std::mutex> lk(m_shutdownMtx);
                m_shutdownCv.wait_for(lk, std::chrono::milliseconds(500),
                                      [this] { return m_shuttingDown.load(); });
            }
            if (m_shuttingDown) break;
        }

        {
            std::lock_guard<std::mutex> lock(m_torrentMutex);
            m_torrentPollingActive = false;
        }
        printf("[App] Native Torrent stats polling thread finished.\n");
    });
}

void App::playStream(const Stream& stream) {
    std::string playUrl = stream.url;
    if (playUrl.empty() && !stream.infoHash.empty()) {
        static const std::string DEFAULT_MAGNET_TRACKERS =
            "&tr=udp%3A%2F%2Ftracker.opentrackr.org%3A1337%2Fannounce"
            "&tr=udp%3A%2F%2Fopen.demonii.com%3A1337%2Fannounce"
            "&tr=udp%3A%2F%2Fopen.stealth.si%3A80%2Fannounce"
            "&tr=udp%3A%2F%2Ftracker.torrent.eu.org%3A451%2Fannounce"
            "&tr=udp%3A%2F%2Fexplodie.org%3A6969%2Fannounce";
        playUrl = "magnet:?xt=urn:btih:" + stream.infoHash + DEFAULT_MAGNET_TRACKERS;
    }

    if (playUrl.empty()) {
        printf("[playStream] ERROR: stream URL is empty, name=%s\n", stream.name.c_str());
        return;
    }
    // Guard against non-playable URLs that somehow slipped through
    bool isPlayable = playUrl.rfind("http", 0) == 0 ||
                      playUrl.rfind("magnet:", 0) == 0;
    if (!isPlayable) {
        printf("[playStream] ERROR: unplayable URL scheme: %s\n", playUrl.substr(0,60).c_str());
        return;
    }

    bool isTorrent = (!stream.infoHash.empty() || playUrl.rfind("magnet:", 0) == 0);
    if (isTorrent && isFourKOrHigher(stream)) {
        printf("[playStream] Blocked 4K/UHD torrent stream: %s (%s)\n", stream.name.c_str(), stream.title.c_str());
        return;
    }

    // Debounce rapid duplicate clicks on the exact same stream (< 1000ms)
    static uint32_t s_lastPlayTime = 0;
    static std::string s_lastPlayUrl = "";
    uint32_t now = SDL_GetTicks();
    if (playUrl == s_lastPlayUrl && (now - s_lastPlayTime < 1000)) {
        printf("[playStream] Debouncing rapid duplicate click on same stream\n");
        return;
    }
    s_lastPlayTime = now;
    s_lastPlayUrl = playUrl;

    // Stop any previous playback session cleanly before starting new stream
    m_player.stop();

    m_showSubList = false;
    m_subAddonMode = false;
    m_subAddonLoading = false;
    {
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_addonSubtitles.clear();
    }
    m_currentPlayingType = m_detailMeta.type;
    if (m_currentPlayingType == "series" && !m_currentPlayingEpisodeId.empty()) {
        m_currentPlayingId = m_currentPlayingEpisodeId;
    } else {
        m_currentPlayingId = m_detailMeta.id;
    }

    {
        std::lock_guard<std::mutex> lock(m_torrentMutex);
        m_lastPlayingMagnet = "";
        m_pendingTorrentPlay = false;
        m_torrentPollingActive = false;
    }

    if (playUrl.rfind("magnet:", 0) == 0) {
        {
            std::lock_guard<std::mutex> lock(m_torrentMutex);
            m_lastPlayingMagnet = playUrl;
            m_torrentStatString = "Connecting to trackers...";
            m_torrentPeers = 0;
            m_torrentSpeed = 0.0;
            m_torrentPreloadPercent = 0;
            m_torrentFailed = false;
            m_torrentErrorMsg.clear();
        }

        m_screen = Screen::PLAYER;
        m_osdShowTime = SDL_GetTicks();
        m_torrentBuffering = true;

        printf("[App] Starting native BitTorrent engine asynchronously for: %s (fileIndex: %d)\n",
               playUrl.substr(0, 60).c_str(), stream.fileIdx);

        std::string headers = stream.httpHeaderFields;
        TorrentStream::instance().startAsync(playUrl, stream.fileIdx, [this, headers](bool ok, const std::string& err) {
            if (ok) {
                printf("[App] Torrent metadata resolved, queuing playback on main thread!\n");
                std::lock_guard<std::mutex> lock(m_torrentMutex);
                m_pendingTorrentPlay = true;
                m_pendingTorrentHeaders = headers;
                m_torrentFailed = false;
                m_torrentErrorMsg.clear();
            } else {
                printf("[App] Torrent start failed or cancelled: %s\n", err.c_str());
                std::lock_guard<std::mutex> lock(m_torrentMutex);
                m_torrentFailed = true;
                m_torrentErrorMsg = err.empty() ? "No seeders or peers available online" : err;
                m_torrentBuffering = false;
            }
        });

        m_library.updateProgress(m_detailMeta.id, m_detailMeta.type,
                                  m_detailMeta.name, m_detailMeta.poster,
                                  m_detailMeta.id, 0.01);
        m_library.save(LIB_FILE);
        return;
    }

    printf("Playing stream URL: %s\n", playUrl.c_str());

    m_player.play(playUrl, stream.httpHeaderFields);
    m_screen = Screen::PLAYER;
    m_osdShowTime = SDL_GetTicks();

    m_library.updateProgress(m_detailMeta.id, m_detailMeta.type,
                              m_detailMeta.name, m_detailMeta.poster,
                              m_detailMeta.id, 0.01);
    m_library.save(LIB_FILE);
}

void App::openSwkbd(std::string& output, const std::string& header) {
#ifdef __SWITCH__
    SwkbdConfig kbd;
    swkbdCreate(&kbd, 0);
    swkbdConfigMakePresetDefault(&kbd);
    if (!header.empty()) {
        swkbdConfigSetHeaderText(&kbd, header.c_str());
    }
    char buf[256] = {0};
    Result rc = swkbdShow(&kbd, buf, sizeof(buf));
    swkbdClose(&kbd);
    if (R_SUCCEEDED(rc)) {
        output = buf;
    }
#else
    printf("\n--- INPUT REQUIRED ---\n");
    if (!header.empty()) {
        printf("%s\n", header.c_str());
    }
    printf("Enter text: ");
    fflush(stdout);

    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), stdin)) {
        std::string str(buf);
        while (!str.empty() && (str.back() == '\n' || str.back() == '\r')) {
            str.pop_back();
        }
        output = str;
    }
#endif
}

void App::shutdown() {
    // Guard to prevent double-shutdown
    if (!m_window) return;

    // 0. Cancel all in-flight HTTP requests so blocked threads return quickly
    m_http.cancel();

    // Signal sleeping threads (torrent poller) to wake up immediately
    m_shuttingDown = true;
    m_shutdownCv.notify_all();

    // 1. Terminate and join all background threads
    m_downloadWorkerRunning = false;
    m_downloadQueueCV.notify_all();
    for (auto& t : m_downloadWorkers) {
        if (t.joinable()) {
            t.join();
        }
    }
    m_downloadWorkers.clear();

    {
        std::lock_guard<std::mutex> lock(m_torrentMutex);
        m_torrentPollingActive = false;
    }
    if (m_torrentPollingThread.joinable()) {
        m_torrentPollingThread.join();
    }
    if (m_homeLoadingThread.joinable()) {
        m_homeLoadingThread.join(); // returns fast: curl aborted
    }
    if (m_searchThread.joinable()) {
        m_searchThread.join(); // returns fast: curl aborted
    }
    m_workerRunning = false;
    m_workerCv.notify_all();
    if (m_detailWorkerThread.joinable()) {
        m_detailWorkerThread.join(); // returns fast: curl aborted
    }
    if (m_installThread.joinable()) {
        m_installThread.join(); // returns fast: curl aborted
    }
    if (m_subSearchThread.joinable()) {
        m_subSearchThread.join();
    }

    // 2. Save configurations
    m_library.save(LIB_FILE);
    m_addonManager.saveConfig(CONFIG_FILE);

    // 3. Clean up input controllers
    if (m_gameController) {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
    }

    // 4. Stop mpv playback before destroying it so it drains buffers fast
    m_player.stop();
    m_player.shutdown();
    delete m_imageCache;
    m_imageCache = nullptr;

    if (m_fontLarge)  { TTF_CloseFont(m_fontLarge);  m_fontLarge  = nullptr; }
    if (m_fontNormal) { TTF_CloseFont(m_fontNormal); m_fontNormal = nullptr; }
    if (m_fontSmall)  { TTF_CloseFont(m_fontSmall);  m_fontSmall  = nullptr; }
    
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window)   { SDL_DestroyWindow(m_window);     m_window   = nullptr; }
    
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

void App::handleDrag(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    printf("[Touch] handleDrag: dx=%d, dy=%d, accumX=%d, accumY=%d, screen=%d\n", 
           dx, dy, m_dragAccumX, m_dragAccumY, (int)m_screen);

    switch (m_screen) {
    case Screen::HOME: {
        std::vector<CatalogRow> localCatalogs;
        {
            std::lock_guard<std::mutex> lock(m_homeMutex);
            localCatalogs = m_homeCatalogs;
        }
        if (localCatalogs.empty() || m_loadingHome) return;

        // Vertical scroll (scroll rows)
        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 60) {
            int rowDiff = -m_dragAccumY / 60;
            int prevRow = m_homeRowIndex;
            m_homeRowIndex = std::clamp(m_homeRowIndex + rowDiff, 0, (int)localCatalogs.size() - 1);
            m_homeColIndex = 0; // reset column
            m_dragAccumY = 0;
            printf("[Touch] Drag HOME Row: %d -> %d\n", prevRow, m_homeRowIndex);
        }

        // Horizontal scroll (scroll posters in the current row)
        m_dragAccumX += dx;
        if (m_homeRowIndex >= 0 && m_homeRowIndex < (int)localCatalogs.size()) {
            int maxCol = (int)localCatalogs[m_homeRowIndex].items.size() - 1;
            if (abs(m_dragAccumX) >= 40) {
                int colDiff = -m_dragAccumX / 40;
                int prevCol = m_homeColIndex;
                m_homeColIndex = std::clamp(m_homeColIndex + colDiff, 0, maxCol);
                m_dragAccumX = 0;
                printf("[Touch] Drag HOME Col: %d -> %d\n", prevCol, m_homeColIndex);
            }
        }
        break;
    }

    case Screen::SEARCH: {
        std::vector<MetaItem> localResults;
        {
            std::lock_guard<std::mutex> lock(m_searchMutex);
            localResults = m_searchResults;
        }
        if (localResults.empty()) return;

        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 20) {
            int diff = -m_dragAccumY / 20;
            int prev = m_searchIndex;
            m_searchIndex = std::clamp(m_searchIndex + diff, 0, (int)localResults.size() - 1);
            m_dragAccumY = 0;
            printf("[Touch] Drag SEARCH Index: %d -> %d\n", prev, m_searchIndex);
        }
        break;
    }

    case Screen::DETAIL: {
        std::vector<Video> localEpisodes;
        std::vector<Stream> localStreams;
        {
            std::lock_guard<std::mutex> lock(m_streamsMutex);
            localEpisodes = m_detailEpisodes;
            localStreams = m_detailStreams;
        }

        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 15) {
            int diff = -m_dragAccumY / 15;
            
            if (m_lastMouseX >= 840) {
                if (!localStreams.empty()) {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailStreamIndex = std::clamp(m_detailStreamIndex + diff, 0, (int)localStreams.size() - 1);
                    m_dragAccumY = 0;
                }
            } else if (!localEpisodes.empty()) {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                m_detailEpisodeIndex = std::clamp(m_detailEpisodeIndex + diff, 0, (int)localEpisodes.size() - 1);
                m_dragAccumY = 0;
            }
        }
        break;
    }

    case Screen::LIBRARY: {
        auto items = m_library.getRecentlyWatched(20);
        if (items.empty()) return;

        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 20) {
            int diff = -m_dragAccumY / 20;
            int prev = m_libraryIndex;
            m_libraryIndex = std::clamp(m_libraryIndex + diff, 0, (int)items.size() - 1);
            m_dragAccumY = 0;
            printf("[Touch] Drag LIBRARY Index: %d -> %d\n", prev, m_libraryIndex);
        }
        break;
    }

    case Screen::ADDONS: {
        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 20) {
            int diff = -m_dragAccumY / 20;
            if (!m_addonDiscoverPane) {
                auto addons = m_addonManager.getAddons();
                if (!addons.empty()) {
                    int prev = m_addonIndex;
                    m_addonIndex = std::clamp(m_addonIndex + diff, 0, (int)addons.size() - 1);
                    printf("[Touch] Drag ADDON Index (Installed): %d -> %d\n", prev, m_addonIndex);
                }
            } else {
                int prev = m_addonDiscoverIndex;
                m_addonDiscoverIndex = std::clamp(m_addonDiscoverIndex + diff, 0, (int)DISCOVER_ADDONS.size() - 1);
                printf("[Touch] Drag ADDON Index (Discover): %d -> %d\n", prev, m_addonDiscoverIndex);
            }
            m_dragAccumY = 0;
        }
        break;
    }

    case Screen::SETTINGS: {
        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 30) {
            int diff = -m_dragAccumY / 30;
            int prev = m_settingsIndex;
            m_settingsIndex = std::clamp(m_settingsIndex + diff, 0, 5);
            m_dragAccumY = 0;
            printf("[Touch] Drag SETTINGS Index: %d -> %d\n", prev, m_settingsIndex);
        }
        break;
    }

    case Screen::PLAYER: {
        if (m_showSubList) {
            m_dragAccumY += dy;
            if (abs(m_dragAccumY) >= 30) {
                int diff = -m_dragAccumY / 30;
                m_dragAccumY = 0;
                if (m_subAddonMode) {
                    std::lock_guard<std::mutex> lock(m_subMutex);
                    if (!m_addonSubtitles.empty()) {
                        m_subAddonIndex = std::clamp(m_subAddonIndex + diff, 0, (int)m_addonSubtitles.size() - 1);
                    }
                } else {
                    auto tracks = m_player.getSubtitleTracks();
                    int totalItems = 1 + (int)tracks.size();
                    m_subListIndex = std::clamp(m_subListIndex + diff, 0, totalItems - 1);
                }
            }
        } else if (m_showAudioList) {
            m_dragAccumY += dy;
            if (abs(m_dragAccumY) >= 30) {
                int diff = -m_dragAccumY / 30;
                m_dragAccumY = 0;
                auto tracks = m_player.getAudioTracks();
                if (!tracks.empty()) {
                    m_audioListIndex = std::clamp(m_audioListIndex + diff, 0, (int)tracks.size() - 1);
                }
            }
        } else {
            // Swipe to scrub (horizontal drag)
            if (!m_isScrubbing) {
                m_isScrubbing = true;
                m_scrubStartPos = m_player.getPosition();
                m_scrubCurrentPos = m_scrubStartPos;
                printf("[Touch] Drag Player: Scrubbing started at pos=%.2f\n", m_scrubStartPos);
            }
            
            m_dragAccumX += dx;
            double duration = m_player.getDuration();
            if (duration > 0.0) {
                m_scrubCurrentPos = m_scrubStartPos + m_dragAccumX * 0.25;
                if (m_scrubCurrentPos < 0.0) m_scrubCurrentPos = 0.0;
                if (m_scrubCurrentPos > duration) m_scrubCurrentPos = duration;
                m_osdShowTime = SDL_GetTicks(); // Keep OSD showing
            }
        }
        break;
    }

    default:
        break;
    }
}

void App::handleLongPress(int x, int y) {
    printf("[Touch] handleLongPress triggered: x=%d, y=%d, screen=%d\n", x, y, (int)m_screen);
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return;

    switch (m_screen) {
    case Screen::HOME: {
        std::vector<CatalogRow> localCatalogs;
        {
            std::lock_guard<std::mutex> lock(m_homeMutex);
            localCatalogs = m_homeCatalogs;
        }
        if (localCatalogs.empty() || m_loadingHome) return;

        int startY = 74;
        int visibleRows = 2;
        int firstRow = m_homeRowIndex > 0 ? m_homeRowIndex - 1 : 0;
        int maxVisibleCols = (SCREEN_W - 80) / (POSTER_W + POSTER_GAP);

        for (int r = firstRow; r < (int)localCatalogs.size() && r < firstRow + visibleRows + 1; r++) {
            auto& row = localCatalogs[r];
            int rowY = startY + (r - firstRow) * ROW_HEIGHT;
            int cardY = rowY + 34;

            int startCol = 0;
            if (r == m_homeRowIndex && m_homeColIndex >= maxVisibleCols)
                startCol = m_homeColIndex - maxVisibleCols + 1;

            for (int c = startCol; c < (int)row.items.size() && c < startCol + maxVisibleCols; c++) {
                int cardX = 40 + (c - startCol) * (POSTER_W + POSTER_GAP);
                if (x >= cardX && x < cardX + POSTER_W && y >= cardY && y < cardY + POSTER_H) {
                    auto& item = row.items[c];
                    m_homeRowIndex = r;
                    m_homeColIndex = c;
                    m_library.toggleBookmark(item.id, item.type, item.name, item.poster);
                    m_library.save(LIB_FILE);
                    printf("[Touch] LongPress HOME bookmarked item ID: %s (%s)\n", item.id.c_str(), item.name.c_str());
                    return;
                }
            }
        }
        break;
    }

    case Screen::DETAIL: {
        m_library.toggleBookmark(m_detailMeta.id, m_detailMeta.type,
                                  m_detailMeta.name, m_detailMeta.poster);
        m_library.save(LIB_FILE);
        printf("[Touch] LongPress DETAIL bookmarked item ID: %s (%s)\n", m_detailMeta.id.c_str(), m_detailMeta.name.c_str());
        break;
    }

    case Screen::LIBRARY: {
        auto items = m_library.getRecentlyWatched(20);
        if (items.empty()) return;

        int itemY = 90;
        for (int i = 0; i < (int)items.size() && itemY < SCREEN_H - 60; i++) {
            if (x >= 30 && x <= SCREEN_W - 30 && y >= itemY - 2 && y <= itemY + 44) {
                m_libraryIndex = i;
                m_library.toggleBookmark(items[i].id, items[i].type, items[i].name, items[i].poster);
                m_library.save(LIB_FILE);
                printf("[Touch] LongPress LIBRARY bookmarked item ID: %s (%s)\n", items[i].id.c_str(), items[i].name.c_str());
                return;
            }
            itemY += 50;
        }
        break;
    }

    case Screen::ADDONS: {
        if (x >= 40 && x <= 620 && y >= 80 && y <= 600) {
            m_addonDiscoverPane = false;
            auto addons = m_addonManager.getAddons();
            if (addons.empty()) return;

            int maxVisible = 7;
            int startIndex = 0;
            if (m_addonIndex >= maxVisible) {
                startIndex = m_addonIndex - maxVisible + 1;
            }

            int itemY = 80 + 45;
            for (int i = startIndex; i < (int)addons.size() && (itemY + 60) <= (80 + 520 - 20); i++) {
                if (x >= 45 && x <= 615 && y >= itemY - 2 && y <= itemY + 54) {
                    m_addonIndex = i;
                    std::string addonId = addons[i].manifest.id;
                    m_addonManager.removeAddon(addonId);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    if (m_addonIndex > 0) m_addonIndex--;
                    loadHomeCatalogs(true);
                    printf("[Touch] LongPress ADDONS removed addon ID: %s\n", addonId.c_str());
                    return;
                }
                itemY += 60;
            }
        }
        break;
    }

    default:
        break;
    }
}

} // namespace ss
