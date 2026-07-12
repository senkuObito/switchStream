#pragma once

// ─────────────────────────────────────────────
// Library — Watch history & bookmarks
// Persists to SD card as lightweight JSON
// ─────────────────────────────────────────────

#include "types.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace ss {

class Library {
public:
    // Load from SD card
    bool load(const std::string& path);

    // Save to SD card
    bool save(const std::string& path) const;

    // Update watch progress for an item
    void updateProgress(const std::string& id, const std::string& type,
                        const std::string& name, const std::string& poster,
                        const std::string& videoId, double progress);

    // Toggle bookmark
    void toggleBookmark(const std::string& id, const std::string& type,
                        const std::string& name, const std::string& poster);

    // Get item (nullptr if not in library)
    const LibraryItem* getItem(const std::string& id) const;

    // Get recently watched (sorted by lastWatched desc)
    std::vector<LibraryItem> getRecentlyWatched(int limit = 20) const;

    // Get bookmarked items
    std::vector<LibraryItem> getBookmarked() const;

    // Get "continue watching" items (progress > 0 and < 0.95)
    std::vector<LibraryItem> getContinueWatching(int limit = 10) const;

private:
    std::unordered_map<std::string, LibraryItem> m_items;
};

} // namespace ss
