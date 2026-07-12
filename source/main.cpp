// SwitchStream — Main entry point
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "app.h"
#include <curl/curl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <sys/stat.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

static void signalHandler(int) {
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);
}

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef __SWITCH__
    setenv("SSL_CERT_FILE", "romfs:/cacert.pem", 1);
#else
    setenv("SSL_CERT_FILE", "romfs/cacert.pem", 1);
#endif

#ifdef __SWITCH__
    // Initialize Switch services
    socketInitializeDefault();
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/switchstream", 0777);
    freopen("sdmc:/switch/switchstream/log.txt", "w", stdout);
    freopen("sdmc:/switch/switchstream/log.txt", "w", stderr);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    plInitialize(PlServiceType_User);
#endif

    // Global curl init (once)
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Handle external kill signals gracefully
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGINT, signalHandler);

    // Ensure data directory exists
#ifdef __SWITCH__
    mkdir("sdmc:/switch/switchstream", 0777);
#else
    mkdir("switchstream_data", 0777);
#endif

    // Create and run app
    ss::App app;
    if (app.init()) {
        app.run();
    }
    app.shutdown();

    // Cleanup
    curl_global_cleanup();
#ifdef __SWITCH__
    plExit();
    socketExit();
#endif
    return 0;
}
