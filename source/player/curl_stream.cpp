#include "curl_stream.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace ss {

static const size_t RING_BUFFER_SIZE = 2 * 1024 * 1024; // 2MB — optimal for Switch memory & multiple concurrent HLS streams

CurlStream::CurlStream(const std::string& url, const std::string& headers)
    : m_url(url), m_headers(headers), m_buffer(RING_BUFFER_SIZE) {
    
    // Start streaming from the beginning (offset 0)
    startThread(0);
}

CurlStream::~CurlStream() {
    stopThread();
}

void CurlStream::startThread(int64_t offset) {
    stopThread();

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_bufferHead = 0;
        m_bufferTail = 0;
        m_bufferCount = 0;
        m_eof = false;
        m_error = false;
        m_stop = false;
        m_position = offset;
    }

    m_thread = std::thread(&CurlStream::curlThreadFunc, this, offset);
}

void CurlStream::stopThread() {
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_stop = true;
        m_cv_write.notify_all();
        m_cv_read.notify_all();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void CurlStream::cancel() {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_stop = true;
    m_cv_write.notify_all();
    m_cv_read.notify_all();
}

int64_t CurlStream::getSize() const {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_totalSize > 0) {
        return m_totalSize;
    }
    return -18; // MPV_ERROR_UNSUPPORTED: size unknown
}

int CurlStream::xferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    CurlStream* self = static_cast<CurlStream*>(clientp);
    if (!self) return 0;
    std::unique_lock<std::mutex> lock(self->m_mutex);
    if (self->m_stop) {
        return 1; // Non-zero aborts transfer immediately
    }
    return 0;
}

size_t CurlStream::writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    CurlStream* self = static_cast<CurlStream*>(userdata);
    size_t totalBytes = size * nmemb;
    size_t bytesWritten = 0;

    while (bytesWritten < totalBytes) {
        std::unique_lock<std::mutex> lock(self->m_mutex);
        
        if (self->m_stop) {
            return 0; // abort transfer
        }

        // Wait while the buffer is full
        while (self->m_bufferCount >= RING_BUFFER_SIZE && !self->m_stop) {
            self->m_cv_write.wait(lock);
        }

        if (self->m_stop) {
            return 0;
        }

        size_t spaceAvailable = RING_BUFFER_SIZE - self->m_bufferCount;
        size_t toWrite = std::min(totalBytes - bytesWritten, spaceAvailable);

        // Write to circular buffer
        size_t firstPart = std::min(toWrite, RING_BUFFER_SIZE - self->m_bufferTail);
        std::memcpy(&self->m_buffer[self->m_bufferTail], ptr + bytesWritten, firstPart);
        self->m_bufferTail = (self->m_bufferTail + firstPart) % RING_BUFFER_SIZE;

        if (toWrite > firstPart) {
            size_t secondPart = toWrite - firstPart;
            std::memcpy(&self->m_buffer[self->m_bufferTail], ptr + bytesWritten + firstPart, secondPart);
            self->m_bufferTail = (self->m_bufferTail + secondPart) % RING_BUFFER_SIZE;
        }

        self->m_bufferCount += toWrite;
        bytesWritten += toWrite;

        self->m_cv_read.notify_all();
    }

    return totalBytes;
}

void CurlStream::curlThreadFunc(int64_t offset) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_error = true;
        m_cv_read.notify_all();
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, m_url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &CurlStream::writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

    // Multithreading & network safety on Nintendo Switch
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);

    // Immediate cancellation hook
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, &CurlStream::xferInfoCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    
    // Disable certificate validation internally to bypass any handshake failures
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // Only set a fallback UA if the stream headers don't already supply one.
    bool hasCustomUA = (m_headers.find("User-Agent:") != std::string::npos ||
                        m_headers.find("user-agent:") != std::string::npos);
    if (!hasCustomUA) {
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    }

    // Custom headers (Referer, auth tokens, etc. from stream behaviorHints.proxyHeaders)
    struct curl_slist* chunk = nullptr;
    if (!m_headers.empty()) {
        size_t pos = 0;
        while (pos < m_headers.length()) {
            size_t next = m_headers.find('\n', pos);
            std::string header = m_headers.substr(pos, next - pos);
            if (!header.empty() && header.back() == '\r') {
                header.pop_back();
            }
            if (!header.empty()) {
                chunk = curl_slist_append(chunk, header.c_str());
            }
            if (next == std::string::npos) break;
            pos = next + 1;
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
    }

    // Set Range request if offset > 0
    char rangeHeader[64];
    if (offset > 0) {
        std::sprintf(rangeHeader, "%lld-", (long long)offset);
        curl_easy_setopt(curl, CURLOPT_RANGE, rangeHeader);
    }

    CURLcode res = curl_easy_perform(curl);

    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);

    curl_off_t totalLen = -1;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &totalLen);

    {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
            printf("[CurlStream] curl_easy_perform failed: %s\n", curl_easy_strerror(res));
            m_error = true;
        } else if (httpCode >= 400) {
            printf("[CurlStream] HTTP error %ld for URL: %.80s...\n", httpCode, m_url.c_str());
            m_bufferCount = 0;
            m_bufferHead  = 0;
            m_bufferTail  = 0;
            m_error = true;
        } else {
            m_eof = true;
            if (m_totalSize == -1 && totalLen > 0) {
                m_totalSize = offset + (int64_t)totalLen;
            }
        }
        m_cv_read.notify_all();
    }

    if (chunk) {
        curl_slist_free_all(chunk);
    }
    curl_easy_cleanup(curl);
}

int64_t CurlStream::read(char* buf, uint64_t nbytes) {
    std::unique_lock<std::mutex> lock(m_mutex);

    while (m_bufferCount == 0 && !m_eof && !m_error && !m_stop) {
        m_cv_read.wait(lock);
    }

    if (m_stop) {
        return -1;
    }

    if (m_bufferCount == 0) {
        if (m_error) return -1;
        if (m_eof) return 0; // EOF
    }

    size_t toRead = std::min((size_t)nbytes, m_bufferCount);

    size_t firstPart = std::min(toRead, RING_BUFFER_SIZE - m_bufferHead);
    std::memcpy(buf, &m_buffer[m_bufferHead], firstPart);
    m_bufferHead = (m_bufferHead + firstPart) % RING_BUFFER_SIZE;

    if (toRead > firstPart) {
        size_t secondPart = toRead - firstPart;
        std::memcpy(buf + firstPart, &m_buffer[m_bufferHead], secondPart);
        m_bufferHead = (m_bufferHead + secondPart) % RING_BUFFER_SIZE;
    }

    m_bufferCount -= toRead;
    m_position += toRead;

    m_cv_write.notify_all();

    return toRead;
}

int64_t CurlStream::seek(int64_t offset) {
    if (offset < 0) {
        return -18; // MPV_ERROR_UNSUPPORTED
    }

    std::unique_lock<std::mutex> lock(m_mutex);

    // If seeking to current position, no-op (critical: prevents destroying thread on seek(0))
    if (offset == m_position) {
        return m_position;
    }

    // If seeking forward and the target offset is already buffered in memory:
    if (offset > m_position) {
        int64_t diff = offset - m_position;
        if ((size_t)diff <= m_bufferCount) {
            m_bufferHead = (m_bufferHead + (size_t)diff) % RING_BUFFER_SIZE;
            m_bufferCount -= (size_t)diff;
            m_position = offset;
            m_cv_write.notify_all();
            return m_position;
        }
    }

    lock.unlock();

    printf("[CurlStream] Seeking to offset: %lld (from %lld)\n", (long long)offset, (long long)m_position);
    startThread(offset);
    return offset;
}

} // namespace ss
