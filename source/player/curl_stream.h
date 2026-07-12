#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <curl/curl.h>

namespace ss {

class CurlStream {
public:
    CurlStream(const std::string& url, const std::string& headers);
    ~CurlStream();

    int64_t read(char* buf, uint64_t nbytes);
    int64_t seek(int64_t offset);
    int64_t getSize() const { return m_totalSize; }

private:
    void startThread(int64_t offset);
    void stopThread();
    static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata);
    void curlThreadFunc(int64_t offset);

    std::string m_url;
    std::string m_headers;

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv_read;
    std::condition_variable m_cv_write;

    std::vector<char> m_buffer;
    size_t m_bufferHead = 0;
    size_t m_bufferTail = 0;
    size_t m_bufferCount = 0;
    
    bool m_eof = false;
    bool m_error = false;
    bool m_stop = false;

    int64_t m_position = 0;
    int64_t m_totalSize = -1;
};

} // namespace ss
