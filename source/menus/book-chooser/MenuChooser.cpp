extern "C" {
    #include "MenuChooser.h"
    #include "menu_book_reader.h"
    #include "SDL_helper.h"
    #include "common.h"
    #include "textures.h"
    #include "config.h"
}

#include <switch.h>
#include <iostream>
#include <filesystem>
#include <vector>
#include <list>
#include <algorithm>
#include <cctype>
#include <system_error>
#include <mupdf/fitz.h>

#include <SDL2/SDL_image.h>

using namespace std;
namespace fs = filesystem;

extern fz_context *ctx;

template <typename T> bool contains(const list<T> & listOfElements, const T & element) {
	auto it = find(listOfElements.begin(), listOfElements.end(), element);
	return it != listOfElements.end();
}

static string lower_copy(string value) {
    transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return (char)tolower(c); });
    return value;
}

struct BookEntry {
    string filename;
    string fullPath;
    string extension;
    bool warned = false;
};

extern TTF_Font *ROBOTO_35, *ROBOTO_25, *ROBOTO_15;

void Menu_StartChoosing() {
    int choosenIndex = 0;
    bool readingBook = false;
    list<string> warnedExtentions = {".epub", ".cbz", ".xps", ".fb2", ".mobi", ".html"};

    string path = "/switch/eBookReader/books";

    if (ctx == NULL) {
        ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
        fz_register_document_handlers(ctx);
    }

    vector<BookEntry> books;
    std::error_code dirEc;
    fs::directory_iterator dirIt(path, dirEc);
    fs::directory_iterator endIt;
    while (!dirEc && dirIt != endIt) {
        const auto &entry = *dirIt;
        if (entry.is_regular_file()) {
            string filename = entry.path().filename().string();
            string fullPath = entry.path().string();
            string extension = lower_copy(entry.path().extension().string());

            const fz_document_handler *handler = NULL;
            if (ctx) {
                fz_try(ctx) {
                    handler = fz_recognize_document(ctx, fullPath.c_str());
                }
                fz_catch(ctx) {
                    handler = NULL;
                }
            }

            if (handler) {
                BookEntry book;
                book.filename = filename;
                book.fullPath = fullPath;
                book.extension = extension;
                book.warned = contains(warnedExtentions, extension);
                books.push_back(book);
            }
        }

        dirIt.increment(dirEc);
    }

    sort(books.begin(), books.end(), [](const BookEntry &a, const BookEntry &b) {
        return lower_copy(a.filename) < lower_copy(b.filename);
    });

    int amountOfFiles = (int)books.size();

    bool isWarningOnScreen = false;
    int windowX, windowY;
    SDL_GetWindowSize(WINDOW, &windowX, &windowY);
    int warningWidth = 700;
    int warningHeight = 300;

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    while(appletMainLoop()) {
        if (readingBook) {
            break;
        }

        SDL_Color textColor = configDarkMode ? WHITE : BLACK;
        SDL_Color backColor = configDarkMode ? BACK_BLACK : BACK_WHITE;

        SDL_ClearScreen(RENDERER, backColor);
		SDL_RenderClear(RENDERER);

	padUpdate(&pad);

	u64 kDown = padGetButtonsDown(&pad);
	u64 kHeld = padGetButtons(&pad);
	u64 kUp = padGetButtonsUp(&pad);

        if (!isWarningOnScreen && kDown & HidNpadButton_Plus) {
            break;
        }

        if (kDown & HidNpadButton_B) {
            if (!isWarningOnScreen) {
                break;
            } else {
                isWarningOnScreen = false;
            }
        }

        if (kDown & HidNpadButton_A) {
            if (amountOfFiles > 0 && choosenIndex >= 0 && choosenIndex < amountOfFiles) {
                const BookEntry &book = books[choosenIndex];
                if (book.warned && !isWarningOnScreen) {
                    isWarningOnScreen = true;
                } else {
                    cout << "Opening book: " << book.fullPath << endl;
                    Menu_OpenBook((char*) book.fullPath.c_str());
                    readingBook = true;
                }
            }
        }

        if (kDown & HidNpadButton_Up || kDown & HidNpadButton_StickRUp) {
            if (amountOfFiles <= 0) {
                choosenIndex = 0;
            } else if (choosenIndex != 0 && !isWarningOnScreen) {
                choosenIndex--;
            } else if (choosenIndex == 0) {
                choosenIndex = amountOfFiles-1;
            }
        }

        if (kDown & HidNpadButton_Down || kDown & HidNpadButton_StickRDown) {
            if (amountOfFiles <= 0) {
                choosenIndex = 0;
            } else if (choosenIndex == amountOfFiles-1) {
                choosenIndex = 0;
            } else if (choosenIndex < amountOfFiles-1 && !isWarningOnScreen) {
                choosenIndex++;
            }
        }

        if (kUp & HidNpadButton_Minus) {
            configDarkMode = !configDarkMode;
        }

        int exitWidth = 0;
        TTF_SizeText(ROBOTO_20, "Exit", &exitWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_b, ROBOTO_20, textColor, "Exit", windowX - exitWidth - 50, windowY - 10, 35, 35, 5, 0);

        int themeWidth = 0;
        TTF_SizeText(ROBOTO_20, "Switch Theme", &themeWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_minus, ROBOTO_20, textColor, "Switch Theme", windowX - themeWidth - 50, windowY - 40, 35, 35, 5, 0);

        for (int choosingIndex = 0; choosingIndex < amountOfFiles; choosingIndex++) {
            const BookEntry &book = books[choosingIndex];
            if (choosenIndex == choosingIndex) {
                SDL_DrawRect(RENDERER, 15, 15 + (40 * choosingIndex), 1265, 40, configDarkMode ? SELECTOR_COLOUR_DARK : SELECTOR_COLOUR_LIGHT);
            }

            if (book.warned) {
                SDL_DrawImage(RENDERER, warning, 25, 18 + (40 * choosingIndex));
            }

            SDL_DrawText(RENDERER, ROBOTO_25, 70, 20 + (40 * choosingIndex), textColor, book.filename.c_str());
        }

        if (amountOfFiles == 0) {
            SDL_DrawText(RENDERER, ROBOTO_30, 35, 80, textColor, "No supported books found in /switch/eBookReader/books");
            SDL_DrawText(RENDERER, ROBOTO_20, 35, 120, textColor, "Supported files are detected through MuPDF document handlers.");
        }

        if (isWarningOnScreen) {
            if (!configDarkMode) { // Display a dimmed background if on light mode
                SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));
            }

            SDL_DrawRect(RENDERER, (windowX - warningWidth) / 2, (windowY - warningHeight) / 2, warningWidth, warningHeight, configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);
            SDL_DrawText(RENDERER, ROBOTO_30, (windowX - warningWidth) / 2 + 15, (windowY - warningHeight) / 2 + 15, textColor, "This file is not yet fully supported, and may");
            SDL_DrawText(RENDERER, ROBOTO_30, (windowX - warningWidth) / 2 + 15, (windowY - warningHeight) / 2 + 50, textColor, "cause a system, or app crash.");
            SDL_DrawText(RENDERER, ROBOTO_20, (windowX - warningWidth) / 2 + warningWidth - 250, (windowY - warningHeight) / 2 + warningHeight - 30, textColor, "\"A\" - Read");
            SDL_DrawText(RENDERER, ROBOTO_20, (windowX - warningWidth) / 2 + warningWidth - 125, (windowY - warningHeight) / 2 + warningHeight - 30, textColor, "\"B\" - Cancel.");
        }

        SDL_RenderPresent(RENDERER);
    }
}
