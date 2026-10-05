#pragma once

// ─────────────────────────────────────────────
// Torrent Stream Manager for SwitchStream
// Native on-device BitTorrent streaming engine
// ─────────────────────────────────────────────

#include <string>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <mpv/client.h>

extern "C" {
struct torrentfs;
}

namespace ss {

struct TorrentStats {
    std::string name;
    int64_t piecesDone = 0;
    int64_t piecesTotal = 0;
    int64_t playheadPiece = 0;
    int livePeers = 0;
    int peakPeers = 0;
    int connectingPeers = 0;
    double speedMBps = 0.0;
    int64_t totalBytes = 0;
    int64_t bytesRecv = 0;
    bool metadataReady = false;
    std::string statusStr;
};

class TorrentStream {
public:
    static TorrentStream& instance();

    TorrentStream();
    ~TorrentStream();

    // Register protocol "torrent" with mpv
    void registerWithMpv(mpv_handle* mpv);

    // Start a torrent stream asynchronously on a background thread so UI never freezes
    void startAsync(const std::string& magnetOrTorrent, int fileIndex,
                    std::function<void(bool success, const std::string& err)> onReady);

    // Cancel any active read or open operations (sets stop=true, non-blocking)
    void cancel();

    // Stop and cleanly shut down the active torrent engine
    void stop();

    // Check if engine is running
    bool isActive() const;
    bool isOpening() const;

    // Called when mpv opens or closes the stream callback
    void onStreamOpened();
    void onStreamClosed();

    // Stream callback helpers (called from mpv worker thread)
    int64_t read(int64_t pos, char* buf, int64_t nbytes);
    void setPlayhead(int64_t pos);
    void setBacklog(int ms);
    int64_t getSize() const;

    // Poll live statistics for UI / OSD
    TorrentStats getStats();

    torrentfs* getTfs() const { return m_tfs.load(); }
    std::string getLastError() const { return m_lastError; }

private:
    std::atomic<torrentfs*> m_tfs{nullptr};
    std::string m_lastError;
    std::atomic<bool> m_active{false};
    std::atomic<bool> m_opening{false};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<uint32_t> m_sessionGen{0};
    std::atomic<int> m_openStreams{0};
    std::thread m_openThread;

    // Speed calculation
    int64_t m_lastBytesRecv = 0;
    uint64_t m_lastSampleTime = 0;
    double m_currentSpeedMBps = 0.0;
};

} // namespace ss
