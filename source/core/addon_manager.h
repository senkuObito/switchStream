#pragma once

// ─────────────────────────────────────────────
// Addon Manager — Manages installed addons
// Aggregates results across all addons
// ─────────────────────────────────────────────

#include "types.h"
#include <vector>
#include <string>
#include <mutex>

namespace ss {

class AddonClient;

// A single catalog row for the home screen
struct CatalogRow {
    std::string addonName;
    std::string catalogName;
    std::string type;
    std::string catalogId;
    std::string transportUrl;
    std::vector<MetaItem> items;
};

class AddonManager {
public:
    explicit AddonManager(AddonClient& client);

    // Load installed addons from SD card config
    bool loadConfig(const std::string& configPath);

    // Save installed addons to SD card
    bool saveConfig(const std::string& configPath) const;

    // Install addon by transport URL (fetches manifest)
    bool installAddon(const std::string& transportUrl);

    // Remove addon by ID
    void removeAddon(const std::string& addonId);

    // Toggle addon enabled/disabled status
    void toggleAddon(const std::string& addonId);

    // Get all installed addons
    std::vector<InstalledAddon> getAddons() const;

    // ─── Aggregated queries ──────────────────

    // Get home screen catalog rows (one per catalog across all addons)
    // Only fetches first page (lightweight)
    std::vector<CatalogRow> getHomeCatalogs(const std::string& type = "");

    // Search across all addons
    std::vector<MetaItem> search(const std::string& query, const std::string& type = "");

    // Get meta from best available addon
    bool getMeta(const std::string& type, const std::string& id, MetaResponse& out);

    // Get all streams from all addons
    std::vector<Stream> getAllStreams(const std::string& type, const std::string& videoId);

    // Get subtitles from all addons
    std::vector<Subtitle> getAllSubtitles(const std::string& type, const std::string& id);

    std::string getTorrServerHost() const { return m_torrserverHost; }
    void setTorrServerHost(const std::string& host) {
        m_torrserverHost = host;
        while (!m_torrserverHost.empty() && m_torrserverHost.back() == '/') {
            m_torrserverHost.pop_back();
        }
    }

    bool getHwDecode() const { return m_hwDecode; }
    void setHwDecode(bool enable) { m_hwDecode = enable; }

    std::string getSubtitleLang() const { return m_subtitleLang; }
    void setSubtitleLang(const std::string& lang) { m_subtitleLang = lang; }

    bool getEnableTorrents() const { return m_enableTorrents; }
    void setEnableTorrents(bool enable) { m_enableTorrents = enable; }

private:
    // Ensure manifest is fetched on the fly if it failed at startup
    void ensureManifest(InstalledAddon& addon);

    // Check if addon handles this resource for this type/id
    bool addonHandles(const InstalledAddon& addon,
                      const std::string& resource,
                      const std::string& type,
                      const std::string& id = "") const;

    AddonClient& m_client;
    std::vector<InstalledAddon> m_addons;
    mutable std::mutex m_addonsMutex;
    std::string m_torrserverHost = "http://127.0.0.1:8090";
    bool m_hwDecode = true;
    std::string m_subtitleLang = "en";
    bool m_enableTorrents = false; // Default: false (completely independent streaming by default)
};

} // namespace ss
