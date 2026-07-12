#pragma once

// ─────────────────────────────────────────────
// Player — mpv video player wrapper
// Works on both Linux and Nintendo Switch
// ─────────────────────────────────────────────

#include <string>
#include <vector>
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

    struct AudioTrack {
        int id;
        std::string name;
        bool selected;
    };
    std::vector<AudioTrack> getAudioTracks();
    void setAudioTrack(int id);

    // Get playback status
    bool isPaused() const;
    double getPosition() const;
    double getDuration() const;
    bool isFinished() const;
    bool isBuffering() const;
    double getBufferingPercentage() const;

    std::string getActiveHeaders() const { return m_activeHeaders; }

    // Render current video frame to OpenGL default framebuffer
    void render(int w, int h);

    // Handle internal mpv events (call this in the main loop)
    void update();

    // Destroy mpv handle
    void shutdown();

private:
    mpv_handle* m_mpv = nullptr;
    mpv_render_context* m_mpvGL = nullptr;
    bool m_paused = false;
    bool m_finished = false;
    std::string m_activeHeaders;
};

} // namespace ss
