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

    writer.Key("addons");
    writer.StartArray();
    for (auto& addon : m_addons) {
        writer.StartObject();
        writer.Key("url");
        writer.String(addon.transportUrl.c_str());
        writer.Key("enabled");
        writer.Bool(addon.enabled);
        writer.Key("id");
        writer.String(addon.manifest.id.c_str());
        writer.Key("name");
        writer.String(addon.manifest.name.c_str());
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

bool AddonManager::installAddon(const std::string& transportUrl) {
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

    if (!m_client.fetchManifest(transportUrl, addon.manifest))
        return false;

    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        // Deduplicate by manifest ID — same addon, different URL (e.g. Pengu)
        if (!addon.manifest.id.empty()) {
            for (auto& a : m_addons) {
                if (a.manifest.id == addon.manifest.id) {
                    a.transportUrl = transportUrl;
                    a.manifest     = addon.manifest;
                    return true;
                }
            }
        }
        m_addons.push_back(std::move(addon));
    }
    return true;
}

void AddonManager::removeAddon(const std::string& addonId) {
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    m_addons.erase(
        std::remove_if(m_addons.begin(), m_addons.end(),
            [&](const InstalledAddon& a) { return a.manifest.id == addonId; }),
        m_addons.end()
    );
}

void AddonManager::toggleAddon(const std::string& addonId) {
    std::lock_guard<std::mutex> lock(m_addonsMutex);
    // Toggle ALL entries with this ID — handles any lingering duplicates
    for (auto& a : m_addons) {
        if (a.manifest.id == addonId) {
            a.enabled = !a.enabled;
        }
    }
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
}

bool AddonManager::addonHandles(const InstalledAddon& addon,
                                 const std::string& resource,
                                 const std::string& type,
                                 const std::string& id) const {
    if (!addon.enabled) return false;

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

        if (!id.empty() && !res.idPrefixes.empty()) {
            bool prefixMatch = false;
            for (auto& prefix : res.idPrefixes) {
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
                RowVec rows;
                for (auto& cat : addon.manifest.catalogs) {
                    if (!type.empty() && cat.type != type) continue;
                    if (cat.hasRequiredExtras) continue;

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
                    if (!row.items.empty()) {
                        rows.push_back(std::move(row));
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
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    for (auto& addon : localAddons) {
        ensureManifest(addon);
        if (!addonHandles(addon, "subtitles", type, id)) continue;
        SubtitleResponse resp;
        if (m_client.fetchSubtitles(addon.manifest, type, id, resp)) {
            for (auto& s : resp.subtitles) allSubs.push_back(std::move(s));
        }
    }
    return allSubs;
}

} // namespace ss
