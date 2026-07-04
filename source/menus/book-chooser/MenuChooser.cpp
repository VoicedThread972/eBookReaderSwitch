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
#include <bits/stdc++.h>

#include <SDL2/SDL_image.h>

using namespace std;
namespace fs = filesystem;

template <typename T> bool contains(list<T> & listOfElements, const T & element) {
	auto it = find(listOfElements.begin(), listOfElements.end(), element);
	return it != listOfElements.end();
}

// Returns the lower-cased file extension (including the leading dot), or an
// empty string when the file name has no extension. Safe for names without a
// dot (find_last_of returns npos, which would otherwise make substr throw).
static string lower_ext(const string & filename) {
	size_t dot = filename.find_last_of('.');
	if (dot == string::npos)
		return "";

	string ext = filename.substr(dot);
	transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return tolower(c); });
	return ext;
}

extern TTF_Font *ROBOTO_35, *ROBOTO_25, *ROBOTO_15;

void Menu_StartChoosing() {
    int choosenIndex = 0;
    bool readingBook = false;
    // All of these are handled natively by mupdf's document handlers, so there
    // is nothing to warn about.
    list<string> allowedExtentions = {".pdf", ".epub", ".cbz", ".xps", ".oxps", ".fb2"};

    string path = "/switch/eBookReader/books";

    int windowX, windowY;
    SDL_GetWindowSize(WINDOW, &windowX, &windowY);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    while(appletMainLoop()) {
        if (readingBook) {
            break;
        }

        // Rebuild the book list every frame so files added over USB/MTP while
        // the app is open are picked up, and so a missing directory or files
        // without an extension can never crash the iterator.
        vector<string> books;
        if (fs::exists(path) && fs::is_directory(path)) {
            for (const auto & entry : fs::directory_iterator(path)) {
                if (!entry.is_regular_file()) {
                    continue;
                }

                string filename = entry.path().filename().string();
                if (contains(allowedExtentions, lower_ext(filename))) {
                    books.push_back(filename);
                }
            }
            sort(books.begin(), books.end());
        }
        int amountOfFiles = (int) books.size();

        // Keep the selection within range even if the file count changed.
        if (amountOfFiles == 0) {
            choosenIndex = 0;
        } else {
            choosenIndex = std::min(std::max(0, choosenIndex), amountOfFiles - 1);
        }

        SDL_Color textColor = configDarkMode ? WHITE : BLACK;
        SDL_Color backColor = configDarkMode ? BACK_BLACK : BACK_WHITE;

        SDL_ClearScreen(RENDERER, backColor);
		SDL_RenderClear(RENDERER);

	padUpdate(&pad);

	u64 kDown = padGetButtonsDown(&pad);
	u64 kUp = padGetButtonsUp(&pad);

        if (kDown & HidNpadButton_Plus) {
            break;
        }

        if (kDown & HidNpadButton_B) {
            break;
        }

        if ((kDown & HidNpadButton_A) && amountOfFiles > 0) {
            string book = path + "/" + books[choosenIndex];
            cout << "Opening book: " << book << endl;

            Menu_OpenBook((char*) book.c_str());
            readingBook = true;
            break;
        }

        if ((kDown & HidNpadButton_Up || kDown & HidNpadButton_StickRUp) && amountOfFiles > 0) {
            choosenIndex = (choosenIndex == 0) ? amountOfFiles - 1 : choosenIndex - 1;
        }

        if ((kDown & HidNpadButton_Down || kDown & HidNpadButton_StickRDown) && amountOfFiles > 0) {
            choosenIndex = (choosenIndex == amountOfFiles - 1) ? 0 : choosenIndex + 1;
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

        if (amountOfFiles == 0) {
            SDL_DrawText(RENDERER, ROBOTO_25, 70, 20, textColor, "No books found in /switch/eBookReader/books");
        }

        for (int i = 0; i < amountOfFiles; i++) {
            if (choosenIndex == i) {
                SDL_DrawRect(RENDERER, 15, 15 + (40 * i), 1265, 40, configDarkMode ? SELECTOR_COLOUR_DARK : SELECTOR_COLOUR_LIGHT);
            }

            SDL_DrawText(RENDERER, ROBOTO_25, 70, 20 + (40 * i), textColor, books[i].c_str());
        }

        SDL_RenderPresent(RENDERER);
    }
}
