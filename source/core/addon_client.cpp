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
    item.imdb_id     = getString(obj, "imdb_id");
    if (item.imdb_id.empty() && obj.HasMember("behaviorHints") && obj["behaviorHints"].IsObject()) {
        item.imdb_id = getString(obj["behaviorHints"], "defaultVideoId");
    }
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

// ─── Indian Live TV Verified Channels Database ───
enum class ChannelCategory {
    NATIONAL,
    MUSIC_MOVIES,
    REGIONAL_DEVOTIONAL
};

struct VerifiedChannel {
    const char* id;
    const char* name;
    const char* poster;
    const char* streamUrl;
    const char* genre;
    ChannelCategory category;
};

static const VerifiedChannel VERIFIED_INDIAN_CHANNELS[] = {
    {
        "aajtak_hd",
        "Aaj Tak HD",
        "https://upload.wikimedia.org/wikipedia/en/thumb/0/08/Aaj_Tak_HD_Logo.png/320px-Aaj_Tak_HD_Logo.png",
        "https://feeds.intoday.in/aajtak/api/aajtakhd/master.m3u8",
        "Hindi News",
        ChannelCategory::NATIONAL
    },
    {
        "abp_news",
        "ABP News",
        "https://dtil.tmsimg.com/assets/s158138_ld_h15_aa.png?lock=720x540",
        "https://d1rc86nwwc9fag.cloudfront.net/vglive-sk-472500/abpnews/master.m3u8",
        "Hindi News",
        ChannelCategory::NATIONAL
    },
    {
        "9x_jalwa",
        "9X Jalwa",
        "https://jiotvimages.cdn.jio.com/dare_images/images/9X_Jalwa.png",
        "https://wiselp.wiseplayout.com/9X_Jalwa/master.m3u8",
        "Music & Retro",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "9x_jhakaas",
        "9X Jhakaas",
        "https://jiotvimages.cdn.jio.com/dare_images/images/9X_Jhakaas.png",
        "https://amg01281-9xmediapvtltd-9xjhakaas-samsungin-ci2cs.amagi.tv/playlist/amg01281-9xmediapvtltd-9xjhakaas-samsungin/playlist.m3u8",
        "Marathi Music",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "9x_tashan",
        "9X Tashan",
        "https://jiotvimages.cdn.jio.com/dare_images/images/9X_Tashan.png",
        "https://amg01281-9xmediapvtltd-9xtashan-samsungin-xz1sd.amagi.tv/playlist/amg01281-9xmediapvtltd-9xtashan-samsungin/playlist.m3u8",
        "Punjabi Music",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "b4u_movies",
        "B4U Movies",
        "https://i.imgur.com/M9kMFJl.png",
        "https://streams.tangotv.in/B4UMOVIES/ORIGIN/index.m3u8",
        "Bollywood Movies",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "b4u_kadak",
        "B4U Kadak",
        "https://dtil.tmsimg.com/assets/s142695_ld_h15_aa.png?lock=720x540",
        "https://streams.tangotv.in/B4UKADAK/ORIGIN/index.m3u8",
        "Cinema & Action",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "b4u_music",
        "B4U Music",
        "https://dtil.tmsimg.com/assets/s158141_ld_h15_aa.png?lock=720x540",
        "https://amg01408-amg01408c5-amgplt0747.playout.now3.amagi.tv/b4um001/playlist.m3u8",
        "Music Hits",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "b4u_bhojpuri",
        "B4U Bhojpuri",
        "https://i.imgur.com/NwOQUDp.png",
        "https://cdnb4u.wiseplayout.com/B4U_Bhojpuri/master.m3u8",
        "Regional Cinema",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "bhojpuri_cinema",
        "Bhojpuri Cinema",
        "https://xstreamcp-assets-msp.streamready.in/assets/LIVETV/LIVECHANNEL/LIVETV_LIVETVCHANNEL_BHOJPURI_CINEMA/images/LOGO_HD/image.png",
        "https://live-bhojpuri.akamaized.net/liveabr/playlist.m3u8",
        "Regional Cinema",
        ChannelCategory::MUSIC_MOVIES
    },
    {
        "dangal_tv",
        "Dangal TV",
        "https://xstreamcp-assets-msp.streamready.in/assets/LIVETV/LIVECHANNEL/LIVETV_LIVETVCHANNEL_DANGAL/images/LOGO_HD/image.png",
        "https://live-dangal.akamaized.net/liveabr/playlist.m3u8",
        "Hindi Entertainment",
        ChannelCategory::NATIONAL
    },
    {
        "dangal_2",
        "Dangal 2",
        "https://i.imgur.com/BEr1r7L.png",
        "https://live-dangal2.akamaized.net/liveabr/playlist.m3u8",
        "Drama & Movies",
        ChannelCategory::NATIONAL
    },
    {
        "asianet_news",
        "Asianet News",
        "https://xstreamcp-assets-msp.streamready.in/assets/LIVETV/LIVECHANNEL/LIVETV_LIVETVCHANNEL_ASIANET_NEWS/images/LOGO_HD/image.png",
        "https://amg13737-amg13737c1-amgplt0016.playout.now3.amagi.tv/playlist/amg13737-amg13737c1-amgplt0016/playlist.m3u8",
        "Malayalam News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "abp_majha",
        "ABP Majha",
        "https://jiotvimages.cdn.jio.com/dare_images/images/ABP_Majha.png",
        "https://mumbai-edge.smartplaytv.in/ABPMajha/index.m3u8",
        "Marathi News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "abp_ananda",
        "ABP Ananda",
        "https://jiotvimages.cdn.jio.com/dare_images/images/ABP_Ananda.png",
        "https://d2l4ar6y3mrs4k.cloudfront.net/live-streaming/ananda-livetv/master.m3u8",
        "Bengali News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "abp_asmita",
        "ABP Asmita",
        "https://jiotvimages.cdn.jio.com/dare_images/images/ABP_Asmita.png",
        "https://d2l4ar6y3mrs4k.cloudfront.net/live-streaming/asmita-livetv/master.m3u8",
        "Gujarati News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "abp_ganga",
        "ABP Ganga",
        "https://jiotvimages.cdn.jio.com/dare_images/images/ABP_Ganga.png",
        "https://d2l4ar6y3mrs4k.cloudfront.net/live-streaming/ganga-livetv/master.m3u8",
        "UP & Uttarakhand",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "10_tv",
        "10 TV",
        "https://jiotvimages.cdn.jio.com/dare_images/images/10_TV.png",
        "https://mumbai-edge.smartplaytv.in/10TV/index.m3u8",
        "Telugu News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "abn_andhra_jyothi",
        "ABN Andhra Jyothi",
        "https://jiotvimages.cdn.jio.com/dare_images/images/ABN_Andhra_Jyothi.png",
        "https://mumbai-edge.smartplaytv.in/ABNAJ/index.m3u8",
        "Telugu News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "24_news",
        "24 News",
        "https://sund-images.sunnxt.com/202222/300x300_24News_202222_d63feca0-79ae-47ea-b75a-66c17d456f4c.png",
        "https://mumt07.tangotv.in/zHjX9OFlTWENTYFOURNEWS/index.m3u8",
        "Malayalam News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "aastha_hd",
        "Aastha HD",
        "https://jiotvimages.cdn.jio.com/dare_images/images/Aastha_TV.png",
        "https://aasthaott.akamaized.net/110923/smil:aasthatv.smil/index.m3u8",
        "Devotional",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "aastha_prime_1",
        "Aastha Prime 1",
        "https://jiotvimages.cdn.jio.com/dare_images/images/Aastha_Prime_1.png",
        "https://aasthaott.akamaized.net/110923/smil:aasthaprime1.smil/master.m3u8",
        "Spiritual",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "dd_bharati",
        "DD Bharati",
        "https://i.imgur.com/4tfUIEo.png",
        "https://cdn.pishow.tv/ott/live/10/master.m3u8",
        "National & Heritage",
        ChannelCategory::NATIONAL
    },
    {
        "dd_bihar",
        "DD Bihar",
        "https://i.imgur.com/C7KQiZI.png",
        "https://cdn.pishow.tv/ott/live/35/master.m3u8",
        "Regional News",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "dd_chandana",
        "DD Chandana",
        "https://xstreamcp-assets-msp.streamready.in/assets/LIVETV/LIVECHANNEL/LIVETV_LIVETVCHANNEL_DD_CHANDANA/images/LOGO_HD/image.png",
        "https://d2lk5u59tns74c.cloudfront.net/out/v1/0c980455d2fb4b69bcc6235745ee6039/index.m3u8",
        "Kannada Regional",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "dd_bangla",
        "DD Bangla",
        "https://dtil.tmsimg.com/assets/s142778_ld_h15_aa.png?lock=720x540",
        "https://d3qs3d2rkhfqrt.cloudfront.net/out/v1/7ff57cc9046b4c188b51a0d506f36e7f/index.m3u8",
        "Bengali Regional",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "dd_girnar",
        "DD Girnar",
        "https://dtil.tmsimg.com/assets/s142782_ld_h15_aa.png?lock=720x540",
        "https://d2lk5u59tns74c.cloudfront.net/out/v1/558fdb9aebb54bb5bbbf0ced03686148/index.m3u8",
        "Gujarati Regional",
        ChannelCategory::REGIONAL_DEVOTIONAL
    },
    {
        "4tv_news",
        "4TV News",
        "https://jiotvimages.cdn.jio.com/dare_images/images/4_TV.png",
        "https://cdn.pishow.tv/ott/live/1007/master.m3u8",
        "Urdu & Hindi News",
        ChannelCategory::NATIONAL
    },
    {
        "7s_music",
        "7S Music",
        "https://i.imgur.com/zDiIhdN.png",
        "https://cdn.pishow.tv/ott/live/1257/master.m3u8",
        "Music",
        ChannelCategory::MUSIC_MOVIES
    }
};
static const size_t VERIFIED_INDIAN_CHANNELS_COUNT = sizeof(VERIFIED_INDIAN_CHANNELS) / sizeof(VERIFIED_INDIAN_CHANNELS[0]);

static bool isLiveTvCatalog(const AddonManifest& addon, const std::string& catalogId) {
    std::string trans = addon.transportUrl;
    for (char& c : trans) c = (char)tolower((unsigned char)c);
    std::string cid = catalogId;
    for (char& c : cid) c = (char)tolower((unsigned char)c);

    return trans.find("indian-livetv") != std::string::npos ||
           trans.find("indian.tv") != std::string::npos ||
           trans.find("indian_tv") != std::string::npos ||
           cid == "zee5_home" || cid == "sliv_home" || cid == "mx_home" ||
           cid.find("livetv") != std::string::npos;
}

static const VerifiedChannel* findVerifiedChannel(const std::string& idOrName) {
    if (idOrName.empty()) return nullptr;
    std::string target = idOrName;
    for (char& c : target) c = (char)tolower((unsigned char)c);
    size_t colon = target.find(':');
    if (colon != std::string::npos) {
        target = target.substr(colon + 1);
    }

    // 1. Exact ID match
    for (size_t i = 0; i < VERIFIED_INDIAN_CHANNELS_COUNT; ++i) {
        if (target == VERIFIED_INDIAN_CHANNELS[i].id) {
            return &VERIFIED_INDIAN_CHANNELS[i];
        }
    }
    // 2. Substring ID / Name match
    for (size_t i = 0; i < VERIFIED_INDIAN_CHANNELS_COUNT; ++i) {
        std::string chId = VERIFIED_INDIAN_CHANNELS[i].id;
        std::string chName = VERIFIED_INDIAN_CHANNELS[i].name;
        for (char& c : chName) c = (char)tolower((unsigned char)c);
        if (target.find(chId) != std::string::npos || chId.find(target) != std::string::npos ||
            target.find(chName) != std::string::npos || chName.find(target) != std::string::npos) {
            return &VERIFIED_INDIAN_CHANNELS[i];
        }
    }
    // 3. Keyword matches
    if (target.find("aajtak") != std::string::npos || target.find("aaj") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[0];
    if (target.find("abp") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[1];
    if (target.find("jalwa") != std::string::npos || target.find("9x") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[2];
    if (target.find("b4u") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[5];
    if (target.find("dangal") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[10];
    if (target.find("aastha") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[20];
    if (target.find("dd") != std::string::npos) return &VERIFIED_INDIAN_CHANNELS[22];

    return nullptr;
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
    if (transportUrl.rfind("local:", 0) == 0 || transportUrl.rfind("local://", 0) == 0) return false;
    std::string url = transportUrl;
    if (url.find("manifest.json") == std::string::npos) {
        if (url.back() != '/') url += '/';
        url += "manifest.json";
    }

    auto resp = m_http.get(url, 10);
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
    out.idPrefixes  = getStringArray(doc, "idPrefixes");
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
                res.types = out.types;
                res.idPrefixes = out.idPrefixes;
            } else if (r.IsObject()) {
                res.name       = getString(r, "name");
                res.types      = getStringArray(r, "types");
                res.idPrefixes = getStringArray(r, "idPrefixes");
                if (res.types.empty()) res.types = out.types;
                if (res.idPrefixes.empty()) res.idPrefixes = out.idPrefixes;
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
    // If this is an Indian Live TV catalog, serve verified active HLS channels directly
    if (isLiveTvCatalog(addon, catalogId)) {
        out.metas.clear();
        ChannelCategory targetCat = ChannelCategory::NATIONAL;
        bool filterCategory = false;

        if (catalogId == "zee5_home" || catalogId.find("national") != std::string::npos) {
            targetCat = ChannelCategory::NATIONAL;
            filterCategory = true;
        } else if (catalogId == "sliv_home" || catalogId.find("music") != std::string::npos || catalogId.find("movie") != std::string::npos) {
            targetCat = ChannelCategory::MUSIC_MOVIES;
            filterCategory = true;
        } else if (catalogId == "mx_home" || catalogId.find("regional") != std::string::npos) {
            targetCat = ChannelCategory::REGIONAL_DEVOTIONAL;
            filterCategory = true;
        }

        for (size_t i = 0; i < VERIFIED_INDIAN_CHANNELS_COUNT; ++i) {
            const auto& ch = VERIFIED_INDIAN_CHANNELS[i];
            if (filterCategory && ch.category != targetCat) continue;

            MetaItem meta;
            meta.id = std::string("livetv:") + ch.id;
            meta.type = "tv";
            meta.name = ch.name;
            meta.poster = ch.poster;
            meta.description = std::string(ch.name) + " — " + ch.genre + " • 24/7 Live Stream\nVerified high-definition direct HLS.";
            meta.addonName = "Indian Live TV";
            out.metas.push_back(std::move(meta));
        }

        // If category filter returned empty or too few, append all remaining channels
        if (out.metas.size() < 4) {
            for (size_t i = 0; i < VERIFIED_INDIAN_CHANNELS_COUNT; ++i) {
                const auto& ch = VERIFIED_INDIAN_CHANNELS[i];
                std::string cid = std::string("livetv:") + ch.id;
                bool exists = false;
                for (const auto& m : out.metas) {
                    if (m.id == cid) { exists = true; break; }
                }
                if (!exists) {
                    MetaItem meta;
                    meta.id = cid;
                    meta.type = "tv";
                    meta.name = ch.name;
                    meta.poster = ch.poster;
                    meta.description = std::string(ch.name) + " — " + ch.genre + " • 24/7 Live Stream";
                    meta.addonName = "Indian Live TV";
                    out.metas.push_back(std::move(meta));
                }
            }
        }

        out.hasMore = false;
        return !out.metas.empty();
    }

    std::string base = baseUrl(addon.transportUrl);
    if (base.empty()) {
        printf("[AddonClient] ERROR: fetchCatalog failed - empty transportUrl for addon '%s'\n", addon.id.c_str());
        return false;
    }

    std::string url = base + "/catalog/" + type + "/" + catalogId;
    if (skip > 0) {
        url += "/skip=" + std::to_string(skip);
    }
    url += ".json";

    auto resp = m_http.get(url, 12);
    if (resp.ok()) {
        Document doc;
        doc.Parse(resp.body.c_str());
        if (!doc.HasParseError() && doc.IsObject()) {
            out.metas.clear();
            if (doc.HasMember("metas") && doc["metas"].IsArray()) {
                for (auto& m : doc["metas"].GetArray()) {
                    if (!m.IsObject()) continue;
                    out.metas.push_back(parseMetaItem(m));
                }
            }
            if (!out.metas.empty()) {
                out.hasMore = doc.HasMember("hasMore") && doc["hasMore"].IsBool() && doc["hasMore"].GetBool();
                if (out.metas.size() >= 20) out.hasMore = true;
                return true;
            }
        }
    }

    return false;
}

bool AddonClient::searchCatalog(const AddonManifest& addon,
                                 const std::string& type,
                                 const std::string& catalogId,
                                 const std::string& query,
                                 CatalogResponse& out) {
    // If Live TV search, search verified channels locally
    if (isLiveTvCatalog(addon, catalogId)) {
        out.metas.clear();
        std::string qLow = query;
        for (char& c : qLow) c = (char)tolower((unsigned char)c);

        for (size_t i = 0; i < VERIFIED_INDIAN_CHANNELS_COUNT; ++i) {
            const auto& ch = VERIFIED_INDIAN_CHANNELS[i];
            std::string nLow = ch.name;
            for (char& c : nLow) c = (char)tolower((unsigned char)c);
            std::string gLow = ch.genre;
            for (char& c : gLow) c = (char)tolower((unsigned char)c);

            if (qLow.empty() || nLow.find(qLow) != std::string::npos || gLow.find(qLow) != std::string::npos) {
                MetaItem meta;
                meta.id = std::string("livetv:") + ch.id;
                meta.type = "tv";
                meta.name = ch.name;
                meta.poster = ch.poster;
                meta.description = std::string(ch.name) + " — " + ch.genre + " • 24/7 Live Stream";
                meta.addonName = "Indian Live TV";
                out.metas.push_back(std::move(meta));
            }
        }
        out.hasMore = false;
        return !out.metas.empty();
    }
    // URL-encode the query minimally
    std::string encoded;
    for (char c : query) {
        if (c == ' ') encoded += "%20";
        else if (c == '&') encoded += "%26";
        else if (c == '=') encoded += "%3D";
        else encoded += c;
    }

    std::string base = baseUrl(addon.transportUrl);
    if (base.empty()) return false;
    std::string url = base
        + "/catalog/" + type + "/" + catalogId
        + "/search=" + encoded + ".json";

    printf("[AddonClient] Searching URL: %s\n", url.c_str());

    auto resp = m_http.get(url, 10);
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
    if (id.rfind("livetv:", 0) == 0 || isLiveTvCatalog(addon, "") ||
        id.rfind("zee5:", 0) == 0 || id.rfind("sliv:", 0) == 0 || id.rfind("mx:", 0) == 0) {
        const VerifiedChannel* ch = findVerifiedChannel(id);
        if (ch) {
            out.meta.id = id;
            out.meta.type = "tv";
            out.meta.name = ch->name;
            out.meta.poster = ch->poster;
            out.meta.description = std::string(ch->name) + " — " + ch->genre + " • 24/7 Live Stream\nVerified direct HLS streaming.";
            out.meta.addonName = "Indian Live TV";
            return true;
        }
    }

    std::string base = baseUrl(addon.transportUrl);
    if (!base.empty()) {
        std::string url = base
            + "/meta/" + type + "/" + id + ".json";

        auto resp = m_http.get(url, 10);
        if (resp.ok()) {
            Document doc;
            doc.Parse(resp.body.c_str());
            if (!doc.HasParseError() && doc.IsObject()) {
                if (doc.HasMember("meta") && doc["meta"].IsObject()) {
                    out.meta = parseMetaItem(doc["meta"]);
                    return true;
                }
            }
        }
    }

    return false;
}

bool AddonClient::fetchStreams(const AddonManifest& addon,
                               const std::string& type,
                               const std::string& videoId,
                               StreamResponse& out) {
    out.streams.clear();

    // Direct resolution for Indian Live TV channels
    if (videoId.rfind("livetv:", 0) == 0 || isLiveTvCatalog(addon, "") ||
        videoId.rfind("zee5:", 0) == 0 || videoId.rfind("sliv:", 0) == 0 || videoId.rfind("mx:", 0) == 0) {
        const VerifiedChannel* ch = findVerifiedChannel(videoId);
        if (!ch && isLiveTvCatalog(addon, "")) {
            ch = &VERIFIED_INDIAN_CHANNELS[0]; // Fallback to Aaj Tak HD
        }
        if (ch) {
            Stream stream;
            stream.url = ch->streamUrl;
            stream.name = ch->name;
            stream.title = std::string(ch->name) + " • Direct HLS Live Stream (1080p/720p)";
            stream.addonName = "Indian Live TV";
            out.streams.push_back(std::move(stream));
            return true;
        }
    }

    std::string base = baseUrl(addon.transportUrl);
    if (!base.empty()) {
        std::string url = base
            + "/stream/" + type + "/" + videoId + ".json";

        auto resp = m_http.get(url, 45); // streams: 45s timeout for upstream scrapers
        if (resp.ok()) {
            Document doc;
            doc.Parse(resp.body.c_str());
            if (!doc.HasParseError() && doc.IsObject()) {
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
                        if (stream.title.empty()) {
                            stream.title = getString(s, "description");
                        }

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
            }
        }
    }

    return !out.streams.empty();
}

bool AddonClient::fetchSubtitles(const AddonManifest& addon,
                                  const std::string& type,
                                  const std::string& id,
                                  SubtitleResponse& out) {
    out.subtitles.clear();
    if (addon.transportUrl.empty() || type.empty() || id.empty()) return false;

    std::string url = baseUrl(addon.transportUrl)
        + "/subtitles/" + type + "/" + id + ".json";

    auto resp = m_http.get(url, 8);
    if (!resp.ok() || resp.body.empty()) return false;

    Document doc;
    doc.Parse(resp.body.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    if (doc.HasMember("subtitles") && doc["subtitles"].IsArray()) {
        for (auto& s : doc["subtitles"].GetArray()) {
            if (!s.IsObject()) continue;
            Subtitle sub;
            sub.url  = getString(s, "url");
            sub.lang = getString(s, "lang");
            sub.id   = getString(s, "id");
            if (s.HasMember("subtitleFileName") && s["subtitleFileName"].IsString()) {
                sub.title = s["subtitleFileName"].GetString();
            } else if (s.HasMember("movieReleaseName") && s["movieReleaseName"].IsString()) {
                sub.title = s["movieReleaseName"].GetString();
            } else if (s.HasMember("title") && s["title"].IsString()) {
                sub.title = s["title"].GetString();
            }
            if (!sub.url.empty()) {
                out.subtitles.push_back(std::move(sub));
            }
        }
    }

    return true;
}

} // namespace ss
