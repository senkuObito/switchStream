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

HttpClient::HttpClient() {
    // Global init is called once in main.cpp
}

HttpClient::~HttpClient() {}

HttpResponse HttpClient::get(const std::string& url) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (!curl) {
        response.statusCode = -1;
        return response;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, m_timeout);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SwitchStream/1.0");
    // SSL: use system CA bundle on Switch (disabled on Switch since it doesn't exist by default)
#ifdef __SWITCH__
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#else
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#endif
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long httpCode;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        response.statusCode = static_cast<int>(httpCode);
    } else {
        response.statusCode = -1;
    }

    curl_easy_cleanup(curl);
    return response;
}

HttpResponse HttpClient::downloadBytes(const std::string& url) {
    // Same as get — body contains raw bytes
    return get(url);
}

void HttpClient::setTimeout(int seconds) {
    m_timeout = seconds;
}

} // namespace ss
