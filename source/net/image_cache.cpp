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

    return it->second.first.texture.get();
}

SDL_Texture* ImageCache::store(const std::string& url, const void* data, size_t dataSize) {
    // If already cached, return existing
    if (has(url)) return get(url);

    // Load image from memory
    SDL_RWops* rw = SDL_RWFromConstMem(data, static_cast<int>(dataSize));
    if (!rw) return nullptr;

    SDL_Surface* surface = IMG_Load_RW(rw, 1); // 1 = auto-free RW
    if (!surface) return nullptr;

    SDL_Surface* targetSurface = surface;
    SDL_Surface* downscaled = nullptr;
    // On Switch 720p/1080p, cards are 150x225 and detail poster is 270x405.
    // Downscale oversized images (> 300x450) to save massive amounts of RAM/VRAM.
    if (surface->w > 300 || surface->h > 450) {
        int targetW = 300;
        int targetH = (surface->h * targetW) / surface->w;
        if (targetH < 1) targetH = 1;
        downscaled = SDL_CreateRGBSurface(0, targetW, targetH, 32,
                                          0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        if (downscaled) {
            SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);
            SDL_BlitScaled(surface, nullptr, downscaled, nullptr);
            targetSurface = downscaled;
        }
    }

    SDL_Texture* rawTex = SDL_CreateTextureFromSurface(m_renderer, targetSurface);
    int w = targetSurface->w;
    int h = targetSurface->h;
    if (downscaled) SDL_FreeSurface(downscaled);
    SDL_FreeSurface(surface);

    if (!rawTex) return nullptr;

    std::shared_ptr<SDL_Texture> texture(rawTex, [](SDL_Texture* t) {
        if (t) SDL_DestroyTexture(t);
    });

    // Estimate memory: width * height * 4 bytes (RGBA)
    size_t estimatedBytes = static_cast<size_t>(w) * h * 4;

    // Evict old entries if needed
    evictIfNeeded(estimatedBytes);

    // Insert into cache
    m_lruOrder.push_front(url);
    CacheEntry entry = { texture, w, h, estimatedBytes };
    m_cache[url] = { entry, m_lruOrder.begin() };
    m_currentBytes += estimatedBytes;

    return texture.get();
}

void ImageCache::addAlias(const std::string& aliasKey, const std::string& targetKey) {
    if (aliasKey.empty() || aliasKey == targetKey) return;
    auto it = m_cache.find(targetKey);
    if (it == m_cache.end()) return;

    auto existing = m_cache.find(aliasKey);
    if (existing != m_cache.end()) {
        m_lruOrder.erase(existing->second.second);
        m_cache.erase(existing);
    }

    m_lruOrder.push_front(aliasKey);
    CacheEntry entry = it->second.first;
    entry.estimatedBytes = 0; // Already accounted for in targetKey
    m_cache[aliasKey] = { entry, m_lruOrder.begin() };
}

bool ImageCache::has(const std::string& url) const {
    return m_cache.find(url) != m_cache.end();
}

void ImageCache::clear() {
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
            m_currentBytes -= it->second.first.estimatedBytes;
            m_cache.erase(it);
        }
        m_lruOrder.pop_back();
    }
}

} // namespace ss
