// ─────────────────────────────────────────────
// HTTP Client — libcurl implementation
// ─────────────────────────────────────────────

#include "http_client.h"
#include <curl/curl.h>
#include <cstring>

namespace ss {

// curl write callback — appends data to std::string
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// curl progress callback — returns non-zero to abort the transfer
static int xferinfoCallback(void* clientp,
                            curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                            curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    const std::atomic<bool>* cancelled = static_cast<const std::atomic<bool>*>(clientp);
    return cancelled->load(std::memory_order_relaxed) ? 1 : 0;
}

HttpClient::HttpClient() {}
HttpClient::~HttpClient() {}

HttpResponse HttpClient::get(const std::string& url, int timeout_s) {
    HttpResponse response;

    if (m_cancelled.load(std::memory_order_relaxed)) {
        response.statusCode = -1;
        return response;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.statusCode = -1;
        return response;
    }

    const int effective_timeout = (timeout_s > 0) ? timeout_s : DEFAULT_TIMEOUT;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)effective_timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
#ifdef __SWITCH__
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#else
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#endif
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    // Cancel callback — checks m_cancelled every ~1s, aborts if set
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfoCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &m_cancelled);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long httpCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
    } else if (res == CURLE_ABORTED_BY_CALLBACK) {
        fprintf(stderr, "[HttpClient] request aborted (shutdown)\n");
        response.statusCode = -1;
    } else {
        fprintf(stderr, "[HttpClient] curl_easy_perform failed: %s\n", curl_easy_strerror(res));
        response.statusCode = -1;
    }

    curl_easy_cleanup(curl);
    return response;
}

HttpResponse HttpClient::downloadBytes(const std::string& url) {
    return get(url);
}

} // namespace ss
