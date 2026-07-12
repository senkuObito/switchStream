#pragma once

// ─────────────────────────────────────────────
// SwitchStream — Lightweight Stremio for Switch
// Core type definitions matching Stremio protocol
// ─────────────────────────────────────────────

#include <string>
#include <vector>
#include <cstdint>

#ifndef __SWITCH__
typedef uint64_t u64;
enum HidNpadButton {
    HidNpadButton_A      = 1 << 0,
    HidNpadButton_B      = 1 << 1,
    HidNpadButton_X      = 1 << 2,
    HidNpadButton_Y      = 1 << 3,
    HidNpadButton_L      = 1 << 6,
    HidNpadButton_R      = 1 << 7,
    HidNpadButton_Plus   = 1 << 10,
    HidNpadButton_Left   = 1 << 12,
    HidNpadButton_Up     = 1 << 13,
    HidNpadButton_Right  = 1 << 14,
    HidNpadButton_Down   = 1 << 15,
};
#endif

namespace ss {

// ─── Addon Manifest ──────────────────────────

struct ResourceDef {
    std::string name;           // "catalog", "meta", "stream", "subtitles"
    std::vector<std::string> types;       // ["movie", "series"]
    std::vector<std::string> idPrefixes;  // ["tt"] for IMDB
};

struct CatalogDef {
    std::string type;  // "movie", "series"
    std::string id;    // "top", "popular", etc.
    std::string name;  // Display name (optional)
    std::vector<std::string> extraSupported; // e.g. ["search", "genre", "skip"]
    bool hasRequiredExtras = false; // true if any extra has isRequired:true (without default)
};

struct AddonManifest {
    std::string id;
    std::string version;
    std::string name;
    std::string description;
    std::string logo;
    std::string background;
    std::vector<std::string> types;
    std::vector<CatalogDef> catalogs;
    std::vector<ResourceDef> resources;
    std::string transportUrl;  // base URL (we add this ourselves)
};

// ─── Meta Objects ────────────────────────────

struct Video {
    std::string id;
    std::string title;
    int season = 0;
    int episode = 0;
    std::string released;      // ISO date
    std::string thumbnail;
    std::string overview;
};

struct MetaItem {
    std::string id;
    std::string type;          // "movie", "series"
    std::string name;
    std::string poster;        // URL
    std::string background;    // URL
    std::string description;
    std::string releaseInfo;
    std::string runtime;
    std::string imdbRating;
    std::vector<std::string> genres;
    std::vector<std::string> cast;
    std::vector<Video> videos; // episodes for series
    std::string addonName;     // Source addon for search results
};

// ─── Stream Objects ──────────────────────────

struct Stream {
    std::string url;           // direct URL
    std::string ytId;          // YouTube video ID
    std::string infoHash;      // torrent info hash
    int fileIdx = -1;          // file index in torrent
    std::string externalUrl;   // open in external app
    std::string name;          // display name (e.g. "1080p")
    std::string title;         // secondary text
    // Behavioral hints
    bool notWebReady = false;
    std::string bingeGroup;
    std::string httpHeaderFields; // Comma-separated list of "Key: Value" headers for mpv
};

// ─── Subtitle Objects ────────────────────────

struct Subtitle {
    std::string url;
    std::string lang;          // ISO 639-1 code
    std::string id;
};

// ─── API Responses ───────────────────────────

struct CatalogResponse {
    std::vector<MetaItem> metas;
    bool hasMore = false;      // pagination hint
};

struct MetaResponse {
    MetaItem meta;
};

struct StreamResponse {
    std::vector<Stream> streams;
};

struct SubtitleResponse {
    std::vector<Subtitle> subtitles;
};

// ─── Library Item (local storage) ────────────

struct LibraryItem {
    std::string id;
    std::string type;
    std::string name;
    std::string poster;
    double progress = 0.0;     // 0.0 - 1.0 watch progress
    int64_t lastWatched = 0;   // unix timestamp
    std::string videoId;       // last watched video/episode
    bool bookmarked = false;
};

// ─── App Config ──────────────────────────────

struct InstalledAddon {
    std::string transportUrl;
    AddonManifest manifest;
    bool enabled = true;
};

struct AppConfig {
    std::vector<InstalledAddon> addons;
    int posterCacheMaxMB = 32;  // lightweight cache
    bool hwDecode = true;
    std::string subtitleLang = "en";
    int uiScale = 100;         // percentage
};

// ─── UI State ────────────────────────────────

enum class Screen {
    HOME,
    SEARCH,
    DETAIL,
    PLAYER,
    LIBRARY,
    ADDONS,
    SETTINGS
};

} // namespace ss
