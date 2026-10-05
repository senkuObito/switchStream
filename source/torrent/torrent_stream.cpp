// ─────────────────────────────────────────────
// Torrent Stream Manager for SwitchStream
// Implementation
// ─────────────────────────────────────────────

#include "torrent_stream.h"
#include <cstdio>
#include <chrono>
#include <mpv/stream_cb.h>

extern "C" {
#include "torrentfs.h"
#include "torrent.h"
}

namespace ss {

struct StreamCtx {
    int64_t pos = 0;
};

static int64_t torrent_read_cb(void *cookie, char *buf, uint64_t nbytes) {
    auto* ctx = static_cast<StreamCtx*>(cookie);
    int64_t n = TorrentStream::instance().read(ctx->pos, buf, (int64_t)nbytes);
    if (n > 0) ctx->pos += n;
    return n;
}

static int64_t torrent_seek_cb(void *cookie, int64_t offset) {
    auto* ctx = static_cast<StreamCtx*>(cookie);
    ctx->pos = offset;
    TorrentStream::instance().setPlayhead(offset);
    return offset;
}

static int64_t torrent_size_cb(void *cookie) {
    (void)cookie;
    int64_t sz = TorrentStream::instance().getSize();
    return sz > 0 ? sz : MPV_ERROR_UNSUPPORTED;
}

static void torrent_close_cb(void *cookie) {
    TorrentStream::instance().onStreamClosed();
    delete static_cast<StreamCtx*>(cookie);
}

static int torrent_open_cb(void *user_data, char *uri, mpv_stream_cb_info *info) {
    (void)user_data;
    (void)uri;
    if (!TorrentStream::instance().isActive()) {
        printf("[TorrentStream] open_cb failed: no active torrent session\n");
        return MPV_ERROR_LOADING_FAILED;
    }

    auto* ctx = new StreamCtx();
    ctx->pos = 0;

    info->cookie = ctx;
    info->read_fn = torrent_read_cb;
    info->seek_fn = torrent_seek_cb;
    info->size_fn = torrent_size_cb;
    info->close_fn = torrent_close_cb;
    TorrentStream::instance().onStreamOpened();
    printf("[TorrentStream] open_cb initialized successfully for mpv\n");
    return 0;
}

TorrentStream& TorrentStream::instance() {
    static TorrentStream s_inst;
    return s_inst;
}

TorrentStream::TorrentStream() {}

TorrentStream::~TorrentStream() {
    stop();
}

void TorrentStream::onStreamOpened() {
    m_openStreams.fetch_add(1);
    printf("[TorrentStream] mpv stream opened (active open streams: %d)\n", m_openStreams.load());
}

void TorrentStream::onStreamClosed() {
    m_openStreams.fetch_sub(1);
    printf("[TorrentStream] mpv stream closed (active open streams: %d)\n", m_openStreams.load());
}

void TorrentStream::registerWithMpv(mpv_handle* mpv) {
    if (!mpv) return;
    int err = mpv_stream_cb_add_ro(mpv, "torrent", this, torrent_open_cb);
    if (err < 0) {
        printf("[TorrentStream] mpv_stream_cb_add_ro failed: %s\n", mpv_error_string(err));
    } else {
        printf("[TorrentStream] Registered 'torrent://' protocol with mpv\n");
    }
}

void TorrentStream::startAsync(const std::string& magnetOrTorrent, int fileIndex,
                               std::function<void(bool success, const std::string& err)> onReady) {
    stop();

    torrentfs_reset_cancel_open();
    m_opening.store(true);
    m_cancelRequested.store(false);
    m_lastError.clear();
    uint32_t sessionGen = ++m_sessionGen;

    m_openThread = std::thread([this, sessionGen, magnetOrTorrent, fileIndex, onReady]() {
        printf("[TorrentStream] Background open worker starting for: %s (fileIndex: %d)...\n",
               magnetOrTorrent.substr(0, 60).c_str(), fileIndex);

        // Enable in-memory RAM streaming mode
        torrentfs_set_ram_stream(1);

        char errBuf[256] = {0};
        torrentfs* t = torrentfs_open_file(magnetOrTorrent.c_str(),
                                           "/switch/switchstream/cache",
                                           fileIndex,
                                           errBuf,
                                           sizeof(errBuf));

        if (m_cancelRequested.load() || m_sessionGen.load() != sessionGen || torrentfs_is_open_cancelled()) {
            printf("[TorrentStream] Open cancelled by user or session expired\n");
            if (t) {
                torrentfs_cancel(t);
                torrentfs_close(t);
            }
            m_opening.store(false);
            if (onReady) onReady(false, "Cancelled by user");
            return;
        }

        if (!t) {
            m_lastError = errBuf;
            m_opening.store(false);
            printf("[TorrentStream] torrentfs_open_file failed: %s\n", errBuf);
            if (onReady) onReady(false, errBuf);
            return;
        }

        m_tfs.store(t);
        m_active.store(true);
        m_opening.store(false);
        m_lastBytesRecv = 0;
        m_lastSampleTime = 0;
        m_currentSpeedMBps = 0.0;

        printf("[TorrentStream] Torrent opened successfully! Name: %s, Size: %.1f MB\n",
               torrentfs_name(t), torrentfs_size(t) / (1024.0 * 1024.0));

        if (onReady) onReady(true, "");
    });
}

void TorrentStream::cancel() {
    m_cancelRequested.store(true);
    m_sessionGen.fetch_add(1);
    torrentfs_cancel_open();
    torrentfs* t = m_tfs.load();
    if (t) {
        printf("[TorrentStream] Cancelling active torrent reads...\n");
        torrentfs_cancel(t);
    }
}

void TorrentStream::stop() {
    cancel();

    if (m_openThread.joinable()) {
        m_openThread.join();
    }

    // Wait briefly (up to 300ms) for mpv demuxer to exit its read loop and close the stream
    for (int i = 0; i < 30 && m_openStreams.load() > 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Safely close torrent engine after mpv has detached
    torrentfs* t = m_tfs.exchange(nullptr);
    if (t) {
        printf("[TorrentStream] Closing torrent engine...\n");
        torrentfs_close(t);
        printf("[TorrentStream] Torrent engine closed cleanly.\n");
    }

    m_active.store(false);
    m_opening.store(false);
    m_cancelRequested.store(false);
}

bool TorrentStream::isActive() const {
    return m_active.load() && (m_tfs.load() != nullptr);
}

bool TorrentStream::isOpening() const {
    return m_opening.load();
}

int64_t TorrentStream::read(int64_t pos, char* buf, int64_t nbytes) {
    if (m_cancelRequested.load()) return -1;
    torrentfs* t = m_tfs.load();
    if (!t) return -1;
    return torrentfs_read(t, pos, buf, nbytes);
}

void TorrentStream::setPlayhead(int64_t pos) {
    torrentfs* t = m_tfs.load();
    if (t) {
        torrentfs_set_playhead(t, pos);
    }
}

void TorrentStream::setBacklog(int ms) {
    torrentfs* t = m_tfs.load();
    if (t) {
        torrentfs_set_backlog(t, ms);
    }
}

int64_t TorrentStream::getSize() const {
    torrentfs* t = m_tfs.load();
    if (!t) return -1;
    return torrentfs_size(t);
}

TorrentStats TorrentStream::getStats() {
    TorrentStats stats;

    if (m_opening.load()) {
        const char* st = torrent_meta_state_str(torrent_meta_state);
        stats.statusStr = st ? st : "Connecting to trackers...";
        stats.connectingPeers = torrent_meta_trackers;
        stats.livePeers = torrent_meta_connected;

        char buf[128];
        snprintf(buf, sizeof(buf), "%s (trackers: %d, peers: %d)",
                 stats.statusStr.c_str(), stats.connectingPeers, stats.livePeers);
        stats.statusStr = buf;
        return stats;
    }

    torrentfs* t = m_tfs.load();
    if (!t) {
        stats.statusStr = m_lastError.empty() ? "Inactive" : ("Error: " + m_lastError);
        return stats;
    }

    torrentfs_stats(t, &stats.piecesDone, &stats.piecesTotal, &stats.playheadPiece);
    torrentfs_live_peers(t, &stats.livePeers, &stats.peakPeers, &stats.connectingPeers);
    stats.totalBytes = torrentfs_size(t);
    stats.bytesRecv = torrentfs_bytes_recv(t);
    const char* nm = torrentfs_name(t);
    stats.name = nm ? nm : "Resolving metadata...";
    stats.metadataReady = (stats.totalBytes > 0);

    // Calculate download speed
    auto now = std::chrono::steady_clock::now();
    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (m_lastSampleTime > 0 && nowMs > m_lastSampleTime) {
        double elapsedSec = (nowMs - m_lastSampleTime) / 1000.0;
        if (elapsedSec >= 0.5) {
            int64_t diff = stats.bytesRecv - m_lastBytesRecv;
            if (diff >= 0) {
                m_currentSpeedMBps = (diff / (1024.0 * 1024.0)) / elapsedSec;
            }
            m_lastBytesRecv = stats.bytesRecv;
            m_lastSampleTime = nowMs;
        }
    } else {
        m_lastSampleTime = nowMs;
        m_lastBytesRecv = stats.bytesRecv;
    }
    stats.speedMBps = m_currentSpeedMBps;

    char buf[128];
    if (!stats.metadataReady) {
        snprintf(buf, sizeof(buf), "Connecting to peers (%d peers)...", stats.livePeers);
    } else {
        snprintf(buf, sizeof(buf), "Peers: %d | Speed: %.2f MB/s | %.1f%% buffered",
                 stats.livePeers, stats.speedMBps,
                 stats.piecesTotal > 0 ? (stats.piecesDone * 100.0 / stats.piecesTotal) : 0.0);
    }
    stats.statusStr = buf;

    return stats;
}

} // namespace ss
