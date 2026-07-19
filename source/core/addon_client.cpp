// ─────────────────────────────────────────────
// Addon Client — Stremio protocol implementation
// ─────────────────────────────────────────────

#include "addon_client.h"
#include "../net/http_client.h"

// Using rapidjson for lightweight parsing
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

namespace ss {

using namespace rapidjson;

// ─── Helpers ─────────────────────────────────

static std::string getString(const Value& obj, const char* key, const std::string& def = "") {
    if (obj.HasMember(key) && obj[key].IsString())
        return obj[key].GetString();
    return def;
}

static int getInt(const Value& obj, const char* key, int def = 0) {
    if (obj.HasMember(key) && obj[key].IsInt())
        return obj[key].GetInt();
    return def;
}

static std::vector<std::string> getStringArray(const Value& obj, const char* key) {
    std::vector<std::string> result;
    if (obj.HasMember(key) && obj[key].IsArray()) {
        for (auto& v : obj[key].GetArray()) {
            if (v.IsString()) result.push_back(v.GetString());
        }
    }
    return result;
}

static MetaItem parseMetaItem(const Value& obj) {
    MetaItem item;
    item.id          = getString(obj, "id");
    item.type        = getString(obj, "type");
    item.name        = getString(obj, "name");
    item.poster      = getString(obj, "poster");
    item.background  = getString(obj, "background");
    item.description = getString(obj, "description");
    item.releaseInfo = getString(obj, "releaseInfo");
    item.runtime     = getString(obj, "runtime");
    item.imdbRating  = getString(obj, "imdbRating");
    item.genres      = getStringArray(obj, "genres");
    item.cast        = getStringArray(obj, "cast");

    // Parse videos (episodes)
    if (obj.HasMember("videos") && obj["videos"].IsArray()) {
        for (auto& v : obj["videos"].GetArray()) {
            if (!v.IsObject()) continue;
            Video vid;
            vid.id        = getString(v, "id");
            vid.title     = getString(v, "title");
            vid.season    = getInt(v, "season");
            vid.episode   = getInt(v, "episode");
            vid.released  = getString(v, "released");
            vid.thumbnail = getString(v, "thumbnail");
            vid.overview  = getString(v, "overview");
            item.videos.push_back(std::move(vid));
        }
    }
    return item;
}

// ─── AddonClient ─────────────────────────────

AddonClient::AddonClient(HttpClient& http) : m_http(http) {}

std::string AddonClient::baseUrl(const std::string& transportUrl) const {
    // Strip /manifest.json if present
    std::string base = transportUrl;
    const std::string suffix = "/manifest.json";
    if (base.size() >= suffix.size() &&
        base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
        base = base.substr(0, base.size() - suffix.size());
    }
    // Remove trailing slash
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}

bool AddonClient::fetchManifest(const std::string& transportUrl, AddonManifest& out) {
    std::string url = transportUrl;
    if (url.find("manifest.json") == std::string::npos) {
        if (url.back() != '/') url += '/';
        url += "manifest.json";
    }

    auto resp = m_http.get(url);
    if (!resp.ok()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    out.id          = getString(doc, "id");
    out.version     = getString(doc, "version");
    out.name        = getString(doc, "name");
    out.description = getString(doc, "description");
    out.logo        = getString(doc, "logo");
    out.background  = getString(doc, "background");
    out.types       = getStringArray(doc, "types");
    out.transportUrl = transportUrl;

    // Parse catalogs
    if (doc.HasMember("catalogs") && doc["catalogs"].IsArray()) {
        for (auto& c : doc["catalogs"].GetArray()) {
            if (!c.IsObject()) continue;
            CatalogDef cat;
            cat.type = getString(c, "type");
            cat.id   = getString(c, "id");
            cat.name = getString(c, "name");

            // Parse extraSupported (quick lookup for which extras are supported)
            cat.extraSupported = getStringArray(c, "extraSupported");

            // If extraSupported is empty, build it from the extra array
            if (cat.extraSupported.empty() && c.HasMember("extra") && c["extra"].IsArray()) {
                for (auto& e : c["extra"].GetArray()) {
                    if (e.IsObject() && e.HasMember("name") && e["name"].IsString()) {
                        cat.extraSupported.push_back(e["name"].GetString());
                    }
                }
            }

            // Check if any extra has isRequired:true (meaning catalog won't work without it)
            cat.hasRequiredExtras = false;
            if (c.HasMember("extraRequired") && c["extraRequired"].IsArray() &&
                c["extraRequired"].GetArray().Size() > 0) {
                cat.hasRequiredExtras = true;
            }
            if (!cat.hasRequiredExtras && c.HasMember("extra") && c["extra"].IsArray()) {
                for (auto& e : c["extra"].GetArray()) {
                    if (e.IsObject() && e.HasMember("isRequired") &&
                        e["isRequired"].IsBool() && e["isRequired"].GetBool()) {
                        cat.hasRequiredExtras = true;
                        break;
                    }
                }
            }

            out.catalogs.push_back(std::move(cat));
        }
    }

    // Parse resources
    if (doc.HasMember("resources") && doc["resources"].IsArray()) {
        for (auto& r : doc["resources"].GetArray()) {
            ResourceDef res;
            if (r.IsString()) {
                res.name = r.GetString();
            } else if (r.IsObject()) {
                res.name       = getString(r, "name");
                res.types      = getStringArray(r, "types");
                res.idPrefixes = getStringArray(r, "idPrefixes");
            }
            out.resources.push_back(std::move(res));
        }
    }

    return true;
}

bool AddonClient::fetchCatalog(const AddonManifest& addon,
                                const std::string& type,
                                const std::string& catalogId,
                                int skip,
                                CatalogResponse& out) {
    std::string url = baseUrl(addon.transportUrl)
        + "/catalog/" + type + "/" + catalogId;
    if (skip > 0) {
        url += "/skip=" + std::to_string(skip);
    }
    url += ".json";

    auto resp = m_http.get(url);
    if (!resp.ok()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    out.metas.clear();
    if (doc.HasMember("metas") && doc["metas"].IsArray()) {
        for (auto& m : doc["metas"].GetArray()) {
            if (!m.IsObject()) continue;
            out.metas.push_back(parseMetaItem(m));
        }
    }

    out.hasMore = doc.HasMember("hasMore") && doc["hasMore"].IsBool() && doc["hasMore"].GetBool();
    // Also treat non-empty results as potentially having more
    if (out.metas.size() >= 20) out.hasMore = true;

    return true;
}

bool AddonClient::searchCatalog(const AddonManifest& addon,
                                 const std::string& type,
                                 const std::string& catalogId,
                                 const std::string& query,
                                 CatalogResponse& out) {
    // URL-encode the query minimally
    std::string encoded;
    for (char c : query) {
        if (c == ' ') encoded += "%20";
        else if (c == '&') encoded += "%26";
        else if (c == '=') encoded += "%3D";
        else encoded += c;
    }

    std::string url = baseUrl(addon.transportUrl)
        + "/catalog/" + type + "/" + catalogId
        + "/search=" + encoded + ".json";

    printf("[AddonClient] Searching URL: %s\n", url.c_str());

    auto resp = m_http.get(url);
    if (!resp.ok()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    out.metas.clear();
    if (doc.HasMember("metas") && doc["metas"].IsArray()) {
        for (auto& m : doc["metas"].GetArray()) {
            if (!m.IsObject()) continue;
            out.metas.push_back(parseMetaItem(m));
        }
    }

    return true;
}

bool AddonClient::fetchMeta(const AddonManifest& addon,
                             const std::string& type,
                             const std::string& id,
                             MetaResponse& out) {
    std::string url = baseUrl(addon.transportUrl)
        + "/meta/" + type + "/" + id + ".json";

    auto resp = m_http.get(url);
    if (!resp.ok()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    if (doc.HasMember("meta") && doc["meta"].IsObject()) {
        out.meta = parseMetaItem(doc["meta"]);
    }

    return true;
}

bool AddonClient::fetchStreams(const AddonManifest& addon,
                               const std::string& type,
                               const std::string& videoId,
                               StreamResponse& out) {
    std::string url = baseUrl(addon.transportUrl)
        + "/stream/" + type + "/" + videoId + ".json";

    auto resp = m_http.get(url, 30); // streams: 30s timeout
    if (!resp.ok()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    out.streams.clear();
    if (doc.HasMember("streams") && doc["streams"].IsArray()) {
        for (auto& s : doc["streams"].GetArray()) {
            if (!s.IsObject()) continue;
            Stream stream;
            stream.url         = getString(s, "url");
            stream.ytId        = getString(s, "ytId");
            stream.infoHash    = getString(s, "infoHash");
            stream.fileIdx     = getInt(s, "fileIdx", -1);
            stream.externalUrl = getString(s, "externalUrl");
            stream.name        = getString(s, "name");
            stream.title       = getString(s, "title");

            if (s.HasMember("behaviorHints") && s["behaviorHints"].IsObject()) {
                auto& bh = s["behaviorHints"];
                stream.notWebReady = bh.HasMember("notWebReady") &&
                                     bh["notWebReady"].IsBool() &&
                                     bh["notWebReady"].GetBool();
                stream.bingeGroup = getString(bh, "bingeGroup");

                std::string headersStr;
                if (bh.HasMember("requestHeaders") && bh["requestHeaders"].IsObject()) {
                    auto& rh = bh["requestHeaders"];
                    for (auto it = rh.MemberBegin(); it != rh.MemberEnd(); ++it) {
                        if (it->name.IsString() && it->value.IsString()) {
                            if (!headersStr.empty()) {
                                headersStr += "\n";
                            }
                            headersStr += std::string(it->name.GetString()) + ": " + std::string(it->value.GetString());
                        }
                    }
                }
                if (bh.HasMember("proxyHeaders") && bh["proxyHeaders"].IsObject()) {
                    auto& ph = bh["proxyHeaders"];
                    if (ph.HasMember("request") && ph["request"].IsObject()) {
                        auto& rh = ph["request"];
                        for (auto it = rh.MemberBegin(); it != rh.MemberEnd(); ++it) {
                            if (it->name.IsString() && it->value.IsString()) {
                                if (!headersStr.empty()) {
                                    headersStr += "\n";
                                }
                                headersStr += std::string(it->name.GetString()) + ": " + std::string(it->value.GetString());
                            }
                        }
                    }
                }
                stream.httpHeaderFields = headersStr;
            }

            // Add streams that have direct URLs, YouTube IDs, or torrent infohashes
            if (!stream.url.empty() || !stream.ytId.empty() || !stream.infoHash.empty()) {
                out.streams.push_back(std::move(stream));
            }
        }
    }

    return true;
}

bool AddonClient::fetchSubtitles(const AddonManifest& addon,
                                  const std::string& type,
                                  const std::string& id,
                                  SubtitleResponse& out) {
    std::string url = baseUrl(addon.transportUrl)
        + "/subtitles/" + type + "/" + id + ".json";

    auto resp = m_http.get(url);
    if (!resp.ok()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    out.subtitles.clear();
    if (doc.HasMember("subtitles") && doc["subtitles"].IsArray()) {
        for (auto& s : doc["subtitles"].GetArray()) {
            if (!s.IsObject()) continue;
            Subtitle sub;
            sub.url  = getString(s, "url");
            sub.lang = getString(s, "lang");
            sub.id   = getString(s, "id");
            if (!sub.url.empty()) {
                out.subtitles.push_back(std::move(sub));
            }
        }
    }

    return true;
}

} // namespace ss
