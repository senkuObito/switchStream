// ─────────────────────────────────────────────
// Player — mpv implementation
// ─────────────────────────────────────────────

#include "player.h"
#include "curl_stream.h"
#include <mpv/stream_cb.h>
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <stdexcept>
#include <cstdio>
#include <cstring>

static void* get_proc_address(void* ctx, const char* name) {
    return SDL_GL_GetProcAddress(name);
}

namespace ss {

Player::Player() {}

Player::~Player() {
    shutdown();
}

static int stream_open_cb(void* user_data, char* uri, mpv_stream_cb_info* info) {
    Player* player = static_cast<Player*>(user_data);
    // Use the active stream headers (Referer, UA etc.) for the main URL.
    // Sub-requests (HLS segments, DASH chunks) carry no extra headers —
    // they use the fallback browser UA set via CURLOPT_USERAGENT.
    std::string headers = player->getActiveHeaders();
    CurlStream* stream = new CurlStream(uri, headers);

    info->cookie = stream;
    info->read_fn = [](void* cookie, char* buf, uint64_t nbytes) -> int64_t {
        return static_cast<CurlStream*>(cookie)->read(buf, nbytes);
    };
    info->seek_fn = [](void* cookie, int64_t offset) -> int64_t {
        return static_cast<CurlStream*>(cookie)->seek(offset);
    };
    info->size_fn = [](void* cookie) -> int64_t {
        return static_cast<CurlStream*>(cookie)->getSize();
    };
    info->close_fn = [](void* cookie) {
        delete static_cast<CurlStream*>(cookie);
    };
    return 0;
}

bool Player::init(SDL_Window* window, SDL_Renderer* renderer, bool hwDecode) {
    m_mpv = mpv_create();
    if (!m_mpv) return false;

    // Use libmpv render API instead of creating a standard VO window
    mpv_set_option_string(m_mpv, "vo", "libmpv");

    // Basic options
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "ytdl", "no"); // support YouTube URLs
    mpv_set_option_string(m_mpv, "tls-verify", "no");
#ifdef __SWITCH__
    mpv_set_option_string(m_mpv, "demuxer-lavf-o", "tls_verify=0,verify=0");
#endif
    mpv_set_option_string(m_mpv, "terminal", "yes");
    mpv_set_option_string(m_mpv, "msg-level", "all=v");

    if (hwDecode) {
        mpv_set_option_string(m_mpv, "hwdec", "auto");
    } else {
        mpv_set_option_string(m_mpv, "hwdec", "no");
    }

#ifdef __SWITCH__
    int val = 0;
#else
    int val = 1;
#endif
    mpv_set_option(m_mpv, "osc", MPV_FORMAT_FLAG, &val); // handle on-screen controls cross-platform

#ifdef __SWITCH__
    // On Switch, mpv's libavformat is compiled without native https support.
    // Register CurlStream for https so all https:// URLs (including HLS/DASH
    // sub-requests for segments) can be fetched via libcurl.
    int cb_err = mpv_stream_cb_add_ro(m_mpv, "https", this, stream_open_cb);
    if (cb_err < 0) {
        printf("[Player] Failed to register https callback: %s\n", mpv_error_string(cb_err));
    }
#endif

    // Set a browser User-Agent for all mpv HTTP requests (helps CDN access)
    mpv_set_option_string(m_mpv, "user-agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");

    if (mpv_initialize(m_mpv) < 0) {
        shutdown();
        return false;
    }

    // Initialize OpenGL render context
    mpv_opengl_init_params gl_init_params = {
        .get_proc_address = get_proc_address,
        .get_proc_address_ctx = nullptr
    };

    int advanced_control = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_OPENGL},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced_control},
        {(mpv_render_param_type)0, nullptr}
    };

    int err = mpv_render_context_create(&m_mpvGL, m_mpv, params);
    if (err < 0) {
        printf("[Player] Failed to create mpv render context: %s\n", mpv_error_string(err));
        shutdown();
        return false;
    }
    printf("[Player] Successfully created mpv render context!\n");

    return true;
}

#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_VIEWPORT 0x0BA2
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_CULL_FACE 0x0B44
#define GL_BLEND_SRC_RGB 0x80C9
#define GL_BLEND_DST_RGB 0x80C8
#define GL_BLEND_SRC_ALPHA 0x80CB
#define GL_BLEND_DST_ALPHA 0x80CA
#define GL_BLEND_EQUATION_RGB 0x8009
#define GL_BLEND_EQUATION_ALPHA 0x883D
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_FRAMEBUFFER 0x8D40
#define GL_TEXTURE_2D 0x0DE1

typedef void (*glGetIntegervProc)(unsigned int, int*);
typedef unsigned char (*glIsEnabledProc)(unsigned int);
typedef void (*glEnableProc)(unsigned int);
typedef void (*glDisableProc)(unsigned int);
typedef void (*glUseProgramProc)(unsigned int);
typedef void (*glActiveTextureProc)(unsigned int);
typedef void (*glBindTextureProc)(unsigned int, unsigned int);
typedef void (*glBindVertexArrayProc)(unsigned int);
typedef void (*glBindBufferProc)(unsigned int, unsigned int);
typedef void (*glBindFramebufferProc)(unsigned int, unsigned int);
typedef void (*glViewportProc)(int, int, int, int);
typedef void (*glBlendFuncSeparateProc)(unsigned int, unsigned int, unsigned int, unsigned int);
typedef void (*glBlendEquationSeparateProc)(unsigned int, unsigned int);

void Player::render(int w, int h) {
    if (!m_mpvGL) return;

    // Call update to process any pending frame updates/decoding
    mpv_render_context_update(m_mpvGL);

    mpv_opengl_fbo fbo = {
        .fbo = 0, // default framebuffer
        .w = w,
        .h = h,
        .internal_format = 0
    };

    int flip_y = 1;
    mpv_render_param draw_params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {(mpv_render_param_type)0, nullptr}
    };

    // 1. Resolve GL function pointers
    auto glGetIntegervFn = (glGetIntegervProc)SDL_GL_GetProcAddress("glGetIntegerv");
    auto glIsEnabledFn = (glIsEnabledProc)SDL_GL_GetProcAddress("glIsEnabled");
    auto glEnableFn = (glEnableProc)SDL_GL_GetProcAddress("glEnable");
    auto glDisableFn = (glDisableProc)SDL_GL_GetProcAddress("glDisable");
    auto glUseProgramFn = (glUseProgramProc)SDL_GL_GetProcAddress("glUseProgram");
    auto glActiveTextureFn = (glActiveTextureProc)SDL_GL_GetProcAddress("glActiveTexture");
    auto glBindTextureFn = (glBindTextureProc)SDL_GL_GetProcAddress("glBindTexture");
    auto glBindVertexArrayFn = (glBindVertexArrayProc)SDL_GL_GetProcAddress("glBindVertexArray");
    auto glBindBufferFn = (glBindBufferProc)SDL_GL_GetProcAddress("glBindBuffer");
    auto glBindFramebufferFn = (glBindFramebufferProc)SDL_GL_GetProcAddress("glBindFramebuffer");
    auto glViewportFn = (glViewportProc)SDL_GL_GetProcAddress("glViewport");
    auto glBlendFuncSeparateFn = (glBlendFuncSeparateProc)SDL_GL_GetProcAddress("glBlendFuncSeparate");
    auto glBlendEquationSeparateFn = (glBlendEquationSeparateProc)SDL_GL_GetProcAddress("glBlendEquationSeparate");

    // 2. Query/Save all states before MPV renders
    int prev_program = 0;
    int prev_active_texture = 0;
    int prev_texture_2d = 0;
    int prev_array_buffer = 0;
    int prev_element_array_buffer = 0;
    int prev_vao = 0;
    int prev_framebuffer = 0;
    int prev_viewport[4] = {0};
    unsigned char prev_blend = 0;
    unsigned char prev_scissor = 0;
    unsigned char prev_depth = 0;
    unsigned char prev_stencil = 0;
    unsigned char prev_cull = 0;
    int prev_blend_src_rgb = 0;
    int prev_blend_dst_rgb = 0;
    int prev_blend_src_alpha = 0;
    int prev_blend_dst_alpha = 0;
    int prev_blend_eq_rgb = 0;
    int prev_blend_eq_alpha = 0;

    if (glGetIntegervFn) {
        glGetIntegervFn(GL_CURRENT_PROGRAM, &prev_program);
        glGetIntegervFn(GL_ACTIVE_TEXTURE, &prev_active_texture);
        glGetIntegervFn(GL_TEXTURE_BINDING_2D, &prev_texture_2d);
        glGetIntegervFn(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
        glGetIntegervFn(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_element_array_buffer);
        if (glBindVertexArrayFn) {
            glGetIntegervFn(GL_VERTEX_ARRAY_BINDING, &prev_vao);
        }
        glGetIntegervFn(GL_FRAMEBUFFER_BINDING, &prev_framebuffer);
        glGetIntegervFn(GL_VIEWPORT, prev_viewport);
        
        glGetIntegervFn(GL_BLEND_SRC_RGB, &prev_blend_src_rgb);
        glGetIntegervFn(GL_BLEND_DST_RGB, &prev_blend_dst_rgb);
        glGetIntegervFn(GL_BLEND_SRC_ALPHA, &prev_blend_src_alpha);
        glGetIntegervFn(GL_BLEND_DST_ALPHA, &prev_blend_dst_alpha);
        glGetIntegervFn(GL_BLEND_EQUATION_RGB, &prev_blend_eq_rgb);
        glGetIntegervFn(GL_BLEND_EQUATION_ALPHA, &prev_blend_eq_alpha);
    }
    if (glIsEnabledFn) {
        prev_blend = glIsEnabledFn(GL_BLEND);
        prev_scissor = glIsEnabledFn(GL_SCISSOR_TEST);
        prev_depth = glIsEnabledFn(GL_DEPTH_TEST);
        prev_stencil = glIsEnabledFn(GL_STENCIL_TEST);
        prev_cull = glIsEnabledFn(GL_CULL_FACE);
    }

    // 3. Render the MPV video frame
    mpv_render_context_render(m_mpvGL, draw_params);

    // 4. Restore OpenGL context state exactly to what SDL left it as
    if (glUseProgramFn) glUseProgramFn(prev_program);
    if (glActiveTextureFn) glActiveTextureFn(prev_active_texture);
    if (glBindTextureFn) glBindTextureFn(GL_TEXTURE_2D, prev_texture_2d);
    if (glBindBufferFn) {
        glBindBufferFn(GL_ARRAY_BUFFER, prev_array_buffer);
        glBindBufferFn(GL_ELEMENT_ARRAY_BUFFER, prev_element_array_buffer);
    }
    if (glBindVertexArrayFn) glBindVertexArrayFn(prev_vao);
    if (glBindFramebufferFn) glBindFramebufferFn(GL_FRAMEBUFFER, prev_framebuffer);
    if (glViewportFn) glViewportFn(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    
    if (glBlendFuncSeparateFn) {
        glBlendFuncSeparateFn(prev_blend_src_rgb, prev_blend_dst_rgb, prev_blend_src_alpha, prev_blend_dst_alpha);
    }
    if (glBlendEquationSeparateFn) {
        glBlendEquationSeparateFn(prev_blend_eq_rgb, prev_blend_eq_alpha);
    }

    if (glEnableFn && glDisableFn) {
        if (prev_blend) glEnableFn(GL_BLEND); else glDisableFn(GL_BLEND);
        if (prev_scissor) glEnableFn(GL_SCISSOR_TEST); else glDisableFn(GL_SCISSOR_TEST);
        if (prev_depth) glEnableFn(GL_DEPTH_TEST); else glDisableFn(GL_DEPTH_TEST);
        if (prev_stencil) glEnableFn(GL_STENCIL_TEST); else glDisableFn(GL_STENCIL_TEST);
        if (prev_cull) glEnableFn(GL_CULL_FACE); else glDisableFn(GL_CULL_FACE);
    }
}

void Player::setHwDec(bool hwDecode) {
    if (!m_mpv) return;
    if (hwDecode) {
        mpv_set_option_string(m_mpv, "hwdec", "auto");
    } else {
        mpv_set_option_string(m_mpv, "hwdec", "no");
    }
}

void Player::play(const std::string& url, const std::string& headers) {
    if (!m_mpv) return;

    m_activeHeaders = headers;

    // mpv's http-header-fields expects comma-separated "Key: Value" pairs.
    // Our addon_client produces newline-separated headers — convert here.
    if (!headers.empty()) {
        std::string mpvHeaders;
        size_t pos = 0;
        while (pos < headers.size()) {
            size_t next = headers.find('\n', pos);
            std::string h = headers.substr(pos, next - pos);
            // strip trailing \r
            if (!h.empty() && h.back() == '\r') h.pop_back();
            if (!h.empty()) {
                if (!mpvHeaders.empty()) mpvHeaders += ",";
                mpvHeaders += h;
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        if (!mpvHeaders.empty()) {
            mpv_set_option_string(m_mpv, "http-header-fields", mpvHeaders.c_str());
            printf("[Player] http-header-fields: %s\n", mpvHeaders.c_str());
        }
    } else {
        mpv_set_option_string(m_mpv, "http-header-fields", "");
    }

    // Restore audio in case a previous stop() muted it
    int unmuteVal = 0;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &unmuteVal);
    double fullVol = 100.0;
    mpv_set_property(m_mpv, "ao-volume", MPV_FORMAT_DOUBLE, &fullVol);

    // Explicitly unpause on the mpv handle when starting a new stream
    int pauseVal = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pauseVal);

    const char* cmd[] = {"loadfile", url.c_str(), nullptr};
    mpv_command(m_mpv, cmd);

    m_paused = false;
    m_finished = false;
}

void Player::pause() {
    if (!m_mpv) return;
    int val = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &val);
    m_paused = true;
}

void Player::resume() {
    if (!m_mpv) return;
    int val = 0;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &val);
    m_paused = false;
}

void Player::togglePlay() {
    if (m_paused) resume();
    else pause();
}

void Player::stop() {
    if (!m_mpv) return;
    // Immediately mute to prevent audio bleed while mpv drains its buffer
    int muteVal = 1;
    mpv_set_property(m_mpv, "mute", MPV_FORMAT_FLAG, &muteVal);
    double zeroVol = 0.0;
    mpv_set_property(m_mpv, "ao-volume", MPV_FORMAT_DOUBLE, &zeroVol);
    // Pause then stop (stop is async — mute ensures silence during drain)
    int pauseVal = 1;
    mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &pauseVal);
    const char* cmd[] = {"stop", nullptr};
    mpv_command(m_mpv, cmd);
    m_paused = true;
    m_finished = true;
}

void Player::seek(double offsetSeconds) {
    if (!m_mpv) return;
    std::string pos = std::to_string(offsetSeconds);
    const char* cmd[] = {"seek", pos.c_str(), "relative", nullptr};
    mpv_command(m_mpv, cmd);
}

void Player::seekAbsolute(double positionSeconds) {
    if (!m_mpv) return;
    std::string pos = std::to_string(positionSeconds);
    const char* cmd[] = {"seek", pos.c_str(), "absolute", nullptr};
    mpv_command(m_mpv, cmd);
}

void Player::changeVolume(double delta) {
    if (!m_mpv) return;
    double vol = 0.0;
    mpv_get_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);
    vol += delta;
    if (vol < 0.0) vol = 0.0;
    if (vol > 100.0) vol = 100.0;
    mpv_set_property(m_mpv, "volume", MPV_FORMAT_DOUBLE, &vol);

    std::string volStr = "Volume: " + std::to_string((int)vol) + "%";
    const char* cmd[] = {"show-text", volStr.c_str(), nullptr};
    mpv_command(m_mpv, cmd);
}

void Player::cycleSubtitles() {
    if (!m_mpv) return;
    const char* cmd[] = {"cycle", "sub", nullptr};
    mpv_command(m_mpv, cmd);
}

void Player::cycleAudio() {
    if (!m_mpv) return;
    const char* cmd[] = {"cycle", "audio", nullptr};
    mpv_command(m_mpv, cmd);
}

bool Player::isPaused() const {
    return m_paused;
}

double Player::getPosition() const {
    if (!m_mpv) return 0.0;
    double pos = 0.0;
    mpv_get_property(m_mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    return pos;
}

double Player::getDuration() const {
    if (!m_mpv) return 0.0;
    double duration = 0.0;
    mpv_get_property(m_mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
    return duration;
}

bool Player::isFinished() const {
    return m_finished;
}

bool Player::isBuffering() const {
    if (!m_mpv) return false;
    int val = 0;
    if (mpv_get_property(m_mpv, "paused-for-cache", MPV_FORMAT_FLAG, &val) >= 0) {
        return (val != 0);
    }
    return false;
}

double Player::getBufferingPercentage() const {
    if (!m_mpv) return 0.0;
    double val = 0.0;
    mpv_get_property(m_mpv, "cache-buffering-state", MPV_FORMAT_DOUBLE, &val);
    return val;
}

void Player::update() {
    if (!m_mpv) return;

    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0); // non-blocking poll
        if (event->event_id == MPV_EVENT_NONE) break;

        printf("[Player] Event: %s (%d)\n", mpv_event_name(event->event_id), event->event_id);

        switch (event->event_id) {
            case MPV_EVENT_END_FILE: {
                mpv_event_end_file* end = (mpv_event_end_file*)event->data;
                printf("[Player] End file reason: %d, error code: %d\n", end->reason, end->error);
                                if (end->reason == MPV_END_FILE_REASON_EOF ||
                    end->reason == MPV_END_FILE_REASON_ERROR ||
                    end->reason == MPV_END_FILE_REASON_STOP) {
                    m_finished = true;
                }
                break;
            }
            default:
                break;
        }
    }
}

void Player::shutdown() {
    if (m_mpvGL) {
        mpv_render_context_free(m_mpvGL);
        m_mpvGL = nullptr;
    }
    if (m_mpv) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

std::vector<Player::SubtitleTrack> Player::getSubtitleTracks() {
    std::vector<SubtitleTrack> tracks;
    if (!m_mpv) return tracks;

    char* current_sid = nullptr;
    bool noneSelected = true;
    if (mpv_get_property(m_mpv, "sid", MPV_FORMAT_STRING, &current_sid) >= 0 && current_sid) {
        if (std::string(current_sid) != "no") {
            noneSelected = false;
        }
        mpv_free(current_sid);
    }

    SubtitleTrack noneTrack;
    noneTrack.id = -1;
    noneTrack.name = "None (Disable Subtitles)";
    noneTrack.selected = noneSelected;
    tracks.push_back(noneTrack);

    int64_t count = 0;
    if (mpv_get_property(m_mpv, "track-list/count", MPV_FORMAT_INT64, &count) < 0) {
        return tracks;
    }

    for (int i = 0; i < count; i++) {
        std::string prefix = "track-list/" + std::to_string(i) + "/";
        char* type = nullptr;
        if (mpv_get_property(m_mpv, (prefix + "type").c_str(), MPV_FORMAT_STRING, &type) >= 0 && type) {
            if (std::string(type) == "sub") {
                SubtitleTrack t;
                
                int64_t id = 0;
                mpv_get_property(m_mpv, (prefix + "id").c_str(), MPV_FORMAT_INT64, &id);
                t.id = (int)id;

                char* title = nullptr;
                char* lang = nullptr;
                mpv_get_property(m_mpv, (prefix + "title").c_str(), MPV_FORMAT_STRING, &title);
                mpv_get_property(m_mpv, (prefix + "lang").c_str(), MPV_FORMAT_STRING, &lang);

                std::string name = "";
                if (title && strlen(title) > 0) name += title;
                if (lang && strlen(lang) > 0) {
                    if (!name.empty()) name += " [";
                    name += lang;
                    if (!name.empty()) name += "]";
                }
                if (name.empty()) name = "Subtitle Track " + std::to_string(t.id);
                t.name = name;

                int8_t selected = 0;
                mpv_get_property(m_mpv, (prefix + "selected").c_str(), MPV_FORMAT_FLAG, &selected);
                t.selected = (selected != 0);

                tracks.push_back(t);

                if (title) mpv_free(title);
                if (lang) mpv_free(lang);
            }
            mpv_free(type);
        }
    }
    return tracks;
}

void Player::setSubtitleTrack(int id) {
    if (!m_mpv) return;
    if (id <= 0) {
        mpv_set_property_string(m_mpv, "sid", "no");
    } else {
        int64_t sid = id;
        mpv_set_property(m_mpv, "sid", MPV_FORMAT_INT64, &sid);
    }
}

std::vector<Player::AudioTrack> Player::getAudioTracks() {
    std::vector<AudioTrack> tracks;
    if (!m_mpv) return tracks;

    int64_t count = 0;
    if (mpv_get_property(m_mpv, "track-list/count", MPV_FORMAT_INT64, &count) < 0) {
        return tracks;
    }

    for (int i = 0; i < count; i++) {
        std::string prefix = "track-list/" + std::to_string(i) + "/";
        char* type = nullptr;
        if (mpv_get_property(m_mpv, (prefix + "type").c_str(), MPV_FORMAT_STRING, &type) >= 0 && type) {
            if (std::string(type) == "audio") {
                AudioTrack t;
                
                int64_t id = 0;
                mpv_get_property(m_mpv, (prefix + "id").c_str(), MPV_FORMAT_INT64, &id);
                t.id = (int)id;

                char* title = nullptr;
                char* lang = nullptr;
                mpv_get_property(m_mpv, (prefix + "title").c_str(), MPV_FORMAT_STRING, &title);
                mpv_get_property(m_mpv, (prefix + "lang").c_str(), MPV_FORMAT_STRING, &lang);

                std::string name = "";
                if (title && strlen(title) > 0) name += title;
                if (lang && strlen(lang) > 0) {
                    if (!name.empty()) name += " [";
                    name += lang;
                    if (!name.empty()) name += "]";
                }
                if (name.empty()) name = "Audio Track " + std::to_string(t.id);
                t.name = name;

                int8_t selected = 0;
                mpv_get_property(m_mpv, (prefix + "selected").c_str(), MPV_FORMAT_FLAG, &selected);
                t.selected = (selected != 0);

                tracks.push_back(t);

                if (title) mpv_free(title);
                if (lang) mpv_free(lang);
            }
            mpv_free(type);
        }
    }
    return tracks;
}

void Player::setAudioTrack(int id) {
    if (!m_mpv) return;
    int64_t aid = id;
    mpv_set_property(m_mpv, "aid", MPV_FORMAT_INT64, &aid);
}

} // namespace ss
