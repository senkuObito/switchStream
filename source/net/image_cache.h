#pragma once

// ─────────────────────────────────────────────
// Image Cache — LRU cache for poster thumbnails
// Keeps memory usage under control
// ─────────────────────────────────────────────

#include <string>
#include <unordered_map>
#include <list>
#include <cstdint>

struct SDL_Texture;
struct SDL_Renderer;

namespace ss {

class ImageCache {
public:
    ImageCache(SDL_Renderer* renderer, int maxCacheMB = 32);
    ~ImageCache();

    // Get texture for URL. Returns nullptr if not cached.
    // Caller must NOT free the texture.
    SDL_Texture* get(const std::string& url);

    // Store downloaded image data as texture
    // Returns the created texture (owned by cache)
    SDL_Texture* store(const std::string& url, const void* data, size_t dataSize);

    // Check if URL is in cache
    bool has(const std::string& url) const;

    // Clear entire cache
    void clear();

    // Current cache memory usage estimate (bytes)
    size_t memoryUsage() const;

private:
    struct CacheEntry {
        SDL_Texture* texture;
        int width;
        int height;
        size_t estimatedBytes;
    };

    void evictIfNeeded(size_t newEntryBytes);

    SDL_Renderer* m_renderer;
    size_t m_maxBytes;
    size_t m_currentBytes = 0;

    // LRU list: front = most recently used
    std::list<std::string> m_lruOrder;
    std::unordered_map<std::string, std::pair<CacheEntry, std::list<std::string>::iterator>> m_cache;
};

} // namespace ss
