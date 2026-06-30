#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <stdarg.h>
#include <sys/stat.h>

#include <switch.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>

#ifdef DEBUG
    #include <twili.h>
#endif

extern "C" {
    #include "common.h"
    #include "textures.h"
    #include "MenuChooser.h"
    #include "menu_book_reader.h"
    #include "fs.h"
    #include "config.h"
}

SDL_Renderer* RENDERER;
SDL_Window* WINDOW;
SDL_Event EVENT;
TTF_Font *ROBOTO_35, *ROBOTO_30, *ROBOTO_27, *ROBOTO_25, *ROBOTO_20, *ROBOTO_15;
bool configDarkMode;

static bool g_servicesInitialized = false;
static FILE *g_startupLog = NULL;

static void StartupLog(const char *fmt, ...) {
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    printf("%s\n", buffer);
    fflush(stdout);

    if (g_startupLog) {
        fprintf(g_startupLog, "%s\n", buffer);
        fflush(g_startupLog);
    }
}

static void StartupLog_Open() {
    // Ensure common SD mountpoint paths exist before opening the log file.
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/eBookReader", 0777);
    FS_RecursiveMakeDir("/switch/eBookReader");

    g_startupLog = fopen("sdmc:/switch/eBookReader/startup.log", "a");
    if (!g_startupLog) {
        g_startupLog = fopen("sdmc:/switch/startup.log", "a");
    }
    if (!g_startupLog) {
        g_startupLog = fopen("/switch/eBookReader/startup.log", "a");
    }
    if (!g_startupLog) {
        g_startupLog = fopen("/switch/startup.log", "a");
    }

    if (g_startupLog) {
        fprintf(g_startupLog, "==== eBookReader startup ====\n");
        fflush(g_startupLog);
    }
}

static void StartupLog_Close() {
    if (g_startupLog) {
        fprintf(g_startupLog, "==== shutdown ====\n");
        fclose(g_startupLog);
        g_startupLog = NULL;
    }
}

void Term_Services() {
    if (!g_servicesInitialized) {
        StartupLog("Terminate_Services skipped (not initialized)");
        StartupLog_Close();
        return;
    }

    StartupLog("Terminate Services");

    timeExit();
    if (ROBOTO_35) TTF_CloseFont(ROBOTO_35);
    if (ROBOTO_30) TTF_CloseFont(ROBOTO_30);
    if (ROBOTO_27) TTF_CloseFont(ROBOTO_27);
    if (ROBOTO_25) TTF_CloseFont(ROBOTO_25);
    if (ROBOTO_20) TTF_CloseFont(ROBOTO_20);
    if (ROBOTO_15) TTF_CloseFont(ROBOTO_15);
    TTF_Quit();

    Textures_Free();
    romfsExit();

    IMG_Quit();

    if (RENDERER) SDL_DestroyRenderer(RENDERER);
    //SDL_FreeSurface(WINDOW_SURFACE);
    if (WINDOW) SDL_DestroyWindow(WINDOW);
    SDL_Quit();

    #ifdef DEBUG
        twiliExit();
    #endif

    g_servicesInitialized = false;
    StartupLog_Close();
}

bool Init_Services() {
    #ifdef DEBUG
        twiliInitialize();
    #endif

    StartupLog("Initialize Services");

    if (R_FAILED(romfsInit())) {
        StartupLog("romfsInit failed");
        return false;
    }
    StartupLog("Initialized RomFs");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0) {
        StartupLog("SDL_Init failed: %s", SDL_GetError());
        Term_Services();
        return false;
    }
    StartupLog("Initialized SDL");

    timeInitialize();
    StartupLog("Initialized Time");

    if (SDL_CreateWindowAndRenderer(1280, 720, 0, &WINDOW, &RENDERER) == -1)  {
        StartupLog("SDL_CreateWindowAndRenderer failed: %s", SDL_GetError());
        Term_Services();
        return false;
    }
    StartupLog("Initialized Window and Renderer");

    SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_BLEND);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "2");

    int imgFlags = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgFlags) & imgFlags) != imgFlags) {
        StartupLog("IMG_Init failed: %s", IMG_GetError());
        Term_Services();
        return false;
    }
    StartupLog("Initialized Image");

    if(TTF_Init() == -1) {
        StartupLog("TTF_Init failed: %s", TTF_GetError());
        Term_Services();
        return false;
    }
    StartupLog("Initialized TTF");

    Textures_Load();
    StartupLog("Loaded Textures");

    ROBOTO_35 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 35);
    ROBOTO_30 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 30);
    ROBOTO_27 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 27);
    ROBOTO_25 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 25);
    ROBOTO_20 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 20);
    ROBOTO_15 = TTF_OpenFont("romfs:/resources/font/Roboto-Light.ttf", 15);
    if (!ROBOTO_35 || !ROBOTO_30 || !ROBOTO_27 || !ROBOTO_25 || !ROBOTO_20 || !ROBOTO_15) {
        StartupLog("Failure to retrieve fonts");
        Term_Services();
        return false;
    }
    StartupLog("Retrieved Fonts");
    
    int joystickCount = SDL_NumJoysticks();
    for (int i = 0; i < joystickCount; i++) {
        SDL_JoystickOpen(i);
    }
    StartupLog("Initialized Input (joysticks=%d)", joystickCount);

    FS_RecursiveMakeDir("/switch/eBookReader/books");
    StartupLog("Created book directory if needed");

    configDarkMode = true;
    g_servicesInitialized = true;
    StartupLog("Init_Services finished successfully");
    return true;
}

int main(int argc, char *argv[]) {
    StartupLog_Open();
    StartupLog("main entry argc=%d", argc);

    if (!Init_Services()) {
        StartupLog("Startup failed before entering app loop");
        Term_Services();
        return 1;
    }

    if (argc == 2) {
        Menu_OpenBook(argv[1]);
    } else {
        Menu_StartChoosing();
    }

    Term_Services();
    return 0;
}