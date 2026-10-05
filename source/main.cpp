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
#include <unistd.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

static void signalHandler(int) {
    SDL_Event quit_event;
    quit_event.type = SDL_QUIT;
    SDL_PushEvent(&quit_event);
}

#ifdef __SWITCH__
static void showAppletModeWarning() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) return;
    if (TTF_Init() < 0) { SDL_Quit(); return; }

    SDL_Window* window = SDL_CreateWindow("SwitchStream - Applet Mode Warning",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
    if (!window) { TTF_Quit(); SDL_Quit(); return; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) { SDL_DestroyWindow(window); TTF_Quit(); SDL_Quit(); return; }

    PlFontData fontData;
    plGetSharedFontByType(&fontData, PlSharedFontType_Standard);
    SDL_RWops* fontRW = SDL_RWFromMem(fontData.address, fontData.size);
    TTF_Font* fontTitle = TTF_OpenFontRW(fontRW, 0, 32);
    fontRW = SDL_RWFromMem(fontData.address, fontData.size);
    TTF_Font* fontBody = TTF_OpenFontRW(fontRW, 0, 20);
    fontRW = SDL_RWFromMem(fontData.address, fontData.size);
    TTF_Font* fontSmall = TTF_OpenFontRW(fontRW, 0, 16);

    PadState pad;
    padInitializeDefault(&pad);

    bool running = true;
    while (running && appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & (HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_A)) {
            break;
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT || ev.type == SDL_FINGERDOWN || ev.type == SDL_MOUSEBUTTONDOWN) {
                running = false;
                break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 11, 15, 25, 255);
        SDL_RenderClear(renderer);

        SDL_Rect cardRect = { 100, 50, 1080, 620 };
        SDL_SetRenderDrawColor(renderer, 18, 24, 38, 255);
        SDL_RenderFillRect(renderer, &cardRect);
        SDL_SetRenderDrawColor(renderer, 255, 75, 75, 180);
        SDL_RenderDrawRect(renderer, &cardRect);

        SDL_Rect pillRect = { 100 + 40, 50 + 30, 310, 34 };
        SDL_SetRenderDrawColor(renderer, 220, 53, 69, 255);
        SDL_RenderFillRect(renderer, &pillRect);

        auto renderText = [&](const char* text, int x, int y, SDL_Color color, TTF_Font* f) {
            if (!f) return;
            SDL_Surface* s = TTF_RenderUTF8_Blended(f, text, color);
            if (s) {
                SDL_Texture* t = SDL_CreateTextureFromSurface(renderer, s);
                if (t) {
                    SDL_Rect dst = { x, y, s->w, s->h };
                    SDL_RenderCopy(renderer, t, nullptr, &dst);
                    SDL_DestroyTexture(t);
                }
                SDL_FreeSurface(s);
            }
        };

        renderText("! TITLE OVERRIDE REQUIRED", 100 + 55, 50 + 36, {255, 255, 255, 255}, fontSmall);
        renderText("SwitchStream Cannot Run in Applet Mode (Album)", 100 + 40, 50 + 78, {255, 255, 255, 255}, fontTitle);

        renderText("Current Mode: Applet Mode (RAM restricted to ~400MB)", 100 + 40, 50 + 130, {255, 170, 70, 255}, fontBody);
        renderText("SwitchStream needs Full Memory (Application Mode / ~3.5GB RAM) for media decoding & streaming.", 100 + 40, 50 + 160, {180, 195, 215, 255}, fontSmall);
        renderText("Running inside Album causes memory exhaustion and Atmosph\xc3\xa8re crash (0x95D / 2349-0004).", 100 + 40, 50 + 185, {180, 195, 215, 255}, fontSmall);

        SDL_Rect guideBox = { 100 + 40, 50 + 225, 1000, 280 };
        SDL_SetRenderDrawColor(renderer, 14, 18, 30, 255);
        SDL_RenderFillRect(renderer, &guideBox);
        SDL_SetRenderDrawColor(renderer, 45, 60, 85, 255);
        SDL_RenderDrawRect(renderer, &guideBox);

        renderText("HOW TO LAUNCH SWITCHSTREAM WITH FULL RAM (TITLE OVERRIDE):", 100 + 65, 50 + 245, {0, 229, 255, 255}, fontBody);
        renderText("1. Exit to the Nintendo Switch HOME Menu.", 100 + 65, 50 + 285, {255, 255, 255, 255}, fontBody);
        renderText("2. Hold down the [R] bumper button on your controller.", 100 + 65, 50 + 325, {255, 255, 255, 255}, fontBody);
        renderText("3. While holding [R], launch ANY installed game, cartridge, or free game demo.", 100 + 65, 50 + 365, {255, 255, 255, 255}, fontBody);
        renderText("4. Keep holding [R] until the Homebrew Menu opens.", 100 + 65, 50 + 405, {255, 255, 255, 255}, fontBody);
        renderText("5. Launch SwitchStream from there to enjoy full memory and full CPU power!", 100 + 65, 50 + 445, {100, 235, 100, 255}, fontBody);

        SDL_Rect btnRect = { 1280 / 2 - 160, 50 + 535, 320, 48 };
        SDL_SetRenderDrawColor(renderer, 35, 45, 65, 255);
        SDL_RenderFillRect(renderer, &btnRect);
        SDL_SetRenderDrawColor(renderer, 80, 100, 130, 255);
        SDL_RenderDrawRect(renderer, &btnRect);

        renderText("Press [B] or [+] to Exit to hbmenu", 1280 / 2 - 130, 50 + 548, {255, 255, 255, 255}, fontBody);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    if (fontTitle) TTF_CloseFont(fontTitle);
    if (fontBody) TTF_CloseFont(fontBody);
    if (fontSmall) TTF_CloseFont(fontSmall);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
}
#endif

int main(int argc, char* argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef __SWITCH__
    setenv("SSL_CERT_FILE", "romfs:/cacert.pem", 1);
#else
    setenv("SSL_CERT_FILE", "romfs/cacert.pem", 1);
#endif

#ifdef __SWITCH__
    // Initialize Switch services with expanded socket sessions for BitTorrent networking
    SocketInitConfig cfg = *(socketGetDefaultInitConfig());
    cfg.num_bsd_sessions = 16;
    cfg.sb_efficiency    = 8;
    cfg.tcp_tx_buf_size     = 0x4000;
    cfg.tcp_rx_buf_size     = 0x8000;
    cfg.tcp_tx_buf_max_size = 0x40000;
    cfg.tcp_rx_buf_max_size = 0x40000;
    socketInitialize(&cfg);

    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/switchstream", 0777);
    freopen("sdmc:/switch/switchstream/log.txt", "w", stdout);
    dup2(fileno(stdout), STDERR_FILENO);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    plInitialize(PlServiceType_User);

    // Intercept Album / Applet Mode to prevent 0x95D (2349-0004) fatal crash
    if (appletGetAppletType() != AppletType_Application) {
        printf("[SwitchStream] Detected Applet Mode (Album)! Displaying Title Override required screen.\n");
        showAppletModeWarning();
        plExit();
        socketExit();
        return 0;
    }
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
