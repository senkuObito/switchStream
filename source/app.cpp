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
#include <rapidjson/document.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

// Switch screen: 1280x720
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// Color palette — true black theme
static constexpr SDL_Color BG_COLOR      = {0,  0,  0,   255}; // true black
static constexpr SDL_Color CARD_COLOR    = {8,  8,  8,   255}; // near-black card
static constexpr SDL_Color CARD_HL       = {28, 28, 40,  255}; // dark highlight
static constexpr SDL_Color ACCENT        = {100, 120, 255, 255};
static constexpr SDL_Color TEXT_PRIMARY   = {240, 240, 245, 255};
static constexpr SDL_Color TEXT_SECONDARY = {150, 150, 170, 255};
static constexpr SDL_Color NAV_BG        = {0,  0,  0,   255}; // true black

// Poster dimensions
static constexpr int POSTER_W = 150;
static constexpr int POSTER_H = 225;
static constexpr int POSTER_GAP = 16;
static constexpr int ROW_HEIGHT = 280;

struct DiscoverAddon {
    std::string name;
    std::string description;
    std::string url;
};

static const std::vector<DiscoverAddon> DISCOVER_ADDONS = {
    // --- Catalog Addons (provide home page content + search) ---
    {"Cinemeta", "The official addon for movie and series catalogs & search.", "https://v3-cinemeta.strem.io/manifest.json"},
    {"Anime Kitsu", "Kitsu.io Anime catalog and metadata.", "https://anime-kitsu.strem.fun/manifest.json"},
    {"IndiaStreams", "Trending movies and shows from Indian platforms (Tamil, Hindi, etc.).", "https://indiastreams.rdata.in/manifest.json"},
    {"Indian Regional Catalog", "Indian regional movies catalog by language (Tamil, Telugu, Hindi).", "https://83e20802dcf1-indian-streams.baby-beamup.club/manifest.json"},
    {"Streaming Catalogs", "Catalogs from Netflix, Disney+, HBO, Prime, etc.", "https://7a82163c306e-streaming-catalogs.baby-beamup.club/manifest.json"},
    {"CyberFlix Catalogs", "Movie and series catalogs sorted by streaming provider.", "https://cyberflix.elfhosted.com/manifest.json"},
    {"TMDB Collections", "Movie collections grouped by franchise.", "https://61ab9c85a149-tmdb-collections.baby-beamup.club/manifest.json"},

    // --- Stream Addons (provide playable stream links) ---
    {"Torrentio", "Torrent streams from YTS, RARBG, 1337x, etc.", "https://torrentio.strem.fun/manifest.json"},
    {"Pengu", "Multi-source scraping stream addon (configured for 1080p/720p/480p).", "https://pengu.uk/%7B%22source_111477%22%3A%22on%22%2C%22source_4khdhub%22%3A%22on%22%2C%22source_cinefreak%22%3A%22on%22%2C%22source_aniwaves%22%3A%22on%22%2C%22source_moviebox%22%3A%22on%22%2C%22source_moviesdrives%22%3A%22on%22%2C%22source_allmovieland%22%3A%22on%22%2C%22source_overflix%22%3A%22on%22%2C%22source_vaplayer%22%3A%22on%22%2C%22source_vidking%22%3A%22on%22%2C%22source_animesuge%22%3A%22on%22%2C%22source_aether%22%3A%22on%22%2C%22source_vidlink%22%3A%22on%22%2C%22source_hdghartv%22%3A%22on%22%2C%22source_scloud%22%3A%22on%22%2C%22res_2160%22%3A%22on%22%2C%22res_1080%22%3A%22on%22%2C%22res_720%22%3A%22on%22%2C%22res_480%22%3A%22on%22%2C%22disable_direct%22%3A%22on%22%7D/manifest.json"},
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
    if (IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) == 0) return false;

    m_downloadWorkerRunning = true;
    m_downloadWorkerThread = std::thread(&App::downloadWorkerLoop, this);

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

    // Image cache — 32MB max (lightweight)
    m_imageCache = new ImageCache(m_renderer, 32);

    // Load saved data
    m_addonManager.loadConfig(CONFIG_FILE);
    m_library.load(LIB_FILE);

    {
        const std::vector<std::string> defaultAddons = {
            "https://v3-cinemeta.strem.io/manifest.json",
            "https://torrentio.strem.fun/manifest.json",
            "https://cyberflix.elfhosted.com/manifest.json",
            "https://pengu.uk/manifest.json",
            "https://free.flixnest.app/manifest.json",
            "https://opensubtitles-v3.strem.io/manifest.json",
            "https://watchhub.strem.io/manifest.json",
            "https://anime-kitsu.strem.fun/manifest.json",
            "https://badboysxs-morpheus.hf.space/manifest.json",
            "https://stremio.yukistreams.xyz/manifest.json",
            "https://sword-watch.vercel.app/manifest.json",
            "https://nagare.nexioapp.org/manifest.json"
        };
        bool updatedAddons = false;
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
                printf("Installing missing default addon: %s\n", url.c_str());
                m_addonManager.installAddon(url);
                updatedAddons = true;
            }
        }
        if (updatedAddons) {
            m_addonManager.saveConfig(CONFIG_FILE);
        }
    }

    // Load home catalogs
    loadHomeCatalogs();

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

        // If playing and finished, go back to detail screen
        if (m_screen == Screen::PLAYER && m_player.isFinished()) {
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
            if (m_loadingDetail || m_loadingStreams) {
                if (kDown & HidNpadButton_B) {
                    if (m_loadingStreams && m_detailEpisodeSelected) {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_loadingStreams = false;
                        m_detailEpisodeSelected = false;
                    } else {
                        m_screen = Screen::HOME;
                        m_loadingDetail = false;
                    }
                }
                break;
            }

            std::lock_guard<std::mutex> lock(m_streamsMutex);

            if (!m_detailEpisodeSelected && !m_detailEpisodes.empty()) {
                if (kDown & HidNpadButton_Down) m_detailEpisodeIndex++;
                if (kDown & HidNpadButton_Up) m_detailEpisodeIndex--;
                if (m_detailEpisodeIndex < 0) m_detailEpisodeIndex = 0;
                if (m_detailEpisodeIndex >= (int)m_detailEpisodes.size())
                    m_detailEpisodeIndex = (int)m_detailEpisodes.size() - 1;
                
                if (kDown & HidNpadButton_A) {
                    std::string epId = m_detailEpisodes[m_detailEpisodeIndex].id;
                    m_detailEpisodeSelected = true;
                    m_detailStreams.clear();
                    m_detailStreamIndex = 0;
                    m_loadingStreams = true;
                    
                    if (m_detailLoadingThread.joinable()) {
                        m_detailLoadingThread.join();
                    }
                    m_detailLoadingThread = std::thread([this, epId]() {
                        auto rawStreams = m_addonManager.getAllStreams("series", epId);
                        std::vector<Stream> loadedStreams;
                        for (const auto& s : rawStreams) {
                            bool isTorrentStream = !s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0;
                            if (isTorrentStream) {
                                if (!m_addonManager.getEnableTorrents()) continue;
                                if (!s.url.empty()) {
                                    loadedStreams.push_back(s);
                                } else if (!s.infoHash.empty()) {
                                    Stream modified = s;
                                    modified.url = m_addonManager.getTorrServerHost() + "/stream?link=" + s.infoHash + "&index=" + std::to_string(s.fileIdx > -1 ? s.fileIdx : 1) + "&play";
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
                            m_detailStreams = std::move(loadedStreams);
                            m_loadingStreams = false;
                        }
                    });
                }
            } else {
                if (kDown & HidNpadButton_Down) m_detailStreamIndex++;
                if (kDown & HidNpadButton_Up) m_detailStreamIndex--;
                if (m_detailStreamIndex < 0) m_detailStreamIndex = 0;
                if (m_detailStreamIndex >= (int)m_detailStreams.size())
                    m_detailStreamIndex = (int)m_detailStreams.size() - 1;
                
                if (kDown & HidNpadButton_A && !m_detailStreams.empty()) {
                    playStream(m_detailStreams[m_detailStreamIndex]);
                }
            }
        }
        if (kDown & HidNpadButton_X) {
            m_library.toggleBookmark(m_detailMeta.id, m_detailMeta.type,
                                      m_detailMeta.name, m_detailMeta.poster);
            m_library.save(LIB_FILE);
        }
        if (kDown & HidNpadButton_B) m_screen = Screen::HOME;
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
                    loadHomeCatalogs(); // Refresh catalogs to hide/show catalog on home screen!
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
                loadHomeCatalogs();  // refresh
            }
        }

        if (kDown & HidNpadButton_B) {
            m_screen = Screen::HOME;
            loadHomeCatalogs();
        }
        break;
    }

    case Screen::PLAYER: {
        uint32_t now = SDL_GetTicks();
        bool isOsdVisible = (now - m_osdShowTime < 4000);

        if (kDown) {
            if (m_showSubList) {
                m_osdShowTime = now;
                auto tracks = m_player.getSubtitleTracks();
                if (kDown & HidNpadButton_Up) {
                    m_subListIndex = std::clamp(m_subListIndex - 1, 0, (int)tracks.size() - 1);
                }
                else if (kDown & HidNpadButton_Down) {
                    m_subListIndex = std::clamp(m_subListIndex + 1, 0, (int)tracks.size() - 1);
                }
                else if (kDown & HidNpadButton_A) {
                    if (!tracks.empty()) {
                        m_player.setSubtitleTrack(tracks[m_subListIndex].id);
                    }
                    m_showSubList = false;
                }
                else if (kDown & HidNpadButton_B) {
                    m_showSubList = false;
                }
                break;
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

            if (!isOsdVisible) {
                // If controls are hidden, B exits immediately.
                // Any other button press wakes up the controls.
                if (kDown & HidNpadButton_B) {
                    {
                        std::lock_guard<std::mutex> lock(m_torrentMutex);
                        m_torrentPollingActive = false;
                    }
                    if (m_torrentPollingThread.joinable()) {
                        m_torrentPollingThread.join();
                    }
                    m_player.stop();
                    m_screen = Screen::DETAIL;
                } else {
                    m_osdShowTime = now;
                }
            } else {
                // Controls are visible. B hides the controls, any other action is processed.
                m_osdShowTime = now;
                if (kDown & HidNpadButton_B) {
                    m_osdShowTime = now - 4000; // hide OSD
                }
                else if (kDown & HidNpadButton_A) {
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
                std::remove(CONFIG_FILE);
                std::remove(LIB_FILE);
                m_library = Library();
                // Re-initialize config (will populate defaults since file is gone)
                m_addonManager.loadConfig(CONFIG_FILE);
                if (m_addonManager.getAddons().empty()) {
                    const std::vector<std::string> defaultAddons = {
                        "https://v3-cinemeta.strem.io/manifest.json",
                        "https://torrentio.strem.fun/manifest.json",
                        "https://cyberflix.elfhosted.com/manifest.json",
                        "https://pengu.uk/manifest.json",
                        "https://free.flixnest.app/manifest.json",
                        "https://opensubtitles-v3.strem.io/manifest.json",
                        "https://watchhub.strem.io/manifest.json",
                        "https://anime-kitsu.strem.fun/manifest.json",
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
                m_settingsIndex = 0;
                m_screen = Screen::HOME;
                loadHomeCatalogs();
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
        // ─── Header Navigation Click ───
        // "[Y] Search   [L] Library   [R] Addons   [X] Settings   [+] Exit"
        if (y >= 10 && y <= 45) {
            if (x >= 400 && x < 490) {
                std::string query;
                openSwkbd(query, "Search movies & series");
                if (!query.empty()) {
                    m_searchQuery = query;
                    performSearch(m_searchQuery);
                    m_screen = Screen::SEARCH;
                }
                return;
            }
            if (x >= 500 && x < 610) {
                m_screen = Screen::LIBRARY;
                return;
            }
            if (x >= 620 && x < 720) {
                m_screen = Screen::ADDONS;
                return;
            }
            if (x >= 730 && x < 850) {
                m_settingsIndex = 0;
                m_screen = Screen::SETTINGS;
                return;
            }
            if (x >= 860 && x < 950) {
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

        int startY = 70;
        int visibleRows = 2;
        int firstRow = m_homeRowIndex > 0 ? m_homeRowIndex - 1 : 0;
        int maxVisibleCols = (SCREEN_W - 80) / (POSTER_W + POSTER_GAP);

        for (int r = firstRow; r < (int)localCatalogs.size() && r < firstRow + visibleRows + 1; r++) {
            auto& row = localCatalogs[r];
            int rowY = startY + (r - firstRow) * ROW_HEIGHT;
            int cardY = rowY + 30;

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
        // "Bookmark   Back" at infoX = 270, y = 175
        int infoX = 270;
        if (y >= 165 && y <= 195) {
            if (x >= infoX && x < infoX + 120) {
                m_library.toggleBookmark(m_detailMeta.id, m_detailMeta.type,
                                          m_detailMeta.name, m_detailMeta.poster);
                m_library.save(LIB_FILE);
                return;
            }
            if (x >= infoX + 130 && x < infoX + 220) {
                m_screen = Screen::HOME;
                return;
            }
        }

        bool isLoading = false;
        std::vector<Stream> localStreams;
        int currentSelIndex = 0;
        bool epsSelected = false;
        int currentEpIndex = 0;
        std::vector<Video> localEpisodes;
        {
            std::lock_guard<std::mutex> lock(m_streamsMutex);
            isLoading = m_loadingStreams;
            localStreams = m_detailStreams;
            currentSelIndex = m_detailStreamIndex;
            epsSelected = m_detailEpisodeSelected;
            currentEpIndex = m_detailEpisodeIndex;
            localEpisodes = m_detailEpisodes;
        }

        if (isLoading || m_loadingDetail) return;

        bool isModal = epsSelected;

        if (isModal) {
            int modalW = 800;
            int modalH = 500;
            int modalX = SCREEN_W / 2 - modalW / 2;
            int modalY = SCREEN_H / 2 - modalH / 2;

            if (x < modalX || x > modalX + modalW || y < modalY || y > modalY + modalH) {
                // Clicked outside modal -> cancel episode selection
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                m_detailEpisodeSelected = false;
                m_loadingStreams = false;
                return;
            }

            if (localStreams.empty()) return;

            int maxVisible = 9;
            int startIndex = 0;
            if (currentSelIndex >= maxVisible) {
                startIndex = currentSelIndex - maxVisible + 1;
            }

            int streamY = modalY + 80;
            for (int i = startIndex; i < (int)localStreams.size() && i < startIndex + maxVisible; i++) {
                int itemMinY = streamY - 2;
                int itemMaxY = streamY + 34;
                int itemMinX = modalX + 25;
                int itemMaxX = modalX + modalW - 25;

                if (x >= itemMinX && x <= itemMaxX && y >= itemMinY && y <= itemMaxY) {
                    std::lock_guard<std::mutex> lock(m_streamsMutex);
                    m_detailStreamIndex = i;
                    playStream(localStreams[i]);
                    return;
                }
                streamY += 40;
            }
            return;
        }

        // Episode List Items (Non-Modal)
        if (!epsSelected && !localEpisodes.empty()) {
            int remainingH = SCREEN_H - 240 - 40;
            int maxVisible = remainingH / 40;
            if (maxVisible < 3) maxVisible = 3;

            int startIndex = 0;
            if (currentEpIndex >= maxVisible) {
                startIndex = currentEpIndex - maxVisible + 1;
            }

            int listY = 240;
            for (int i = startIndex; i < (int)localEpisodes.size() && i < startIndex + maxVisible; i++) {
                int itemMinY = listY - 2;
                int itemMaxY = listY + 34;
                int itemMinX = infoX - 5;
                int itemMaxX = SCREEN_W - 40;

                if (x >= itemMinX && x <= itemMaxX && y >= itemMinY && y <= itemMaxY) {
                    std::string epId = localEpisodes[i].id;
                    {
                        std::lock_guard<std::mutex> lock(m_streamsMutex);
                        m_detailEpisodeIndex = i;
                        m_detailEpisodeSelected = true;
                        m_detailStreams.clear();
                        m_detailStreamIndex = 0;
                        m_loadingStreams = true;
                    }
                    if (m_detailLoadingThread.joinable()) {
                        m_detailLoadingThread.join();
                    }
                    m_detailLoadingThread = std::thread([this, epId]() {
                        auto rawStreams = m_addonManager.getAllStreams("series", epId);
                        std::vector<Stream> loadedStreams;
                        for (const auto& s : rawStreams) {
                            bool isTorrentStream = !s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0;
                            if (isTorrentStream) {
                                if (!m_addonManager.getEnableTorrents()) continue;
                                if (!s.url.empty()) {
                                    loadedStreams.push_back(s);
                                } else if (!s.infoHash.empty()) {
                                    Stream modified = s;
                                    modified.url = m_addonManager.getTorrServerHost() + "/stream?link=" + s.infoHash + "&index=" + std::to_string(s.fileIdx > -1 ? s.fileIdx : 1) + "&play";
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
                            m_detailStreams = std::move(loadedStreams);
                            m_loadingStreams = false;
                        }
                    });
                    return;
                }
                listY += 40;
            }
            return;
        }

        // Stream Items (Non-Modal for movies)
        if (localStreams.empty()) return;

        int remainingH = SCREEN_H - 240 - 40;
        int maxVisible = remainingH / 40;
        if (maxVisible < 3) maxVisible = 3;

        int startIndex = 0;
        if (currentSelIndex >= maxVisible) {
            startIndex = currentSelIndex - maxVisible + 1;
        }

        int streamY = 240;
        for (int i = startIndex; i < (int)localStreams.size() && i < startIndex + maxVisible; i++) {
            int itemMinY = streamY - 2;
            int itemMaxY = streamY + 34;
            int itemMinX = infoX - 5;
            int itemMaxX = SCREEN_W - 40;

            if (x >= itemMinX && x <= itemMaxX && y >= itemMinY && y <= itemMaxY) {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                m_detailStreamIndex = i;
                playStream(localStreams[i]);
                return;
            }
            streamY += 40;
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
            loadHomeCatalogs();
            return;
        }

        if (y >= 50 && y <= 75) {
            if (x >= 600 && x <= 750) {
                std::string url;
                openSwkbd(url, "Enter addon manifest URL");
                if (!url.empty()) {
                    m_addonManager.installAddon(url);
                    m_addonManager.saveConfig(CONFIG_FILE);
                    loadHomeCatalogs();
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
                    addons[i].enabled = !addons[i].enabled;
                    m_addonManager.saveConfig(CONFIG_FILE);
                    loadHomeCatalogs();
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
                    loadHomeCatalogs();
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
                    std::remove(CONFIG_FILE);
                    std::remove(LIB_FILE);
                    m_library = Library();
                    m_addonManager.loadConfig(CONFIG_FILE);
                    if (m_addonManager.getAddons().empty()) {
                        const std::vector<std::string> defaultAddons = {
                            "https://v3-cinemeta.strem.io/manifest.json",
                            "https://torrentio.strem.fun/manifest.json",
                            "https://cyberflix.elfhosted.com/manifest.json",
                            "https://pengu.uk/manifest.json",
                            "https://free.flixnest.app/manifest.json",
                            "https://opensubtitles-v3.strem.io/manifest.json",
                            "https://watchhub.strem.io/manifest.json",
                            "https://anime-kitsu.strem.fun/manifest.json",
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
                    m_imageCache->clear();
                    loadHomeCatalogs();
                    m_screen = Screen::HOME;
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
        if (pos <= 0.01) {
            // Loading screen: back button touch handler
            int cardY = SCREEN_H/2 - 160;
            int btnW = 240;
            int btnH = 34;
            int btnX = SCREEN_W/2 - btnW/2;
            int btnY = cardY + 250;
            if (x >= btnX && x <= btnX + btnW && y >= btnY && y <= btnY + btnH) {
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentPollingActive = false;
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
            int subW = 480;
            int subH = 400;
            int subX = SCREEN_W/2 - subW/2;
            int subY = SCREEN_H/2 - subH/2;

            if (x >= subX && x <= subX + subW && y >= subY && y <= subY + subH) {
                auto tracks = m_player.getSubtitleTracks();
                int maxVis = 6;
                int startIdx = 0;
                if (m_subListIndex >= maxVis) startIdx = m_subListIndex - maxVis + 1;
                
                int rowY = subY + 65;
                for (int i = startIdx; i < (int)tracks.size() && i < startIdx + maxVis; i++) {
                    if (x >= subX + 20 && x <= subX + subW - 20 && y >= rowY && y <= rowY + 42) {
                        m_player.setSubtitleTrack(tracks[i].id);
                        m_showSubList = false;
                        printf("[Touch] Subtitle track %d selected via click\n", tracks[i].id);
                        m_osdShowTime = now;
                        return;
                    }
                    rowY += 48;
                }
            } else {
                // Click outside closes the dialog
                m_showSubList = false;
                m_osdShowTime = now;
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
                {
                    std::lock_guard<std::mutex> lock(m_torrentMutex);
                    m_torrentPollingActive = false;
                }
                if (m_torrentPollingThread.joinable()) {
                    m_torrentPollingThread.join();
                }
                m_player.stop();
                m_screen = Screen::DETAIL;
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
        m_imageCache->store(img.url, img.data.data(), img.data.size());
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

void App::renderPlayer() {
    double pos = m_player.getPosition();
    if (pos <= 0.01) {
        // Keep the render context active to prevent libmpv warning/hang
        m_player.render(SCREEN_W, SCREEN_H);
        SDL_RenderFlush(m_renderer);

        SDL_SetRenderDrawColor(m_renderer, 10, 10, 15, 255);
        SDL_RenderClear(m_renderer);

        std::string statStr;
        int peers = 0;
        double speed = 0.0;
        int percent = -1;
        bool isTorrent = false;

        {
            std::lock_guard<std::mutex> lock(m_torrentMutex);
            isTorrent = !m_lastPlayingMagnet.empty();
            statStr = m_torrentStatString;
            peers = m_torrentPeers;
            speed = m_torrentSpeed;
            percent = m_torrentPreloadPercent;
        }

        // Center card coordinates
        int cardX = SCREEN_W/2 - 300;
        int cardY = SCREEN_H/2 - 160;
        int cardW = 600;
        int cardH = 320;

        // Glassmorphic card body
        drawFilledRoundRect(cardX, cardY, cardW, cardH, 12, {0, 0, 0, 230});
        
        // Specular glass borders
        drawRoundRect(cardX, cardY, cardW, cardH, 12, {255, 255, 255, 35});

        if (isTorrent) {
            drawSpinner(SCREEN_W/2, cardY + 45, 20);
            drawTextCentered("Loading torrent video...", SCREEN_W/2, cardY + 75, TEXT_PRIMARY, m_fontLarge);
            
            std::string msg = "Status: " + statStr;
            if (percent >= 0) {
                msg += " (" + std::to_string(percent) + "%)";
            }
            drawTextCentered(msg, SCREEN_W/2, cardY + 110, {100, 180, 255, 255}, m_fontNormal);

            std::string speedStr;
            if (speed >= 1024 * 1024) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f MB/s", speed / (1024.0 * 1024.0));
                speedStr = buf;
            } else if (speed >= 1024) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f KB/s", speed / 1024.0);
                speedStr = buf;
            } else {
                speedStr = std::to_string((int)speed) + " B/s";
            }
            
            std::string peerInfo = "Peers: " + std::to_string(peers) + "  |  Speed: " + speedStr;
            drawTextCentered(peerInfo, SCREEN_W/2, cardY + 138, TEXT_SECONDARY, m_fontSmall);

            // Add helpful warnings for independent execution
            std::string host = m_addonManager.getTorrServerHost();
            if (host.find("127.0.0.1") != std::string::npos || host.find("localhost") != std::string::npos) {
                drawTextCentered("Warning: Local TorrServer cannot run directly on Switch.", SCREEN_W/2, cardY + 175, {255, 140, 140, 255}, m_fontSmall);
                drawTextCentered("Please configure a remote PC's TorrServer IP in Settings,", SCREEN_W/2, cardY + 195, {255, 180, 140, 255}, m_fontSmall);
                drawTextCentered("or configure a Debrid service in your addon to stream directly.", SCREEN_W/2, cardY + 215, {255, 200, 140, 255}, m_fontSmall);
            } else if (statStr.find("offline") != std::string::npos || statStr.find("unreachable") != std::string::npos) {
                drawTextCentered("Error: Cannot connect to remote TorrServer.", SCREEN_W/2, cardY + 175, {255, 140, 140, 255}, m_fontSmall);
                drawTextCentered("Host URL: " + host, SCREEN_W/2, cardY + 195, {150, 200, 255, 255}, m_fontSmall);
                drawTextCentered("Tip: Configure a Debrid service to stream serverless.", SCREEN_W/2, cardY + 215, {150, 255, 150, 255}, m_fontSmall);
            }
        } else {
            drawSpinner(SCREEN_W/2, cardY + 80, 28);
            drawTextCentered("Loading video stream...", SCREEN_W/2, cardY + 140, TEXT_PRIMARY, m_fontLarge);
        }

        // Action button pill
        int btnW = 240;
        int btnH = 34;
        int btnX = SCREEN_W/2 - btnW/2;
        int btnY = cardY + 250;
        
        drawFilledRoundRect(btnX, btnY, btnW, btnH, 8, {255, 255, 255, 20});
        drawRoundRect(btnX, btnY, btnW, btnH, 8, {255, 255, 255, 45});
        drawTextCentered("[B] Back to Details", SCREEN_W/2, btnY + 7, {255, 120, 120, 255}, m_fontSmall);

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
            int subW = 480;
            int subH = 400;
            int subX = SCREEN_W/2 - subW/2;
            int subY = SCREEN_H/2 - subH/2;

            drawFilledRoundRect(subX, subY, subW, subH, 14, {0, 0, 0, 240});
            drawRoundRect(subX, subY, subW, subH, 14, {255, 255, 255, 40});

            drawText("Select Subtitles", subX + 25, subY + 20, ACCENT, m_fontNormal);

            auto tracks = m_player.getSubtitleTracks();
            int maxVis = 6;
            int startIdx = 0;
            if (m_subListIndex >= maxVis) startIdx = m_subListIndex - maxVis + 1;
            
            int rowY = subY + 65;
            for (int i = startIdx; i < (int)tracks.size() && i < startIdx + maxVis; i++) {
                bool sel = (i == m_subListIndex);
                SDL_Color bg = sel ? CARD_HL : (tracks[i].selected ? SDL_Color{0, 180, 255, 30} : SDL_Color{255, 255, 255, 10});
                drawFilledRoundRect(subX + 20, rowY, subW - 40, 42, 6, bg);
                if (sel) {
                    drawRoundRect(subX + 20, rowY, subW - 40, 42, 6, ACCENT);
                }
                
                std::string name = tracks[i].name;
                if (tracks[i].selected) name = "[Active] " + name;
                drawText(name, subX + 35, rowY + 9, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
                rowY += 48;
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

                // Build a concise label: name + first line of title description
                std::string label = localStreams[i].name;
                if (!localStreams[i].title.empty()) {
                    std::string desc = localStreams[i].title;
                    // Take only the first line of the description
                    auto nl = desc.find('\n');
                    if (nl != std::string::npos) desc = desc.substr(0, nl);
                    if (desc.size() > 60) desc = desc.substr(0, 57) + "...";
                    label += ": " + desc;
                }
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

void App::renderHome() {
    drawText("SwitchStream", 40, 20, ACCENT, m_fontLarge);
    drawText("[Y] Search   [L] Library   [R] Addons   [X] Settings   [+] Exit",
             400, 28, TEXT_SECONDARY, m_fontSmall);

    if (m_loadingHome) {
        drawSpinner(SCREEN_W/2, SCREEN_H/2 - 40, 22);
        drawTextCentered("Loading catalogs...", SCREEN_W/2, SCREEN_H/2 + 25, TEXT_SECONDARY);
        return;
    }

    std::vector<CatalogRow> localCatalogs;
    {
        std::lock_guard<std::mutex> lock(m_homeMutex);
        localCatalogs = m_homeCatalogs;
    }

    if (localCatalogs.empty()) {
        drawText("No catalogs loaded. Press [R] to manage addons or [X] for Settings.",
                 SCREEN_W/2 - 250, SCREEN_H/2, TEXT_SECONDARY);
        return;
    }

    int startY = 70;
    // Only render visible rows (lightweight)
    int visibleRows = 2;
    int firstRow = m_homeRowIndex > 0 ? m_homeRowIndex - 1 : 0;

    for (int r = firstRow; r < (int)localCatalogs.size() && r < firstRow + visibleRows + 1; r++) {
        auto& row = localCatalogs[r];
        int y = startY + (r - firstRow) * ROW_HEIGHT;
        if (y > SCREEN_H) break;

        // Row title
        std::string title = row.addonName + " — " + row.catalogName;
        drawText(title, 40, y, (r == m_homeRowIndex) ? ACCENT : TEXT_PRIMARY, m_fontNormal);

        // Poster cards
        int x = 40;
        int maxVisible = (SCREEN_W - 80) / (POSTER_W + POSTER_GAP);
        int startCol = 0;
        if (r == m_homeRowIndex && m_homeColIndex >= maxVisible)
            startCol = m_homeColIndex - maxVisible + 1;

        for (int c = startCol; c < (int)row.items.size() && c < startCol + maxVisible; c++) {
            int cardX = x + (c - startCol) * (POSTER_W + POSTER_GAP);
            int cardY = y + 30;
            bool selected = (r == m_homeRowIndex && c == m_homeColIndex);

            // Card background
            SDL_Color cardBg = selected ? CARD_HL : CARD_COLOR;
            drawFilledRect(cardX - 4, cardY - 4, POSTER_W + 8, POSTER_H + 8, cardBg);

            // Draw actual poster image
            drawPoster(row.items[c], cardX, cardY, POSTER_W, POSTER_H);

            // Overlay name at bottom of card
            std::string name = row.items[c].name;
            if (name.size() > 18) name = name.substr(0, 16) + "..";
            // Draw a subtle dark semi-transparent band for readability
            drawFilledRect(cardX, cardY + POSTER_H - 30, POSTER_W, 30, {0, 0, 0, 180});
            drawText(name, cardX + 4, cardY + POSTER_H - 24, TEXT_PRIMARY, m_fontSmall);

            if (selected) {
                drawRect(cardX - 4, cardY - 4, POSTER_W + 8, POSTER_H + 8, ACCENT);
            }
        }
    }

    // Draw Author Legend
    drawText("Author: Antigravity", SCREEN_W - 180, SCREEN_H - 30, {100, 100, 120, 255}, m_fontSmall);
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
        drawSpinner(SCREEN_W/2, SCREEN_H/2 - 40, 22);
        drawTextCentered("Loading item details & stream links...", SCREEN_W/2, SCREEN_H/2 + 25, TEXT_SECONDARY);
        return;
    }

    // Left side: actual poster
    drawPoster(m_detailMeta, 40, 80, 200, 300);

    // Right side: info
    int infoX = 270;
    drawText(m_detailMeta.name, infoX, 80, TEXT_PRIMARY, m_fontLarge);

    std::string info = m_detailMeta.type;
    if (!m_detailMeta.releaseInfo.empty()) info += " · " + m_detailMeta.releaseInfo;
    if (!m_detailMeta.runtime.empty()) info += " · " + m_detailMeta.runtime;
    if (!m_detailMeta.imdbRating.empty()) info += " · ⭐ " + m_detailMeta.imdbRating;
    drawText(info, infoX, 115, TEXT_SECONDARY, m_fontSmall);

    // Description (wrapped)
    std::string desc = m_detailMeta.description;
    if (desc.size() > 500) desc = desc.substr(0, 497) + "...";
    int wrapW = SCREEN_W - infoX - 45;
    int descH = drawTextWrapped(desc, infoX, 140, wrapW, TEXT_SECONDARY, m_fontSmall);
    if (descH == 0) descH = 15;

    // Bookmark hint (dynamic Y)
    int bookmarkY = 140 + descH + 12;
    const LibraryItem* libItem = m_library.getItem(m_detailMeta.id);
    std::string bmText = (libItem && libItem->bookmarked) ? "[X] Unbookmark" : "[X] Bookmark";
    drawText(bmText + "   [B] Back", infoX, bookmarkY, {100, 180, 255, 255}, m_fontSmall);

    // Streams Header (dynamic Y)
    int streamsHeaderY = bookmarkY + 30;

    bool isLoading = false;
    std::vector<Stream> localStreams;
    int currentSelIndex = 0;
    bool epsSelected = false;
    int currentEpIndex = 0;
    std::vector<Video> localEpisodes;
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        isLoading = m_loadingStreams;
        localStreams = m_detailStreams;
        currentSelIndex = m_detailStreamIndex;
        epsSelected = m_detailEpisodeSelected;
        currentEpIndex = m_detailEpisodeIndex;
        localEpisodes = m_detailEpisodes;
    }

    if (!localEpisodes.empty()) {
        drawText("Select Episode:", infoX, streamsHeaderY, ACCENT, m_fontNormal);
        int streamsStartY = streamsHeaderY + 32;

        int remainingH = SCREEN_H - streamsStartY - 40;
        int maxVisible = remainingH / 40;
        if (maxVisible < 3) maxVisible = 3;

        int startIndex = 0;
        if (currentEpIndex >= maxVisible) {
            startIndex = currentEpIndex - maxVisible + 1;
        }

        int y = streamsStartY;
        if (startIndex > 0) {
            drawText("^ More episodes above...", infoX, y - 18, TEXT_SECONDARY, m_fontSmall);
        }

        for (int i = startIndex; i < (int)localEpisodes.size() && i < startIndex + maxVisible; i++) {
            bool sel = (i == currentEpIndex);
            if (sel) drawFilledRect(infoX - 5, y - 2, SCREEN_W - infoX - 40, 36, CARD_HL);

            auto& ep = localEpisodes[i];
            std::string label = "S" + std::to_string(ep.season) + " E" + std::to_string(ep.episode);
            if (!ep.title.empty()) label += " — " + ep.title;
            if (label.size() > 90) label = label.substr(0, 87) + "...";

            drawText(label, infoX + 5, y + 4, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
            y += 40;
        }

        if (startIndex + maxVisible < (int)localEpisodes.size()) {
            drawText("v More episodes below...", infoX, y + 2, TEXT_SECONDARY, m_fontSmall);
        }

        if (!epsSelected) return;
    }

    bool isModal = epsSelected;
    
    if (isModal) {
        // Draw fullscreen dim
        drawFilledRect(0, 0, SCREEN_W, SCREEN_H, {0, 0, 0, 180});
        
        // Draw modal box
        int modalW = 800;
        int modalH = 500;
        int modalX = SCREEN_W / 2 - modalW / 2;
        int modalY = SCREEN_H / 2 - modalH / 2;
        drawFilledRect(modalX, modalY, modalW, modalH, {30, 30, 45, 255});
        drawRect(modalX, modalY, modalW, modalH, ACCENT);
        
        auto& ep = localEpisodes[currentEpIndex];
        std::string epLabel = "S" + std::to_string(ep.season) + " E" + std::to_string(ep.episode);
        
        if (isLoading) {
            drawSpinner(SCREEN_W/2, SCREEN_H/2 - 20, 20);
            drawTextCentered("Loading streams for " + epLabel + "...", SCREEN_W/2, SCREEN_H/2 + 20, TEXT_PRIMARY);
            drawTextCentered("Press [B] or tap outside to cancel", SCREEN_W/2, SCREEN_H/2 + 60, TEXT_SECONDARY, m_fontSmall);
            return;
        }
        
        if (localStreams.empty()) {
            drawTextCentered("No playable streams found for " + epLabel + ".", SCREEN_W/2, SCREEN_H/2, TEXT_SECONDARY);
            return;
        }
        
        drawText("Available Streams for " + epLabel + ":", modalX + 30, modalY + 30, ACCENT, m_fontNormal);
        
        int streamsStartY = modalY + 80;
        int maxVisible = 9;
        int startIndex = 0;
        if (currentSelIndex >= maxVisible) {
            startIndex = currentSelIndex - maxVisible + 1;
        }

        int y = streamsStartY;
        if (startIndex > 0) {
            drawText("^ More streams above...", modalX + 30, y - 18, TEXT_SECONDARY, m_fontSmall);
        }

        for (int i = startIndex; i < (int)localStreams.size() && i < startIndex + maxVisible; i++) {
            bool sel = (i == currentSelIndex);
            if (sel) drawFilledRect(modalX + 25, y - 2, modalW - 50, 36, CARD_HL);

            auto& s = localStreams[i];
            std::string label = s.name.empty() ? "Stream " + std::to_string(i+1) : s.name;
            std::string titleClean;
            for (char c : s.title) {
                if (c == '\n' || c == '\r') titleClean += ' ';
                else titleClean += c;
            }
            if (titleClean.size() > 50) titleClean = titleClean.substr(0, 47) + "...";

            if (!titleClean.empty()) label += " — " + titleClean;
            if (label.size() > 70) label = label.substr(0, 67) + "...";

            drawText(label, modalX + 35, y + 4, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
            y += 40;
        }

        if (startIndex + maxVisible < (int)localStreams.size()) {
            drawText("v More streams below...", modalX + 30, y + 2, TEXT_SECONDARY, m_fontSmall);
        }
        
    } else {
        // NON-MODAL for movies!
        drawText("Available Streams:", infoX, streamsHeaderY, ACCENT, m_fontNormal);
        int streamsStartY = streamsHeaderY + 32;

        if (isLoading) {
            drawSpinner(infoX - 25, streamsStartY + 8, 10);
            drawText("Querying addons for stream links... Please wait...", infoX, streamsStartY, {150, 200, 255, 255}, m_fontSmall);
            return;
        }

        if (localStreams.empty()) {
            drawText("No playable streams found.", infoX, streamsStartY, TEXT_SECONDARY);
            return;
        }

        int remainingH = SCREEN_H - streamsStartY - 40;
        int maxVisible = remainingH / 40;
        if (maxVisible < 3) maxVisible = 3;

        int startIndex = 0;
        if (currentSelIndex >= maxVisible) {
            startIndex = currentSelIndex - maxVisible + 1;
        }

        int y = streamsStartY;
        if (startIndex > 0) {
            drawText("^ More streams above...", infoX, y - 18, TEXT_SECONDARY, m_fontSmall);
        }

        for (int i = startIndex; i < (int)localStreams.size() && i < startIndex + maxVisible; i++) {
            bool sel = (i == currentSelIndex);
            if (sel) drawFilledRect(infoX - 5, y - 2, SCREEN_W - infoX - 40, 36, CARD_HL);

            auto& s = localStreams[i];
            std::string label = s.name.empty() ? "Stream " + std::to_string(i+1) : s.name;
            std::string titleClean;
            for (char c : s.title) {
                if (c == '\n' || c == '\r') titleClean += ' ';
                else titleClean += c;
            }
            if (titleClean.size() > 70) titleClean = titleClean.substr(0, 67) + "...";

            if (!titleClean.empty()) label += " — " + titleClean;
            if (label.size() > 90) label = label.substr(0, 87) + "...";

            drawText(label, infoX + 5, y + 4, sel ? ACCENT : TEXT_PRIMARY, m_fontNormal);
            y += 40;
        }

        if (startIndex + maxVisible < (int)localStreams.size()) {
            drawText("v More streams below...", infoX, y + 2, TEXT_SECONDARY, m_fontSmall);
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
        {"Enable Torrent Streams", "Display and stream torrent/magnet links (requires remote TorrServer).", m_addonManager.getEnableTorrents() ? "ON (Enabled)" : "OFF (Disabled)"},
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

    // Draw Author Legend
    drawText("Author: Antigravity", SCREEN_W - 180, SCREEN_H - 30, {100, 100, 120, 255}, m_fontSmall);
}

void App::drawNavBar() {
    drawFilledRect(0, SCREEN_H - 40, SCREEN_W, 40, NAV_BG);
    drawText("[A] Select  [B] Back  [Y] Search  [+] Exit",
             40, SCREEN_H - 32, TEXT_SECONDARY, m_fontSmall);
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
    if (item.poster.empty()) {
        drawFilledRect(x, y, w, h, {40,40,55,255});
        return;
    }

    SDL_Texture* tex = m_imageCache->get(item.poster);
    if (tex) {
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        return;
    }

    // If it's already loading or failed, draw the placeholder and return
    bool isLoading = false;
    bool isFailed = false;
    {
        std::lock_guard<std::mutex> lock(m_downloadedMutex);
        isLoading = (m_loadingPosters.find(item.poster) != m_loadingPosters.end());
        isFailed = (m_failedPosters.find(item.poster) != m_failedPosters.end());
    }

    if (isFailed) {
        drawFilledRect(x, y, w, h, {40,40,55,255});
        return;
    }

    if (!isLoading) {
        {
            std::lock_guard<std::mutex> lock(m_downloadedMutex);
            m_loadingPosters.insert(item.poster);
        }

        // Add to download queue
        {
            std::lock_guard<std::mutex> lock(m_downloadQueueMutex);
            if (std::find(m_downloadQueue.begin(), m_downloadQueue.end(), item.poster) == m_downloadQueue.end()) {
                m_downloadQueue.push_back(item.poster);
                m_downloadQueueCV.notify_one();
            }
        }
    }

    // Draw loading/placeholder box
    drawFilledRect(x, y, w, h, {30,30,45,255});
}

void App::downloadWorkerLoop() {
    while (m_downloadWorkerRunning) {
        std::string url;
        {
            std::unique_lock<std::mutex> lock(m_downloadQueueMutex);
            m_downloadQueueCV.wait(lock, [this]() {
                return !m_downloadQueue.empty() || !m_downloadWorkerRunning;
            });
            if (!m_downloadWorkerRunning) break;
            url = m_downloadQueue.front();
            m_downloadQueue.erase(m_downloadQueue.begin());
        }

        // Fetch image bytes (blocking HTTP call in background worker thread)
        auto resp = m_http.downloadBytes(url);

        std::lock_guard<std::mutex> lock(m_downloadedMutex);
        m_loadingPosters.erase(url);
        if (resp.ok() && !resp.body.empty()) {
            DownloadedImage img;
            img.url = url;
            img.data = std::move(resp.body);
            m_downloadedQueue.push_back(std::move(img));
        } else {
            m_failedPosters.insert(url);
        }
    }
}

// ─── Data loading ────────────────────────────

void App::loadHomeCatalogs() {
    if (m_loadingHome) return;
    if (m_homeLoadingThread.joinable()) {
        m_homeLoadingThread.join();
    }
    m_loadingHome = true;
    m_homeLoadingThread = std::thread([this]() {
        auto catalogs = m_addonManager.getHomeCatalogs();
        {
            std::lock_guard<std::mutex> lock(m_homeMutex);
            m_homeCatalogs = std::move(catalogs);
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

void App::loadDetail(const std::string& type, const std::string& id) {
    m_loadingDetail = true;
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_detailStreams.clear();
        m_detailStreamIndex = 0;
        m_loadingStreams = true;
        m_detailEpisodeSelected = false;
        m_detailEpisodeIndex = 0;
        m_detailEpisodes.clear();
    }

    m_detailMeta = MetaItem(); // clear old

    if (m_detailLoadingThread.joinable()) {
        m_detailLoadingThread.join();
    }

    m_detailLoadingThread = std::thread([this, type, id]() {
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
            {
                std::lock_guard<std::mutex> lock(m_streamsMutex);
                m_detailEpisodes = std::move(sortedEps);
                m_detailMeta = std::move(loadedMeta);
                m_loadingStreams = false;
            }
            m_loadingDetail = false;
            return;
        }

        auto rawStreams = m_addonManager.getAllStreams(type, id);

        std::vector<Stream> loadedStreams;
        for (const auto& s : rawStreams) {
            bool isTorrentStream = !s.infoHash.empty() || s.url.rfind("magnet:", 0) == 0;

            if (isTorrentStream) {
                if (!m_addonManager.getEnableTorrents()) continue;
                if (!s.url.empty()) {
                    loadedStreams.push_back(s);
                } else if (!s.infoHash.empty()) {
                    Stream conv = s;
                    conv.url = "magnet:?xt=urn:btih:" + s.infoHash;
                    loadedStreams.push_back(conv);
                }
            } else {
                // Must have a playable URL — skip externalUrl-only entries
                // (e.g. PenguPlay donor notices that have no stream URL)
                if (!s.url.empty()) {
                    // Only accept http/https URLs
                    bool isHttp = s.url.rfind("http", 0) == 0;
                    if (!isHttp) {
                        printf("[loadDetail] Skipping non-http URL: %s\n", s.url.substr(0,60).c_str());
                        continue;
                    }
                    loadedStreams.push_back(s);
                } else if (!s.ytId.empty()) {
                    Stream conv = s;
                    conv.url = "https://www.youtube.com/watch?v=" + s.ytId;
                    loadedStreams.push_back(conv);
                } else {
                    printf("[loadDetail] Skipping stream with no usable URL (name=%s)\n",
                           s.name.substr(0, 40).c_str());
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_streamsMutex);
            m_detailMeta = std::move(loadedMeta);
            m_detailStreams = std::move(loadedStreams);
            m_loadingStreams = false;
        }
        m_loadingDetail = false;
    });
}

void App::startTorrentPolling() {
    {
        std::lock_guard<std::mutex> lock(m_torrentMutex);
        if (m_torrentPollingActive) return;
        m_torrentPollingActive = true;
        m_torrentStatString = "Connecting...";
        m_torrentPeers = 0;
        m_torrentSpeed = 0.0;
        m_torrentPreloadPercent = -1;
    }

    if (m_torrentPollingThread.joinable()) {
        m_torrentPollingThread.join();
    }

    m_torrentPollingThread = std::thread([this]() {
        printf("TorrServer stats polling thread started.\n");
        while (true) {
            std::string magnet;
            bool active = false;
            {
                std::lock_guard<std::mutex> lock(m_torrentMutex);
                magnet = m_lastPlayingMagnet;
                active = m_torrentPollingActive;
            }

            if (!active || m_screen != Screen::PLAYER || magnet.empty()) {
                break;
            }

            if (m_player.getPosition() > 0.01) {
                break;
            }

            std::string encoded;
            for (char c : magnet) {
                if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    encoded += c;
                } else {
                    char hex[4];
                    snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                    encoded += hex;
                }
            }

            std::string url = m_addonManager.getTorrServerHost() + "/stream?link=" + encoded + "&stat";
            auto resp = m_http.get(url);

            if (resp.ok() && !resp.body.empty()) {
                rapidjson::Document doc;
                doc.Parse(resp.body.c_str());
                if (!doc.HasParseError() && doc.IsObject()) {
                    std::string statStr = "";
                    int peers = 0;
                    double speed = 0.0;
                    int percent = -1;

                    if (doc.HasMember("stat_string") && doc["stat_string"].IsString()) {
                        statStr = doc["stat_string"].GetString();
                    }

                    if (doc.HasMember("total_peers") && doc["total_peers"].IsInt()) {
                        peers = doc["total_peers"].GetInt();
                    }

                    if (doc.HasMember("download_speed") && doc["download_speed"].IsNumber()) {
                        speed = doc["download_speed"].GetDouble();
                    }

                    if (doc.HasMember("stat") && doc["stat"].IsInt()) {
                        int stateVal = doc["stat"].GetInt();
                        if (stateVal == 2) { // TorrentPreload
                            int64_t preloadSize = 0;
                            int64_t preloadedBytes = 0;
                            if (doc.HasMember("preload_size") && doc["preload_size"].IsInt64()) {
                                preloadSize = doc["preload_size"].GetInt64();
                            }
                            if (doc.HasMember("preloaded_bytes") && doc["preloaded_bytes"].IsInt64()) {
                                preloadedBytes = doc["preloaded_bytes"].GetInt64();
                            }
                            if (preloadSize > 0) {
                                percent = (int)((preloadedBytes * 100) / preloadSize);
                            }
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(m_torrentMutex);
                        m_torrentStatString = statStr;
                        m_torrentPeers = peers;
                        m_torrentSpeed = speed;
                        m_torrentPreloadPercent = percent;
                    }
                }
            } else {
                std::lock_guard<std::mutex> lock(m_torrentMutex);
                m_torrentStatString = "TorrServer offline / unreachable";
                m_torrentPeers = 0;
                m_torrentSpeed = 0.0;
                m_torrentPreloadPercent = -1;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        {
            std::lock_guard<std::mutex> lock(m_torrentMutex);
            m_torrentPollingActive = false;
        }
        printf("TorrServer stats polling thread finished.\n");
    });
}

void App::playStream(const Stream& stream) {
    if (stream.url.empty()) {
        printf("[playStream] ERROR: stream URL is empty, name=%s\n", stream.name.c_str());
        return;
    }
    // Guard against non-playable URLs that somehow slipped through
    bool isPlayable = stream.url.rfind("http", 0) == 0 ||
                      stream.url.rfind("magnet:", 0) == 0;
    if (!isPlayable) {
        printf("[playStream] ERROR: unplayable URL scheme: %s\n", stream.url.substr(0,60).c_str());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_torrentMutex);
        m_lastPlayingMagnet = "";
    }

    std::string playUrl = stream.url;
    if (playUrl.rfind("magnet:", 0) == 0) {
        {
            std::lock_guard<std::mutex> lock(m_torrentMutex);
            m_lastPlayingMagnet = playUrl;
        }
        std::string encoded;
        for (char c : playUrl) {
            if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
                encoded += hex;
            }
        }
        playUrl = m_addonManager.getTorrServerHost() + "/stream?link=" + encoded;
        if (stream.fileIdx != -1) {
            playUrl += "&index=" + std::to_string(stream.fileIdx);
        } else {
            playUrl += "&index=0";
        }
        playUrl += "&play";
        printf("Routing torrent via TorrServer (%s): %s\n", m_addonManager.getTorrServerHost().c_str(), playUrl.c_str());

        startTorrentPolling();
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

    // 1. Terminate and join all background threads first
    m_downloadWorkerRunning = false;
    m_downloadQueueCV.notify_all();
    if (m_downloadWorkerThread.joinable()) {
        m_downloadWorkerThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_torrentMutex);
        m_torrentPollingActive = false;
    }
    if (m_torrentPollingThread.joinable()) {
        m_torrentPollingThread.join();
    }
    if (m_homeLoadingThread.joinable()) {
        m_homeLoadingThread.join();
    }
    if (m_searchThread.joinable()) {
        m_searchThread.join();
    }
    if (m_detailLoadingThread.joinable()) {
        m_detailLoadingThread.join();
    }
    if (m_installThread.joinable()) {
        m_installThread.join();
    }

    // 2. Save configurations
    m_library.save(LIB_FILE);
    m_addonManager.saveConfig(CONFIG_FILE);

    // 3. Clean up input controllers
    if (m_gameController) {
        SDL_GameControllerClose(m_gameController);
        m_gameController = nullptr;
    }

    // 4. Delete cache and close SDL resources
    m_player.shutdown();
    delete m_imageCache;
    m_imageCache = nullptr;

    if (m_fontLarge) { TTF_CloseFont(m_fontLarge); m_fontLarge = nullptr; }
    if (m_fontNormal) { TTF_CloseFont(m_fontNormal); m_fontNormal = nullptr; }
    if (m_fontSmall) { TTF_CloseFont(m_fontSmall); m_fontSmall = nullptr; }
    
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    
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
        bool epsSelected = false;
        std::vector<Video> localEpisodes;
        std::vector<Stream> localStreams;
        {
            std::lock_guard<std::mutex> lock(m_streamsMutex);
            epsSelected = m_detailEpisodeSelected;
            localEpisodes = m_detailEpisodes;
            localStreams = m_detailStreams;
        }

        m_dragAccumY += dy;
        if (abs(m_dragAccumY) >= 15) {
            int diff = -m_dragAccumY / 15;
            
            if (!epsSelected && !localEpisodes.empty()) {
                int prev = m_detailEpisodeIndex;
                m_detailEpisodeIndex = std::clamp(m_detailEpisodeIndex + diff, 0, (int)localEpisodes.size() - 1);
                m_dragAccumY = 0;
                printf("[Touch] Drag DETAIL Episode Index: %d -> %d\n", prev, m_detailEpisodeIndex);
            } else if (!localStreams.empty()) {
                int prev = m_detailStreamIndex;
                m_detailStreamIndex = std::clamp(m_detailStreamIndex + diff, 0, (int)localStreams.size() - 1);
                m_dragAccumY = 0;
                printf("[Touch] Drag DETAIL Stream Index: %d -> %d\n", prev, m_detailStreamIndex);
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
                auto tracks = m_player.getSubtitleTracks();
                if (!tracks.empty()) {
                    m_subListIndex = std::clamp(m_subListIndex + diff, 0, (int)tracks.size() - 1);
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

        int startY = 70;
        int visibleRows = 2;
        int firstRow = m_homeRowIndex > 0 ? m_homeRowIndex - 1 : 0;
        int maxVisibleCols = (SCREEN_W - 80) / (POSTER_W + POSTER_GAP);

        for (int r = firstRow; r < (int)localCatalogs.size() && r < firstRow + visibleRows + 1; r++) {
            auto& row = localCatalogs[r];
            int rowY = startY + (r - firstRow) * ROW_HEIGHT;
            int cardY = rowY + 30;

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
                    loadHomeCatalogs();
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
