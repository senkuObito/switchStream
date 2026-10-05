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
#include <atomic>
#include <stdexcept>
#include <exception>
#include <condition_variable>
#include <chrono>

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
    void detailWorkerLoop();
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
    void maskRoundedCorners(int x, int y, int w, int h, int r, SDL_Color bgColor);
    void drawSpinner(int cx, int cy, int radius);
    void drawPoster(const MetaItem& item, int x, int y, int w, int h);
    void drawNavBar();

    void handleInput();
    void openSwkbd(std::string& output, const std::string& header = "");

    void loadHomeCatalogs(bool force = false);
    void cleanCatalogImageCache();
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
    HttpClient m_imageHttp;
    AddonClient m_addonClient;
    AddonManager m_addonManager;
    Library m_library;
    ImageCache* m_imageCache = nullptr;
    Player m_player;

    void handleInputForPad(u64 kDown, u64 kHeld = 0, u64 kUp = 0);
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
    
    enum class DetailFocus { EPISODES, STREAMS };
    DetailFocus m_detailFocus = DetailFocus::STREAMS;

    bool m_detailEpisodeSelected = false;
    int m_detailEpisodeIndex = 0;
    std::vector<Video> m_detailEpisodes;       // ALL episodes (all seasons)
    std::vector<int>   m_detailSeasons;        // sorted unique season numbers
    int m_detailSeasonFilter = 0;              // index into m_detailSeasons (0 = first season)
    void loadEpisodeStreams(const std::string& epId);


    int m_libraryIndex = 0;
    int m_addonIndex = 0;
    bool m_addonDiscoverPane = false;
    int m_addonDiscoverIndex = 0;
    int m_settingsIndex = 0;
    std::atomic<bool> m_loading{false};
    std::atomic<bool> m_loadingStreams{false};
    std::mutex m_streamsMutex;
    std::atomic<bool> m_loadingHome{false};
    std::mutex m_homeMutex;

    // Torrent streaming state
    std::string m_lastPlayingMagnet;
    std::string m_torrentStatString;
    int m_torrentPeers = 0;
    double m_torrentSpeed = 0.0;
    int m_torrentPreloadPercent = -1;
    bool m_torrentBuffering = false;
    bool m_torrentPollingActive = false;
    bool m_pendingTorrentPlay = false;
    std::string m_pendingTorrentHeaders;
    bool m_torrentFailed = false;
    std::string m_torrentErrorMsg;
    std::mutex m_torrentMutex;
    std::thread m_torrentPollingThread;
    std::thread m_homeLoadingThread;
    std::atomic<bool> m_loadingSearch{false};
    std::mutex m_searchMutex;
    std::thread m_searchThread;
    std::atomic<bool> m_loadingDetail{false};
    std::atomic<int> m_detailGeneration{0};
    
    // Persistent worker thread to prevent thread exhaustion
    std::thread m_detailWorkerThread;
    std::mutex m_workerMutex;
    std::condition_variable m_workerCv;
    bool m_workerRunning = false;
    int m_workerTask = 0; // 0=none, 1=meta+streams, 2=episode_streams
    std::string m_workerType;
    std::string m_workerId;
    int m_workerGen = 0;
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

    // 2X Speed & Joycon long-press handling
    bool m_is2xSpeed = false;
    uint32_t m_rButtonDownTime = 0;
    bool m_rButtonLongPressed = false;

    // Subtitle & Audio track overlay lists state
    bool m_showSubList = false;
    bool m_showAudioList = false;
    int m_subListIndex = 0;
    int m_audioListIndex = 0;

    // Addon Subtitle search & selection state
    bool m_subAddonMode = false;
    std::atomic<bool> m_subAddonLoading{false};
    std::vector<Subtitle> m_addonSubtitles;
    int m_subAddonIndex = 0;
    std::mutex m_subMutex;
    std::thread m_subSearchThread;
    std::atomic<int> m_subSearchGen{0};
    std::string m_currentPlayingId;
    std::string m_currentPlayingType;
    std::string m_currentPlayingEpisodeId;
    bool m_wasPlayingBeforeSubSearch = true;
    void fetchAddonSubtitles();
    void applyAddonSubtitle(const Subtitle& sub);
    void cancelAddonSubtitle();

    // Quality (stream) switcher overlay state
    bool m_showQualityList = false;
    int m_qualityListIndex = 0;

    // Stream Addon Performance Warning Modal Popup state
    bool m_showStreamWarningPopup = false;
    int m_streamWarningIndex = 0; // 0 = Cancel, 1 = Don't Show Again

#ifdef __SWITCH__
    static constexpr const char* DATA_DIR    = "sdmc:/switch/switchstream/";
    static constexpr const char* CONFIG_FILE = "sdmc:/switch/switchstream/addons.json";
    static constexpr const char* LIB_FILE    = "sdmc:/switch/switchstream/library.json";
    static constexpr const char* CACHE_DIR   = "sdmc:/switch/switchstream/imgcache";
    static constexpr const char* HOME_CACHE  = "sdmc:/switch/switchstream/home_cache.json";
#else
    static constexpr const char* DATA_DIR    = "./switchstream_data/";
    static constexpr const char* CONFIG_FILE = "./switchstream_data/addons.json";
    static constexpr const char* LIB_FILE    = "./switchstream_data/library.json";
    static constexpr const char* CACHE_DIR   = "./switchstream_data/imgcache";
    static constexpr const char* HOME_CACHE  = "./switchstream_data/home_cache.json";
#endif

    struct DownloadJob {
        std::string url;
        std::string titleKey;
    };

    struct DownloadedImage {
        std::string url;
        std::string titleKey;
        std::string data;
    };
    std::vector<DownloadedImage> m_downloadedQueue;
    std::mutex m_downloadedMutex;
    std::set<std::string> m_loadingPosters;
    std::set<std::string> m_failedPosters;

    static constexpr int NUM_DOWNLOAD_WORKERS = 6;
    std::mutex m_downloadQueueMutex;
    std::vector<DownloadJob> m_downloadQueue;
    std::vector<std::thread> m_downloadWorkers;
    bool m_downloadWorkerRunning = false;
    std::condition_variable m_downloadQueueCV;
    void downloadWorkerLoop();

    static std::string normalizeTitleKey(const std::string& title, const std::string& type);
    std::string getDiskCachePath(const std::string& key);
    bool readDiskCache(const std::string& path, std::string& outData);
    void writeDiskCache(const std::string& path, const std::string& data);
    void prequeuePosters(const std::vector<CatalogRow>& rows, int maxRows = 3);
    bool saveHomeCache(const std::string& path, const std::vector<CatalogRow>& catalogs);
    bool loadHomeCache(const std::string& path, std::vector<CatalogRow>& outCatalogs);

    // Shutdown coordination — allows sleeping threads to wake immediately
    std::atomic<bool> m_shuttingDown{false};
    std::mutex m_shutdownMtx;
    std::condition_variable m_shutdownCv;
};
} // namespace ss
