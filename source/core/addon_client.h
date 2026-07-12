#pragma once

// ─────────────────────────────────────────────
// Addon Client — Stremio addon protocol client
// Fetches manifests, catalogs, meta, streams
// ─────────────────────────────────────────────

#include "types.h"
#include <string>
#include <vector>

namespace ss {

class HttpClient;

class AddonClient {
public:
    explicit AddonClient(HttpClient& http);

    // Fetch and parse addon manifest from transport URL
    // transportUrl should end with /manifest.json or we append it
    bool fetchManifest(const std::string& transportUrl, AddonManifest& out);

    // Fetch catalog
    bool fetchCatalog(const AddonManifest& addon,
                      const std::string& type,
                      const std::string& catalogId,
                      int skip,
                      CatalogResponse& out);

    // Fetch catalog with search query
    bool searchCatalog(const AddonManifest& addon,
                       const std::string& type,
                       const std::string& catalogId,
                       const std::string& query,
                       CatalogResponse& out);

    // Fetch meta details
    bool fetchMeta(const AddonManifest& addon,
                   const std::string& type,
                   const std::string& id,
                   MetaResponse& out);

    // Fetch streams
    bool fetchStreams(const AddonManifest& addon,
                     const std::string& type,
                     const std::string& videoId,
                     StreamResponse& out);

    // Fetch subtitles
    bool fetchSubtitles(const AddonManifest& addon,
                        const std::string& type,
                        const std::string& id,
                        SubtitleResponse& out);

private:
    // Build the base URL from transport URL
    std::string baseUrl(const std::string& transportUrl) const;

    HttpClient& m_http;
};

} // namespace ss
