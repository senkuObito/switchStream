#include <iostream>
#include <curl/curl.h>
#include <vector>
#include "source/net/http_client.h"
#include "source/core/addon_client.h"

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    ss::HttpClient http;
    ss::AddonClient client(http);

    std::vector<std::string> urls = {
        "https://pengu.uk/%7B%22source_111477%22%3A%22on%22%2C%22source_4khdhub%22%3A%22on%22%2C%22source_cinefreak%22%3A%22on%22%2C%22source_aniwaves%22%3A%22on%22%2C%22source_moviebox%22%3A%22on%22%2C%22source_moviesdrives%22%3A%22on%22%2C%22source_allmovieland%22%3A%22on%22%2C%22source_overflix%22%3A%22on%22%2C%22source_vaplayer%22%3A%22on%22%2C%22source_vidking%22%3A%22on%22%2C%22source_animesuge%22%3A%22on%22%2C%22source_aether%22%3A%22on%22%2C%22source_vidlink%22%3A%22on%22%2C%22source_hdghartv%22%3A%22on%22%2C%22source_scloud%22%3A%22on%22%2C%22res_2160%22%3A%22on%22%2C%22res_1080%22%3A%22on%22%2C%22res_720%22%3A%22on%22%2C%22res_480%22%3A%22on%22%2C%22disable_direct%22%3A%22on%22%7D/manifest.json",
        "https://flixnest.app/flix-streams/manifest.json",
        "https://nebulastreams.onrender.com/manifest.json",
        "https://moviebox-cfa7.onrender.com/manifest.json"
    };

    for (const auto& url : urls) {
        std::cout << "\n========================================\n";
        std::cout << "Testing URL: " << url << std::endl;
        
        ss::AddonManifest manifest;
        bool success = client.fetchManifest(url, manifest);
        if (!success) {
            std::cout << "Failed to fetch manifest!" << std::endl;
            continue;
        }
        
        std::cout << "Manifest Name: " << manifest.name << " | ID: " << manifest.id << std::endl;
        
        ss::StreamResponse streams;
        std::cout << "Fetching streams for tt0137523..." << std::endl;
        success = client.fetchStreams(manifest, "movie", "tt0137523", streams);
        if (!success) {
            std::cout << "Failed to fetch streams!" << std::endl;
            continue;
        }
        
        std::cout << "Success! Streams Count: " << streams.streams.size() << std::endl;
        for (const auto& s : streams.streams) {
            std::cout << "  - " << s.name << " | " << s.title << "\n    URL: " << s.url << std::endl;
            if (!s.httpHeaderFields.empty()) {
                std::cout << "    Headers:\n" << s.httpHeaderFields << std::endl;
            }
        }
    }

    curl_global_cleanup();
    return 0;
}
