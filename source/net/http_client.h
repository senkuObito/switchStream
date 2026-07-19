#pragma once

// ─────────────────────────────────────────────
// HTTP Client — Thin libcurl wrapper
// ─────────────────────────────────────────────

#include <string>
#include <functional>
#include <cstdint>
#include <atomic>

namespace ss {

// Simple HTTP response
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Lightweight HTTP client using libcurl
// All methods are thread-safe: each call creates its own CURL handle.
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Synchronous GET — returns response body.
    // timeout_s: per-request override (0 = use default 60s)
    HttpResponse get(const std::string& url, int timeout_s = 0);

    // Download binary data (for images) into a buffer
    HttpResponse downloadBytes(const std::string& url);

    // Signal all in-flight requests to abort immediately
    void cancel() { m_cancelled.store(true,  std::memory_order_relaxed); }

    // Reset cancel flag so the client can be used again
    void reset()  { m_cancelled.store(false, std::memory_order_relaxed); }

private:
    static constexpr int DEFAULT_TIMEOUT = 60; // seconds
    std::atomic<bool> m_cancelled{false};
};

} // namespace ss
