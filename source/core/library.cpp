// ─────────────────────────────────────────────
// Library — Implementation
// ─────────────────────────────────────────────

#include "library.h"

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <fstream>
#include <algorithm>
#include <ctime>

namespace ss {

using namespace rapidjson;

bool Library::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    Document doc;
    doc.Parse(content.c_str());
    if (doc.HasParseError() || !doc.IsObject()) return false;

    m_items.clear();

    if (doc.HasMember("items") && doc["items"].IsArray()) {
        for (auto& v : doc["items"].GetArray()) {
            if (!v.IsObject()) continue;
            LibraryItem item;
            if (v.HasMember("id")) item.id = v["id"].GetString();
            if (v.HasMember("type")) item.type = v["type"].GetString();
            if (v.HasMember("name")) item.name = v["name"].GetString();
            if (v.HasMember("poster")) item.poster = v["poster"].GetString();
            if (v.HasMember("progress")) item.progress = v["progress"].GetDouble();
            if (v.HasMember("lastWatched")) item.lastWatched = v["lastWatched"].GetInt64();
            if (v.HasMember("videoId")) item.videoId = v["videoId"].GetString();
            if (v.HasMember("bookmarked")) item.bookmarked = v["bookmarked"].GetBool();
            m_items[item.id] = std::move(item);
        }
    }

    return true;
}

bool Library::save(const std::string& path) const {
    StringBuffer sb;
    Writer<StringBuffer> writer(sb);

    writer.StartObject();
    writer.Key("items");
    writer.StartArray();
    for (auto& pair : m_items) {
        auto& item = pair.second;
        writer.StartObject();
        writer.Key("id");          writer.String(item.id.c_str());
        writer.Key("type");        writer.String(item.type.c_str());
        writer.Key("name");        writer.String(item.name.c_str());
        writer.Key("poster");      writer.String(item.poster.c_str());
        writer.Key("progress");    writer.Double(item.progress);
        writer.Key("lastWatched"); writer.Int64(item.lastWatched);
        writer.Key("videoId");     writer.String(item.videoId.c_str());
        writer.Key("bookmarked");  writer.Bool(item.bookmarked);
        writer.EndObject();
    }
    writer.EndArray();
    writer.EndObject();

    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << sb.GetString();
    file.close();
    return true;
}

void Library::updateProgress(const std::string& id, const std::string& type,
                              const std::string& name, const std::string& poster,
                              const std::string& videoId, double progress) {
    auto& item = m_items[id];
    item.id = id;
    item.type = type;
    item.name = name;
    item.poster = poster;
    item.videoId = videoId;
    item.progress = progress;
    item.lastWatched = static_cast<int64_t>(std::time(nullptr));
}

void Library::toggleBookmark(const std::string& id, const std::string& type,
                              const std::string& name, const std::string& poster) {
    auto& item = m_items[id];
    item.id = id;
    item.type = type;
    item.name = name;
    item.poster = poster;
    item.bookmarked = !item.bookmarked;
    if (item.lastWatched == 0) {
        item.lastWatched = static_cast<int64_t>(std::time(nullptr));
    }
}

const LibraryItem* Library::getItem(const std::string& id) const {
    auto it = m_items.find(id);
    return (it != m_items.end()) ? &it->second : nullptr;
}

std::vector<LibraryItem> Library::getRecentlyWatched(int limit) const {
    std::vector<LibraryItem> result;
    for (auto& pair : m_items) {
        if (pair.second.lastWatched > 0)
            result.push_back(pair.second);
    }
    std::sort(result.begin(), result.end(),
        [](const LibraryItem& a, const LibraryItem& b) {
            return a.lastWatched > b.lastWatched;
        });
    if ((int)result.size() > limit) result.resize(limit);
    return result;
}

std::vector<LibraryItem> Library::getBookmarked() const {
    std::vector<LibraryItem> result;
    for (auto& pair : m_items) {
        if (pair.second.bookmarked)
            result.push_back(pair.second);
    }
    return result;
}

std::vector<LibraryItem> Library::getContinueWatching(int limit) const {
    std::vector<LibraryItem> result;
    for (auto& pair : m_items) {
        auto& item = pair.second;
        if (item.progress > 0.01 && item.progress < 0.95)
            result.push_back(item);
    }
    std::sort(result.begin(), result.end(),
        [](const LibraryItem& a, const LibraryItem& b) {
            return a.lastWatched > b.lastWatched;
        });
    if ((int)result.size() > limit) result.resize(limit);
    return result;
}

} // namespace ss
