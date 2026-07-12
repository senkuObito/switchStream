// ─────────────────────────────────────────────
// Image Cache — LRU implementation
// ─────────────────────────────────────────────

#include "image_cache.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

namespace ss {

ImageCache::ImageCache(SDL_Renderer* renderer, int maxCacheMB)
    : m_renderer(renderer)
    , m_maxBytes(static_cast<size_t>(maxCacheMB) * 1024 * 1024)
{}

ImageCache::~ImageCache() {
    clear();
}

SDL_Texture* ImageCache::get(const std::string& url) {
    auto it = m_cache.find(url);
    if (it == m_cache.end()) return nullptr;

    // Move to front of LRU
    m_lruOrder.erase(it->second.second);
    m_lruOrder.push_front(url);
    it->second.second = m_lruOrder.begin();

    return it->second.first.texture;
}

SDL_Texture* ImageCache::store(const std::string& url, const void* data, size_t dataSize) {
    // If already cached, return existing
    if (has(url)) return get(url);

    // Load image from memory
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(dataSize));
    if (!rw) return nullptr;

    SDL_Surface* surface = IMG_Load_RW(rw, 1); // 1 = auto-free RW
    if (!surface) return nullptr;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, surface);
    int w = surface->w;
    int h = surface->h;
    SDL_FreeSurface(surface);

    if (!texture) return nullptr;

    // Estimate memory: width * height * 4 bytes (RGBA)
    size_t estimatedBytes = static_cast<size_t>(w) * h * 4;

    // Evict old entries if needed
    evictIfNeeded(estimatedBytes);

    // Insert into cache
    m_lruOrder.push_front(url);
    CacheEntry entry = { texture, w, h, estimatedBytes };
    m_cache[url] = { entry, m_lruOrder.begin() };
    m_currentBytes += estimatedBytes;

    return texture;
}

bool ImageCache::has(const std::string& url) const {
    return m_cache.find(url) != m_cache.end();
}

void ImageCache::clear() {
    for (auto& pair : m_cache) {
        if (pair.second.first.texture) {
            SDL_DestroyTexture(pair.second.first.texture);
        }
    }
    m_cache.clear();
    m_lruOrder.clear();
    m_currentBytes = 0;
}

size_t ImageCache::memoryUsage() const {
    return m_currentBytes;
}

void ImageCache::evictIfNeeded(size_t newEntryBytes) {
    while (m_currentBytes + newEntryBytes > m_maxBytes && !m_lruOrder.empty()) {
        // Evict least recently used (back of list)
        const std::string& lruUrl = m_lruOrder.back();
        auto it = m_cache.find(lruUrl);
        if (it != m_cache.end()) {
            if (it->second.first.texture) {
                SDL_DestroyTexture(it->second.first.texture);
            }
            m_currentBytes -= it->second.first.estimatedBytes;
            m_cache.erase(it);
        }
        m_lruOrder.pop_back();
    }
}

} // namespace ss
