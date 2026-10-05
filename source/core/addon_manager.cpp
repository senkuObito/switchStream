// ─────────────────────────────────────────────
// Addon Manager — Implementation
// ─────────────────────────────────────────────

#include "addon_manager.h"
#include "addon_client.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <fstream>
#include <algorithm>
#include <future>
#include <vector>

namespace ss {

using namespace rapidjson;

AddonManager::AddonManager(AddonClient& client) : m_client(client) {}

bool AddonManager::loadConfig(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    Document doc;
    doc.Parse(content.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        m_addons.clear();
    }

    if (doc.HasMember("torrserver_host") && doc["torrserver_host"].IsString()) {
        m_torrserverHost = doc["torrserver_host"].GetString();
    } else {
        m_torrserverHost = "http://127.0.0.1:8090";
    }

    if (doc.HasMember("hw_decode") && doc["hw_decode"].IsBool()) {
        m_hwDecode = doc["hw_decode"].GetBool();
    } else {
        m_hwDecode = true;
    }

    if (doc.HasMember("subtitle_lang") && doc["subtitle_lang"].IsString()) {
        m_subtitleLang = doc["subtitle_lang"].GetString();
    } else {
        m_subtitleLang = "en";
    }

    if (doc.HasMember("enable_torrents") && doc["enable_torrents"].IsBool()) {
        m_enableTorrents = doc["enable_torrents"].GetBool();
    } else {
        m_enableTorrents = false;
    }

    if (doc.HasMember("suppress_stream_warning") && doc["suppress_stream_warning"].IsBool()) {
        m_suppressStreamAddonWarning = doc["suppress_stream_warning"].GetBool();
    } else {
        m_suppressStreamAddonWarning = false;
    }

    if (doc.HasMember("addons") && doc["addons"].IsArray()) {
        for (auto& a : doc["addons"].GetArray()) {
            if (!a.IsObject() || !a.HasMember("url")) continue;
            std::string url = a["url"].GetString();
            bool enabled = true;
            if (a.HasMember("enabled") && a["enabled"].IsBool())
                enabled = a["enabled"].GetBool();

            InstalledAddon addon;
            addon.transportUrl = url;
            addon.enabled = enabled;
            if (a.HasMember("name") && a["name"].IsString()) {
                addon.manifest.name = a["name"].GetString();
            }
            if (a.HasMember("id") && a["id"].IsString()) {
                addon.manifest.id = a["id"].GetString();
            }
            if (a.HasMember("description") && a["description"].IsString()) {
                addon.manifest.description = a["description"].GetString();
            }
            if (a.HasMember("version") && a["version"].IsString()) {
                addon.manifest.version = a["version"].GetString();
            }

            // Fill empty fields from fallback resolver
            if (addon.manifest.name.empty()) {
                addon.manifest.name = getAddonFallbackName(url);
            }
            if (addon.manifest.description.empty()) {
                addon.manifest.description = getAddonFallbackDesc(url);
            }
            if (addon.manifest.id.empty()) {
                addon.manifest.id = getAddonFallbackId(url);
            }

            if (url.find("opensubtitles") != std::string::npos) {
                if (addon.manifest.id.empty()) addon.manifest.id = "org.stremio.opensubtitlesv3";
                if (addon.manifest.name.empty()) addon.manifest.name = "OpenSubtitles v3";
                if (addon.manifest.resources.empty()) {
                    ResourceDef r;
                    r.name = "subtitles";
                    r.types = {"movie", "series"};
                    r.idPrefixes = {"tt"};
                    addon.manifest.resources.push_back(std::move(r));
                }
                addon.manifest.types = {"movie", "series"};
                addon.manifest.idPrefixes = {"tt"};
            }

            // Deduplicate by manifest ID on load — prevents two list entries for
            // the same addon (e.g. Pengu plain URL + Pengu configured URL).
            // Manifests without an ID (not yet fetched) are always added.
            {
                std::lock_guard<std::mutex> lock(m_addonsMutex);
                bool dupId = false;
                if (!addon.manifest.id.empty()) {
                    for (auto& existing : m_addons) {
                        if (existing.manifest.id == addon.manifest.id) {
                            dupId = true;
                            break;
                        }
                    }
                }
                if (!dupId) {
                    m_addons.push_back(std::move(addon));
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        sortAddons();
    }

    return true;
}

bool AddonManager::saveConfig(const std::string& configPath) const {
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    StringBuffer sb;
    Writer<StringBuffer> writer(sb);

    writer.StartObject();
    writer.Key("torrserver_host");
    writer.String(m_torrserverHost.c_str());

    writer.Key("hw_decode");
    writer.Bool(m_hwDecode);

    writer.Key("subtitle_lang");
    writer.String(m_subtitleLang.c_str());

    writer.Key("enable_torrents");
    writer.Bool(m_enableTorrents);

    writer.Key("suppress_stream_warning");
    writer.Bool(m_suppressStreamAddonWarning);

    writer.Key("addons");
    writer.StartArray();
    for (auto& addon : m_addons) {
        writer.StartObject();
        writer.Key("url");
        writer.String(addon.transportUrl.c_str());
        writer.Key("enabled");
        writer.Bool(addon.enabled);
        writer.Key("id");
        std::string id = addon.manifest.id.empty() ? getAddonFallbackId(addon.transportUrl) : addon.manifest.id;
        writer.String(id.c_str());
        writer.Key("name");
        std::string name = addon.manifest.name.empty() ? getAddonFallbackName(addon.transportUrl) : addon.manifest.name;
        writer.String(name.c_str());
        writer.Key("description");
        std::string desc = addon.manifest.description.empty() ? getAddonFallbackDesc(addon.transportUrl) : addon.manifest.description;
        writer.String(desc.c_str());
        if (!addon.manifest.version.empty()) {
            writer.Key("version");
            writer.String(addon.manifest.version.c_str());
        }
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    std::ofstream file(configPath);
    if (!file.is_open()) return false;
    file << sb.GetString();
    file.close();
    return true;
}

std::vector<InstalledAddon> AddonManager::getAddons() const {
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    return m_addons;
}

int AddonManager::getEnabledStreamAddonCount() const {
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    int count = 0;
    for (const auto& a : m_addons) {
        if (!a.enabled) continue;

        bool hasStreamResource = false;
        for (const auto& r : a.manifest.resources) {
            if (r.name == "stream") {
                hasStreamResource = true;
                break;
            }
        }
        if (hasStreamResource) {
            count++;
            continue;
        }

        // If manifest resources are populated and none is "stream", it's definitely not a stream addon
        if (!a.manifest.resources.empty()) {
            continue;
        }

        // Fallback for addons whose manifest isn't fetched yet
        std::string lowerUrl = a.transportUrl;
        std::string lowerId = a.manifest.id;
        for (char& c : lowerUrl) c = (char)tolower((unsigned char)c);
        for (char& c : lowerId) c = (char)tolower((unsigned char)c);

        if (lowerUrl.find("opensubtitles") != std::string::npos || lowerId.find("opensubtitles") != std::string::npos) continue;
        if (lowerUrl.find("cinemeta") != std::string::npos || lowerId.find("cinemeta") != std::string::npos) continue;
        if (lowerUrl.find("catalog") != std::string::npos || lowerId.find("catalog") != std::string::npos) continue;

        if (lowerUrl.find("torrent") != std::string::npos || lowerUrl.find("stream") != std::string::npos ||
            lowerId.find("torrent") != std::string::npos || lowerId.find("stream") != std::string::npos) {
            count++;
        }
    }
    return count;
}

bool AddonManager::installAddon(const std::string& transportUrl) {
    // Reset warning suppression whenever addons are changed
    m_suppressStreamAddonWarning = false;

    // Check if already installed by URL
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        for (auto& a : m_addons) {
            if (a.transportUrl == transportUrl) return true;
        }
    }

    InstalledAddon addon;
    addon.transportUrl = transportUrl;
    addon.enabled = true;

    if (!m_client.fetchManifest(transportUrl, addon.manifest)) {
        // Even if manifest fetch fails (e.g. offline/timeout), populate fallbacks
        addon.manifest.name = getAddonFallbackName(transportUrl);
        addon.manifest.description = getAddonFallbackDesc(transportUrl);
        addon.manifest.id = getAddonFallbackId(transportUrl);
    } else {
        if (addon.manifest.name.empty()) addon.manifest.name = getAddonFallbackName(transportUrl);
        if (addon.manifest.description.empty()) addon.manifest.description = getAddonFallbackDesc(transportUrl);
        if (addon.manifest.id.empty()) addon.manifest.id = getAddonFallbackId(transportUrl);
    }

    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        // Deduplicate by manifest ID — same addon, different URL (e.g. Pengu)
        if (!addon.manifest.id.empty()) {
            for (auto& a : m_addons) {
                if (a.manifest.id == addon.manifest.id) {
                    a.transportUrl = transportUrl;
                    a.manifest     = addon.manifest;
                    sortAddons();
                    return true;
                }
            }
        }
        m_addons.push_back(std::move(addon));
        sortAddons();
    }
    return true;
}

void AddonManager::addAddon(const InstalledAddon& addon) {
    m_suppressStreamAddonWarning = false;
    InstalledAddon a = addon;
    if (a.manifest.name.empty()) a.manifest.name = getAddonFallbackName(a.transportUrl);
    if (a.manifest.description.empty()) a.manifest.description = getAddonFallbackDesc(a.transportUrl);
    if (a.manifest.id.empty()) a.manifest.id = getAddonFallbackId(a.transportUrl);

    std::lock_guard<std::mutex> lock(m_addonsMutex);
    for (const auto& existing : m_addons) {
        if (existing.transportUrl == a.transportUrl) return;
        if (!a.manifest.id.empty() && existing.manifest.id == a.manifest.id) return;
    }
    m_addons.push_back(std::move(a));
    sortAddons();
}

void AddonManager::removeAddon(const std::string& addonId) {
    m_suppressStreamAddonWarning = false;
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    m_addons.erase(
        std::remove_if(m_addons.begin(), m_addons.end(),
            [&](const InstalledAddon& a) { return a.manifest.id == addonId || a.transportUrl == addonId; }),
        m_addons.end()
    );
}

void AddonManager::toggleAddon(const std::string& addonId) {
    m_suppressStreamAddonWarning = false;
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    // Toggle ALL entries with this ID — handles any lingering duplicates
    for (auto& a : m_addons) {
        if (a.manifest.id == addonId || a.transportUrl == addonId) {
            a.enabled = !a.enabled;
        }
    }
    sortAddons();
}

void AddonManager::sortAddons() {
    std::stable_sort(m_addons.begin(), m_addons.end(), [](const InstalledAddon& a, const InstalledAddon& b) {
        if (a.enabled != b.enabled) {
            return a.enabled > b.enabled; // true (active) comes before false (disabled)
        }
        return false;
    });
}

void AddonManager::ensureManifest(InstalledAddon& addon) {
    if (addon.manifest.resources.empty() && addon.manifest.catalogs.empty()) {
        if (m_client.fetchManifest(addon.transportUrl, addon.manifest)) {
            std::lock_guard<std::mutex> lock(m_addonsMutex);
            for (auto& a : m_addons) {
                if (a.transportUrl == addon.transportUrl) {
                    a.manifest = addon.manifest;
                    break;
                }
            }
        }
    }
    if (addon.manifest.name.empty()) addon.manifest.name = getAddonFallbackName(addon.transportUrl);
    if (addon.manifest.description.empty()) addon.manifest.description = getAddonFallbackDesc(addon.transportUrl);
    if (addon.manifest.id.empty()) addon.manifest.id = getAddonFallbackId(addon.transportUrl);
}

bool AddonManager::addonHandles(const InstalledAddon& addon,
                                 const std::string& resource,
                                 const std::string& type,
                                 const std::string& id) const {
    if (!addon.enabled) return false;

    // Special check for OpenSubtitles:
    // OpenSubtitles only supports IMDB IDs (starting with "tt").
    bool isOpenSubtitles = (addon.manifest.id == "org.stremio.opensubtitlesv3" ||
                            addon.transportUrl.find("opensubtitles") != std::string::npos);
    if (isOpenSubtitles && resource == "subtitles") {
        if (!id.empty() && id.rfind("tt", 0) != 0) {
            return false;
        }
    }

    for (auto& res : addon.manifest.resources) {
        if (res.name != resource) continue;

        if (!res.types.empty()) {
            bool typeMatch = false;
            for (auto& t : res.types) {
                if (t == type) { typeMatch = true; break; }
                if (type == "anime" && (t == "series" || t == "movie")) { typeMatch = true; break; }
            }
            if (!typeMatch) continue;
        }

        const auto& prefixes = !res.idPrefixes.empty() ? res.idPrefixes : addon.manifest.idPrefixes;
        if (!id.empty() && !prefixes.empty()) {
            bool prefixMatch = false;
            for (auto& prefix : prefixes) {
                if (id.substr(0, prefix.size()) == prefix) {
                    prefixMatch = true;
                    break;
                }
                if (prefix == "tt" && id.substr(0, 6) == "kitsu:") {
                    prefixMatch = true;
                    break;
                }
            }
            if (!prefixMatch) continue;
        }

        return true;
    }
    return false;
}

// ─── Parallel home catalog fetch ─────────────────────────────────────────────
// Each addon is fetched concurrently via std::async so total load time is
// the slowest single addon instead of the sum of all addon times.

std::vector<CatalogRow> AddonManager::getHomeCatalogs(const std::string& type) {
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    using RowVec = std::vector<CatalogRow>;
    std::vector<std::future<RowVec>> futures;
    futures.reserve(localAddons.size());

    for (auto addon : localAddons) {
        if (!addon.enabled) continue;
        futures.push_back(std::async(std::launch::async,
            [this, addon, type]() mutable -> RowVec {
                ensureManifest(addon);
                std::vector<std::future<CatalogRow>> catFutures;
                int count = 0;
                for (auto& cat : addon.manifest.catalogs) {
                    if (!type.empty() && cat.type != type) continue;
                    if (cat.hasRequiredExtras) continue;
                    if (++count > 4) break; // Limit to 4 primary catalogs per addon on home screen

                    catFutures.push_back(std::async(std::launch::async,
                        [this, addon, cat]() -> CatalogRow {
                            CatalogRow row;
                            row.addonName    = addon.manifest.name;
                            row.catalogName  = cat.name.empty() ? cat.id : cat.name;
                            row.type         = cat.type;
                            row.catalogId    = cat.id;
                            row.transportUrl = addon.transportUrl;

                            CatalogResponse resp;
                            if (m_client.fetchCatalog(addon.manifest, cat.type, cat.id, 0, resp)) {
                                row.items = std::move(resp.metas);
                            }
                            return row;
                        }));
                }
                RowVec rows;
                for (auto& cf : catFutures) {
                    auto r = cf.get();
                    if (!r.items.empty()) {
                        rows.push_back(std::move(r));
                    }
                }
                return rows;
            }));
    }

    std::vector<CatalogRow> rows;
    for (auto& fut : futures) {
        auto addonRows = fut.get();
        for (auto& r : addonRows) rows.push_back(std::move(r));
    }
    return rows;
}

// ─── Parallel search ─────────────────────────────────────────────────────────

std::vector<MetaItem> AddonManager::search(const std::string& query,
                                            const std::string& type) {
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    using ItemVec = std::vector<MetaItem>;
    std::vector<std::future<ItemVec>> futures;
    futures.reserve(localAddons.size());

    for (auto addon : localAddons) {
        if (!addon.enabled) continue;
        futures.push_back(std::async(std::launch::async,
            [this, addon, query, type]() mutable -> ItemVec {
                ensureManifest(addon);
                ItemVec results;
                for (auto& cat : addon.manifest.catalogs) {
                    if (!type.empty() && cat.type != type) continue;
                    bool supportsSearch = false;
                    for (auto& extra : cat.extraSupported) {
                        if (extra == "search") { supportsSearch = true; break; }
                    }
                    if (!supportsSearch) continue;

                    CatalogResponse resp;
                    if (m_client.searchCatalog(addon.manifest, cat.type, cat.id, query, resp)) {
                        printf("[AddonManager] Addon '%s' (%s) returned %zu search results for '%s'\n",
                               addon.manifest.name.c_str(), cat.type.c_str(), resp.metas.size(), query.c_str());
                        for (auto& meta : resp.metas) {
                            meta.addonName = addon.manifest.name;
                            results.push_back(std::move(meta));
                        }
                    } else {
                        printf("[AddonManager] Addon '%s' (%s) search failed for '%s'\n",
                               addon.manifest.name.c_str(), cat.type.c_str(), query.c_str());
                    }
                }
                return results;
            }));
    }

    std::vector<MetaItem> results;
    for (auto& fut : futures) {
        auto items = fut.get();
        for (auto& item : items) results.push_back(std::move(item));
    }
    return results;
}

bool AddonManager::getMeta(const std::string& type, const std::string& id,
                            MetaResponse& out) {
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    for (auto& addon : localAddons) {
        ensureManifest(addon);
        if (!addonHandles(addon, "meta", type, id)) continue;
        if (m_client.fetchMeta(addon.manifest, type, id, out))
            return true;
    }
    return false;
}

// ─── Parallel stream fetch ────────────────────────────────────────────────────

std::vector<Stream> AddonManager::getAllStreams(const std::string& type,
                                               const std::string& videoId) {
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    using StreamVec = std::vector<Stream>;
    std::vector<std::future<StreamVec>> futures;
    futures.reserve(localAddons.size());

    for (auto addon : localAddons) {
        if (!addonHandles(addon, "stream", type, videoId)) continue;
        futures.push_back(std::async(std::launch::async,
            [this, addon, type, videoId]() mutable -> StreamVec {
                ensureManifest(addon);
                StreamVec streams;
                StreamResponse resp;
                if (m_client.fetchStreams(addon.manifest, type, videoId, resp)) {
                    for (auto& s : resp.streams) streams.push_back(std::move(s));
                }
                return streams;
            }));
    }

    std::vector<Stream> allStreams;
    for (auto& fut : futures) {
        auto streams = fut.get();
        for (auto& s : streams) allStreams.push_back(std::move(s));
    }
    return allStreams;
}

std::vector<Subtitle> AddonManager::getAllSubtitles(const std::string& type,
                                                     const std::string& id) {
    std::vector<Subtitle> allSubs;
    if (type.empty() || id.empty()) return allSubs;

    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    for (auto& addon : localAddons) {
        if (!addon.enabled) continue;

        // Fast pre-filter: only process addons that could reasonably provide subtitles
        bool couldHandleSubtitles = false;
        for (const auto& res : addon.manifest.resources) {
            if (res.name == "subtitles") {
                couldHandleSubtitles = true;
                break;
            }
        }
        if (!couldHandleSubtitles) {
            std::string tUrl = addon.transportUrl;
            std::string aId = addon.manifest.id;
            for (char& c : tUrl) c = (char)tolower((unsigned char)c);
            for (char& c : aId) c = (char)tolower((unsigned char)c);
            if (tUrl.find("opensubtitles") != std::string::npos ||
                tUrl.find("subtitle") != std::string::npos ||
                aId.find("opensubtitles") != std::string::npos) {
                couldHandleSubtitles = true;
            }
        }
        if (!couldHandleSubtitles) continue;

        ensureManifest(addon);
        if (!addonHandles(addon, "subtitles", type, id)) continue;

        SubtitleResponse resp;
        if (m_client.fetchSubtitles(addon.manifest, type, id, resp)) {
            for (auto& s : resp.subtitles) {
                if (!s.url.empty()) {
                    allSubs.push_back(std::move(s));
                }
            }
        }
    }
    return allSubs;
}

std::string AddonManager::getAddonFallbackName(const std::string& url) {
    std::string low = url;
    for (char& c : low) c = (char)tolower((unsigned char)c);

    if (low.find("cinemeta") != std::string::npos) return "Cinemeta";
    if (low.find("yastream") != std::string::npos) return "yastream";
    if (low.find("kdramacrush") != std::string::npos || low.find("k-drama") != std::string::npos) return "K-Drama Crush";
    if (low.find("cyberflix") != std::string::npos) return "CyberFlix Catalogs";
    if (low.find("torrentio") != std::string::npos) return "Torrentio";
    if (low.find("filtorrent") != std::string::npos) return "Filtorrent";
    if (low.find("dramayo") != std::string::npos) return "Dramayo";
    if (low.find("inmax") != std::string::npos) return "InMax";
    if (low.find("indtorrents") != std::string::npos) return "IndTorrents";
    if (low.find("thepiratebay") != std::string::npos) return "ThePirateBay+";
    if (low.find("torrentsdb") != std::string::npos) return "TorrentsDB";
    if (low.find("streamvix") != std::string::npos) return "StreamViX | ElfHosted";
    if (low.find("nodebrid") != std::string::npos) return "NoDebrid";
    if (low.find("streamasia") != std::string::npos || low.find("dramacool") != std::string::npos) return "StreamAsia";
    if (low.find("animestream") != std::string::npos) return "AnimeStream";
    if (low.find("free.flixnest") != std::string::npos) return "Flix-Streams Free";
    if (low.find("flixnest") != std::string::npos) return "Flix Streams";
    if (low.find("nebulastreams") != std::string::npos) return "Nebula Streams";
    if (low.find("moviebox") != std::string::npos) return "MovieBox";
    if (low.find("sword-watch") != std::string::npos) return "Sword Watch";
    if (low.find("morpheus") != std::string::npos) return "Morpheus Streams";
    if (low.find("yukistreams") != std::string::npos) return "Yukistreams";
    if (low.find("nagare") != std::string::npos) return "Nexio Nagare";
    if (low.find("opensubtitles") != std::string::npos) return "OpenSubtitles v3";
    if (low.find("watchhub") != std::string::npos) return "WatchHub";
    if (low.find("indiastreams") != std::string::npos) return "IndiaStreams";
    if (low.find("indian-streams") != std::string::npos) return "Indian Regional Catalog";
    if (low.find("tmdb-collections") != std::string::npos) return "TMDB Collections";
    if (low.find("streaming-catalogs") != std::string::npos) return "Streaming Catalogs";
    if (low.find("comet") != std::string::npos) return "Comet | ElfHosted";
    if (low.find("knightcrawler") != std::string::npos) return "KnightCrawler";
    if (low.find("debrid-search") != std::string::npos) return "Debrid Search";
    if (low.find("youtube") != std::string::npos) return "YouTube";
    if (low.find("twitch") != std::string::npos) return "TwitchStream";

    // Extract domain from URL
    std::string clean = url;
    size_t proto = clean.find("://");
    if (proto != std::string::npos) clean = clean.substr(proto + 3);
    size_t slash = clean.find('/');
    if (slash != std::string::npos) clean = clean.substr(0, slash);
    if (!clean.empty()) return clean;
    return "Custom Addon";
}

std::string AddonManager::getAddonFallbackDesc(const std::string& url) {
    std::string low = url;
    for (char& c : low) c = (char)tolower((unsigned char)c);

    if (low.find("cinemeta") != std::string::npos) return "The official addon for movie and series catalogs & search.";
    if (low.find("yastream") != std::string::npos) return "Stream Asian dramas, series and movies directly with multiple providers.";
    if (low.find("kdramacrush") != std::string::npos || low.find("k-drama") != std::string::npos) return "Asian and Korean drama catalog (drama feeds).";
    if (low.find("cyberflix") != std::string::npos) return "Movie and series catalogs sorted by streaming provider.";
    if (low.find("torrentio") != std::string::npos) return "Torrent streams from YTS, RARBG, 1337x, etc.";
    if (low.find("filtorrent") != std::string::npos) return "Curated & shaped Torrentio + TorrentsDB streams with resolution filtering.";
    if (low.find("dramayo") != std::string::npos) return "Asian dramas, series and movies catalog & streams.";
    if (low.find("inmax") != std::string::npos) return "Indian and regional movies & series streams from TamilMV / TamilBlasters.";
    if (low.find("indtorrents") != std::string::npos) return "Indian torrent streams for regional movies and series.";
    if (low.find("thepiratebay") != std::string::npos) return "ThePirateBay+ torrent streams provider.";
    if (low.find("torrentsdb") != std::string::npos) return "TorrentsDB multi-tracker torrent stream search.";
    if (low.find("streamvix") != std::string::npos) return "StreamViX multi-source HTTP streams provider.";
    if (low.find("nodebrid") != std::string::npos) return "Direct HTTP/HLS streams without requiring Debrid subscriptions.";
    if (low.find("streamasia") != std::string::npos || low.find("dramacool") != std::string::npos) return "Asian drama, movie and series streams from Dramacool.";
    if (low.find("animestream") != std::string::npos) return "Anime streaming catalog and metadata provider.";
    if (low.find("free.flixnest") != std::string::npos) return "Free HTTP streaming links from public providers.";
    if (low.find("flixnest") != std::string::npos) return "HTTP streams from multiple sources.";
    if (low.find("nebulastreams") != std::string::npos) return "HTTP streams from multiple sources.";
    if (low.find("moviebox") != std::string::npos) return "HTTP streams for movies and series.";
    if (low.find("sword-watch") != std::string::npos) return "HTTP streams provider.";
    if (low.find("morpheus") != std::string::npos) return "Multi-source streaming addon.";
    if (low.find("yukistreams") != std::string::npos) return "Anime and Asian drama streams provider.";
    if (low.find("nagare") != std::string::npos) return "Anime streaming provider.";
    if (low.find("opensubtitles") != std::string::npos) return "Subtitles in 50+ languages.";
    if (low.find("watchhub") != std::string::npos) return "Official streaming links (Netflix, Prime, etc.).";
    if (low.find("indiastreams") != std::string::npos) return "Trending movies and shows from Indian platforms.";
    if (low.find("indian-streams") != std::string::npos) return "Indian regional movies catalog by language.";
    if (low.find("tmdb-collections") != std::string::npos) return "Movie collections grouped by franchise.";
    if (low.find("streaming-catalogs") != std::string::npos) return "Catalogs from Netflix, Disney+, HBO, Prime, etc.";
    if (low.find("comet") != std::string::npos) return "Fast torrent/debrid stream search.";
    if (low.find("knightcrawler") != std::string::npos) return "Alternative torrent and debrid stream search.";
    if (low.find("debrid-search") != std::string::npos) return "Search and stream files directly from your Debrid torrent cloud cache.";
    if (low.find("youtube") != std::string::npos) return "Watch YouTube content directly.";
    if (low.find("twitch") != std::string::npos) return "Watch live gaming and creative streams.";
    return "Custom Stremio-compatible media addon.";
}

std::string AddonManager::getAddonFallbackId(const std::string& url) {
    std::string low = url;
    for (char& c : low) c = (char)tolower((unsigned char)c);

    if (low.find("cinemeta") != std::string::npos) return "com.linvo.cinemeta";
    if (low.find("yastream") != std::string::npos) return "community.yastream";
    if (low.find("kdramacrush") != std::string::npos || low.find("k-drama") != std::string::npos) return "org.stremio.kdramas";
    if (low.find("cyberflix") != std::string::npos) return "com.cyberflix.addon";
    if (low.find("torrentio") != std::string::npos) return "com.stremio.torrentio.addon";
    if (low.find("filtorrent") != std::string::npos) return "community.filtorrent";
    if (low.find("dramayo") != std::string::npos) return "community.dramayo";
    if (low.find("inmax") != std::string::npos) return "in.max.stream";
    if (low.find("indtorrents") != std::string::npos) return "com.stremio.indtorrents";
    if (low.find("thepiratebay") != std::string::npos) return "thepiratebay-plus";
    if (low.find("torrentsdb") != std::string::npos) return "org.stremio.torrentsdb";
    if (low.find("streamvix") != std::string::npos) return "streamvix.hayd.uk";
    if (low.find("nodebrid") != std::string::npos) return "nodebrid.fly.dev";
    if (low.find("streamasia") != std::string::npos || low.find("dramacool") != std::string::npos) return "org.stremio.streamasia";
    if (low.find("animestream") != std::string::npos) return "org.stremio.animestream";
    if (low.find("opensubtitles") != std::string::npos) return "org.stremio.opensubtitlesv3";
    if (low.find("morpheus") != std::string::npos) return "community.morpheus";
    if (low.find("sword-watch") != std::string::npos) return "community.swordwatch";
    if (low.find("yukistreams") != std::string::npos) return "yukistreams.stremio.public";
    if (low.find("nagare") != std::string::npos) return "org.community.nexionagare";

    std::string clean = url;
    size_t proto = clean.find("://");
    if (proto != std::string::npos) clean = clean.substr(proto + 3);
    for (char& c : clean) {
        if (!isalnum((unsigned char)c) && c != '.') c = '_';
    }
    return clean;
}

std::string AddonManager::getAddonCategoryBadge(const InstalledAddon& addon) {
    for (const auto& r : addon.manifest.resources) {
        if (r.name == "stream") return "[Stream]";
    }
    for (const auto& r : addon.manifest.resources) {
        if (r.name == "subtitles") return "[Subtitles]";
    }
    if (!addon.manifest.catalogs.empty()) return "[Catalog]";

    // Fallback based on URL or ID
    std::string low = addon.transportUrl + " " + addon.manifest.id + " " + addon.manifest.name;
    for (char& c : low) c = (char)tolower((unsigned char)c);
    if (low.find("opensubtitles") != std::string::npos || low.find("subtitle") != std::string::npos) return "[Subtitles]";
    if (low.find("torrent") != std::string::npos || low.find("stream") != std::string::npos || low.find("debrid") != std::string::npos) return "[Stream]";
    if (low.find("catalog") != std::string::npos || low.find("meta") != std::string::npos || low.find("movie") != std::string::npos || low.find("series") != std::string::npos) return "[Catalog]";
    return "[Addon]";
}

} // namespace ss
