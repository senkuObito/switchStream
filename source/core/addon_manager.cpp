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

            // Try to fetch manifest (we can do this lazily too)
            if (m_client.fetchManifest(url, addon.manifest)) {
                std::lock_guard<std::mutex> lock(m_addonsMutex);
                m_addons.push_back(std::move(addon));
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
    // Check if already installed
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
    for (auto& a : m_addons) {
        if (a.manifest.id == addonId) {
            a.enabled = !a.enabled;
            break;
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

        // If resource has specific types, check them
        if (!res.types.empty()) {
            bool typeMatch = false;
            for (auto& t : res.types) {
                if (t == type) { typeMatch = true; break; }
            }
            if (!typeMatch) continue;
        }

        // If resource has id prefixes, check them
        if (!id.empty() && !res.idPrefixes.empty()) {
            bool prefixMatch = false;
            for (auto& prefix : res.idPrefixes) {
                if (id.substr(0, prefix.size()) == prefix) {
                    prefixMatch = true;
                    break;
                }
            }
            if (!prefixMatch) continue;
        }

        return true;
    }

    // Some addons list resources as simple strings (no types/prefixes)
    // In that case, the resource name in manifest.resources matches directly
    return false;
}

std::vector<CatalogRow> AddonManager::getHomeCatalogs(const std::string& type) {
    std::vector<CatalogRow> rows;
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    for (auto& addon : localAddons) {
        if (!addon.enabled) continue;

        for (auto& cat : addon.manifest.catalogs) {
            if (!type.empty() && cat.type != type) continue;

            // Skip catalogs that require extra parameters we can't provide
            if (cat.hasRequiredExtras) continue;

            CatalogRow row;
            row.addonName   = addon.manifest.name;
            row.catalogName = cat.name.empty() ? cat.id : cat.name;
            row.type        = cat.type;
            row.catalogId   = cat.id;
            row.transportUrl = addon.transportUrl;

            CatalogResponse resp;
            if (m_client.fetchCatalog(addon.manifest, cat.type, cat.id, 0, resp)) {
                row.items = std::move(resp.metas);
            }

            if (!row.items.empty()) {
                rows.push_back(std::move(row));
            }
        }
    }

    return rows;
}

std::vector<MetaItem> AddonManager::search(const std::string& query,
                                            const std::string& type) {
    std::vector<MetaItem> results;
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    for (auto& addon : localAddons) {
        if (!addon.enabled) continue;

        for (auto& cat : addon.manifest.catalogs) {
            if (!type.empty() && cat.type != type) continue;

            // Only search catalogs that explicitly support search
            bool supportsSearch = false;
            for (auto& extra : cat.extraSupported) {
                if (extra == "search") { supportsSearch = true; break; }
            }
            if (!supportsSearch) continue;

            CatalogResponse resp;
            if (m_client.searchCatalog(addon.manifest, cat.type, cat.id, query, resp)) {
                printf("[AddonManager] Addon '%s' (%s) returned %zu search results for query '%s'\n",
                       addon.manifest.name.c_str(), cat.type.c_str(), resp.metas.size(), query.c_str());
                for (auto& meta : resp.metas) {
                    meta.addonName = addon.manifest.name;
                    results.push_back(std::move(meta));
                }
            } else {
                printf("[AddonManager] Addon '%s' (%s) search failed for query '%s'\n",
                       addon.manifest.name.c_str(), cat.type.c_str(), query.c_str());
            }
        }
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
        if (!addonHandles(addon, "meta", type, id)) continue;

        if (m_client.fetchMeta(addon.manifest, type, id, out))
            return true;
    }
    return false;
}

std::vector<Stream> AddonManager::getAllStreams(const std::string& type,
                                               const std::string& videoId) {
    std::vector<Stream> allStreams;
    std::vector<InstalledAddon> localAddons;
    {
        std::lock_guard<std::mutex> lock(m_addonsMutex);
        localAddons = m_addons;
    }

    for (auto& addon : localAddons) {
        if (!addonHandles(addon, "stream", type, videoId)) continue;

        StreamResponse resp;
        if (m_client.fetchStreams(addon.manifest, type, videoId, resp)) {
            for (auto& s : resp.streams) {
                allStreams.push_back(std::move(s));
            }
        }
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
        if (!addonHandles(addon, "subtitles", type, id)) continue;

        SubtitleResponse resp;
        if (m_client.fetchSubtitles(addon.manifest, type, id, resp)) {
            for (auto& s : resp.subtitles) {
                allSubs.push_back(std::move(s));
            }
        }
    }

    return allSubs;
}

} // namespace ss
