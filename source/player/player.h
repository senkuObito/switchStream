#pragma once

// ─────────────────────────────────────────────
// Player — mpv video player wrapper
// Works on both Linux and Nintendo Switch
// ─────────────────────────────────────────────

#include <string>
#include <vector>
#include <atomic>
#include <mpv/client.h>

struct SDL_Window;
struct SDL_Renderer;
struct mpv_render_context;

namespace ss {

class Player {
public:
    Player();
    ~Player();

    // Initialize mpv instance.
    // On Linux we pass SDL window handle to let mpv render into it.
    bool init(SDL_Window* window, SDL_Renderer* renderer, bool hwDecode = true);

    // Update hardware decoding setting on-the-fly
    void setHwDec(bool hwDecode);

    // Play a media URL with optional custom HTTP headers (comma-separated "Key: Value" strings)
    void play(const std::string& url, const std::string& headers = "");

    // Pause/Resume/Stop
    void pause();
    void resume();
    void togglePlay();
    void stop();

    // Seek relative (seconds)
    void seek(double offsetSeconds);
    void seekAbsolute(double positionSeconds);

    // Audio & Subtitle & Volume Controls
    void changeVolume(double delta);
    void cycleSubtitles();
    void cycleAudio();

    struct SubtitleTrack {
        int id;
        std::string name;
        bool selected;
    };
    std::vector<SubtitleTrack> getSubtitleTracks();
    void setSubtitleTrack(int id);
    bool addSubtitle(const std::string& pathOrUrl, const std::string& title = "");

    struct AudioTrack {
        int id;
        std::string name;
        bool selected;
    };
    std::vector<AudioTrack> getAudioTracks();
    void setAudioTrack(int id);

    // Get playback status (all non-blocking, asynchronous observed values)
    bool isPaused() const;
    double getPosition() const;
    double getDuration() const;
    double getCacheDuration() const;
    bool isCacheIdle() const;
    bool isFinished() const;
    bool isBuffering() const;
    double getBufferingPercentage() const;
    bool isFileLoaded() const { return m_fileLoaded.load(); }

    std::string getActiveHeaders() const { return m_activeHeaders; }

    // Render current video frame to OpenGL default framebuffer
    void render(int w, int h);

    // Handle internal mpv events (call this in the main loop)
    void update();

    // Access underlying mpv_handle
    mpv_handle* getMpv() const { return m_mpv; }

    // Destroy mpv handle
    void shutdown();

private:
    mpv_handle* m_mpv = nullptr;
    mpv_render_context* m_mpvGL = nullptr;
    bool m_paused = false;
    bool m_finished = false;
    std::string m_activeHeaders;

    // Asynchronously observed properties (updated in update() via MPV_EVENT_PROPERTY_CHANGE)
    std::atomic<bool> m_fileLoaded{false};
    std::atomic<double> m_cachedPos{0.0};
    std::atomic<double> m_cachedDuration{0.0};
    std::atomic<double> m_cachedCacheSecs{0.0};
    std::atomic<bool> m_cachedCacheIdle{false};
    std::atomic<bool> m_cachedBuffering{false};
    std::atomic<bool> m_cachedPaused{false};
};

} // namespace ss
