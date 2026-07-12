#pragma once
#ifdef __SWITCH__
#include <switch.h>
#endif
#include "types.h"
#include "core/addon_client.h"
#include "core/addon_manager.h"
#include "core/library.h"
#include "net/http_client.h"
#include "net/image_cache.h"
#include "player/player.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <string>
#include <vector>
#include <mutex>
#include <set>
#include <thread>
#include <condition_variable>

namespace ss {
struct CatalogRow;

class App {
public:
    App();
    ~App();
    bool init();
    void run();
    void shutdown();

private:
    void render();
    void renderHome();
    void renderSearch();
    void renderDetail();
    void renderPlayer();
    void renderLibrary();
    void renderAddons();
    void renderSettings();

    void drawText(const std::string& text, int x, int y,
                  SDL_Color color = {255,255,255,255}, TTF_Font* font = nullptr);
    void drawTextCentered(const std::string& text, int cx, int y,
                          SDL_Color color = {255,255,255,255}, TTF_Font* font = nullptr);
    int drawTextWrapped(const std::string& text, int x, int y, int wrapWidth,
                        SDL_Color color = {255,255,255,255}, TTF_Font* font = nullptr);
    void drawRect(int x, int y, int w, int h, SDL_Color color);
    void drawFilledRect(int x, int y, int w, int h, SDL_Color color);
    void drawRoundRect(int x, int y, int w, int h, int r, SDL_Color color);
    void drawFilledRoundRect(int x, int y, int w, int h, int r, SDL_Color color);
    void drawSpinner(int cx, int cy, int radius);
    void drawPoster(const MetaItem& item, int x, int y, int w, int h);
    void drawNavBar();

    void handleInput();
    void openSwkbd(std::string& output, const std::string& header = "");

    void loadHomeCatalogs();
    void performSearch(const std::string& query);
    void sortSearchResults();
    void loadDetail(const std::string& type, const std::string& id);
    void playStream(const Stream& stream);
    void startTorrentPolling();

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    SDL_GameController* m_gameController = nullptr;
    TTF_Font* m_fontNormal = nullptr;
    TTF_Font* m_fontSmall = nullptr;
    TTF_Font* m_fontLarge = nullptr;

    HttpClient m_http;
    AddonClient m_addonClient;
    AddonManager m_addonManager;
    Library m_library;
    ImageCache* m_imageCache = nullptr;
    Player m_player;

    void handleInputForPad(u64 kDown);
    void handleTouch(int x, int y);
    void handleDrag(int dx, int dy);
    void handleLongPress(int x, int y);

    Screen m_screen = Screen::HOME;
    bool m_running = true;

    std::vector<CatalogRow> m_homeCatalogs;
    int m_homeRowIndex = 0;
    int m_homeColIndex = 0;

    std::string m_searchQuery;
    enum class SearchSort { YEAR_DESC, DEFAULT };
    SearchSort m_searchSort = SearchSort::YEAR_DESC;
    std::vector<MetaItem> m_searchResults;
    int m_searchIndex = 0;

    MetaItem m_detailMeta;
    std::vector<Stream> m_detailStreams;
    int m_detailStreamIndex = 0;

    int m_libraryIndex = 0;
    int m_addonIndex = 0;
    bool m_addonDiscoverPane = false;
    int m_addonDiscoverIndex = 0;
    int m_settingsIndex = 0;
    bool m_loading = false;
    bool m_loadingStreams = false;
    std::mutex m_streamsMutex;
    bool m_loadingHome = false;
    std::mutex m_homeMutex;

    // TorrServer torrent stats polling
    std::string m_lastPlayingMagnet;
    std::string m_torrentStatString;
    int m_torrentPeers = 0;
    double m_torrentSpeed = 0.0;
    int m_torrentPreloadPercent = -1;
    bool m_torrentPollingActive = false;
    std::mutex m_torrentMutex;
    std::thread m_torrentPollingThread;
    std::thread m_homeLoadingThread;
    bool m_loadingSearch = false;
    std::mutex m_searchMutex;
    std::thread m_searchThread;
    bool m_loadingDetail = false;
    std::thread m_detailLoadingThread;
    std::thread m_installThread;
    uint32_t m_osdShowTime = 0;

    bool m_mouseDown = false;
    int m_startMouseX = 0;
    int m_startMouseY = 0;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;
    uint32_t m_mouseStartTime = 0;
    bool m_mouseDragging = false;
    bool m_mouseLongPressed = false;
    int m_dragAccumX = 0;
    int m_dragAccumY = 0;

    // Double tap state
    uint32_t m_lastTapTime = 0;
    int m_lastTapX = 0;
    int m_lastTapY = 0;

    // Scrubbing state
    bool m_isScrubbing = false;
    double m_scrubStartPos = 0.0;
    double m_scrubCurrentPos = 0.0;

    // Subtitle & Audio track overlay lists state
    bool m_showSubList = false;
    bool m_showAudioList = false;
    int m_subListIndex = 0;
    int m_audioListIndex = 0;

#ifdef __SWITCH__
    static constexpr const char* DATA_DIR    = "sdmc:/switch/switchstream/";
    static constexpr const char* CONFIG_FILE = "sdmc:/switch/switchstream/addons.json";
    static constexpr const char* LIB_FILE    = "sdmc:/switch/switchstream/library.json";
#else
    static constexpr const char* DATA_DIR    = "./switchstream_data/";
    static constexpr const char* CONFIG_FILE = "./switchstream_data/addons.json";
    static constexpr const char* LIB_FILE    = "./switchstream_data/library.json";
#endif

    struct DownloadedImage {
        std::string url;
        std::string data;
    };
    std::vector<DownloadedImage> m_downloadedQueue;
    std::mutex m_downloadedMutex;
    std::set<std::string> m_loadingPosters;
    std::set<std::string> m_failedPosters;

    std::mutex m_downloadQueueMutex;
    std::vector<std::string> m_downloadQueue;
    std::thread m_downloadWorkerThread;
    bool m_downloadWorkerRunning = false;
    std::condition_variable m_downloadQueueCV;
    void downloadWorkerLoop();
};
} // namespace ss
