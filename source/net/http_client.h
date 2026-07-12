#pragma once

// ─────────────────────────────────────────────
// HTTP Client — Thin libcurl wrapper
// ─────────────────────────────────────────────

#include <string>
#include <functional>
#include <cstdint>

namespace ss {

// Simple HTTP response
struct HttpResponse {
    int statusCode = 0;
    std::string body;
    bool ok() const { return statusCode >= 200 && statusCode < 300; }
};

// Lightweight HTTP client using libcurl
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Synchronous GET — returns response body
    HttpResponse get(const std::string& url);

    // Download binary data (for images) into a buffer
    HttpResponse downloadBytes(const std::string& url);

    // Set timeout in seconds (default: 10s)
    void setTimeout(int seconds);

private:
    int m_timeout = 10;
};

} // namespace ss
