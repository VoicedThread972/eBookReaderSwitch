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
#include "../book/BookReader.hpp"

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

static string strip_ext(const string & filename) {
	size_t dot = filename.find_last_of('.');
	return (dot == string::npos) ? filename : filename.substr(0, dot);
}

// Natural ("human") ordering: compares digit runs by value so that
// something2 sorts before something10. Case-insensitive elsewhere.
static bool natural_less(const string & a, const string & b) {
	size_t i = 0, j = 0;
	while (i < a.size() && j < b.size()) {
		unsigned char ca = a[i], cb = b[j];
		if (isdigit(ca) && isdigit(cb)) {
			size_t si = i, sj = j;
			while (i < a.size() && isdigit((unsigned char) a[i])) ++i;
			while (j < b.size() && isdigit((unsigned char) b[j])) ++j;
			// Trim leading zeros before comparing magnitudes.
			size_t as = si; while (as < i - 1 && a[as] == '0') ++as;
			size_t bs = sj; while (bs < j - 1 && b[bs] == '0') ++bs;
			size_t alen = i - as, blen = j - bs;
			if (alen != blen) return alen < blen;
			int cmp = a.compare(as, alen, b, bs, blen);
			if (cmp != 0) return cmp < 0;
		} else {
			char la = (char) tolower(ca), lb = (char) tolower(cb);
			if (la != lb) return la < lb;
			++i; ++j;
		}
	}
	return (a.size() - i) < (b.size() - j);
}

// Shorten text with an ellipsis so it fits within maxw pixels.
static string fit_text(TTF_Font *font, const string & text, int maxw) {
	int w = 0;
	TTF_SizeText(font, text.c_str(), &w, NULL);
	if (w <= maxw)
		return text;

	string out = text;
	while (!out.empty()) {
		out.pop_back();
		string candidate = out + "...";
		TTF_SizeText(font, candidate.c_str(), &w, NULL);
		if (w <= maxw)
			return candidate;
	}
	return "...";
}

// Filled rounded rectangle built from a cross of rects and four corner circles.
static void draw_round_rect(int x, int y, int w, int h, int r, SDL_Color colour) {
	if (r * 2 > h) r = h / 2;
	if (r * 2 > w) r = w / 2;
	SDL_DrawRect(RENDERER, x + r, y, w - 2 * r, h, colour);
	SDL_DrawRect(RENDERER, x, y + r, w, h - 2 * r, colour);
	SDL_DrawCircle(RENDERER, x + r, y + r, r, colour);
	SDL_DrawCircle(RENDERER, x + w - r - 1, y + r, r, colour);
	SDL_DrawCircle(RENDERER, x + r, y + h - r - 1, r, colour);
	SDL_DrawCircle(RENDERER, x + w - r - 1, y + h - r - 1, r, colour);
}

extern TTF_Font *ROBOTO_35, *ROBOTO_30, *ROBOTO_25, *ROBOTO_20, *ROBOTO_15;

// ── Layout constants ──────────────────────────────────────────────────────
const int LIST_TOP    = 96;
const int LIST_BOTTOM = 662;
const int LIST_LEFT   = 24;
const int LIST_RIGHT  = 1256;
const int ENTRY_H     = 88;
const int ENTRY_GAP   = 8;
const int ENTRY_STRIDE = ENTRY_H + ENTRY_GAP;
const int VIEW_H      = LIST_BOTTOM - LIST_TOP;
const int TOUCH_SLOP  = 16;

float scroll_y = 0.f;

// Touch scroll / tap state.
bool  touch_active = false;
bool  touch_moved  = false;
int   touch_start_y = 0, touch_last_y = 0;
float touch_scroll_start = 0.f;
Uint32 touch_start_ms = 0;

// Analog-stick navigation cooldown.
Uint32 last_stick_nav = 0;

// Color scheme (dark/light mode).
SDL_Color textColor = configDarkMode ? WHITE : BLACK;
SDL_Color backColor = configDarkMode ? BACK_BLACK : BACK_WHITE;
SDL_Color accent    = configDarkMode ? TITLE_COLOUR_DARK : TITLE_COLOUR;
SDL_Color subtle    = configDarkMode ? TEXT_MIN_COLOUR_DARK : TEXT_MIN_COLOUR_LIGHT;
SDL_Color cardColor = configDarkMode ? SDL_MakeColour(45, 45, 48, 255)  : SDL_MakeColour(230, 230, 233, 255);
SDL_Color cardSel   = configDarkMode ? SDL_MakeColour(58, 66, 76, 255)  : SDL_MakeColour(208, 224, 246, 255);
SDL_Color trackColor = configDarkMode ? SDL_MakeColour(70, 70, 74, 255) : SDL_MakeColour(198, 198, 202, 255);
SDL_Color doneColor = SDL_MakeColour(76, 175, 80, 255);

// All of these are handled natively by mupdf's document handlers, so there
// is nothing to warn about.
list<string> allowedExtentions = {".pdf", ".epub", ".cbz", ".xps", ".oxps", ".fb2"};

static void Menu_StartChoosingAtPath(string path) {
    int choosenIndex = 0;
    bool readingBook = false;

    // string path = "/switch/eBookReader/books";

    int windowX, windowY;
    SDL_GetWindowSize(WINDOW, &windowX, &windowY);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);

    PadState pad;
    padInitializeDefault(&pad);

    hidInitializeTouchScreen();

    while(appletMainLoop()) {
        if (readingBook) {
            break;
        }

        // Rebuild the book list every frame so files added over USB/MTP while
        // the app is open are picked up, and so a missing directory or files
        // without an extension can never crash the iterator.
        vector<string> books;
        vector<string> dirs = {".."};
        vector<string> allEntries;
        if (fs::exists(path) && fs::is_directory(path)) {
            for (const auto & entry : fs::directory_iterator(path)) {
                if (!entry.is_regular_file() && !entry.is_directory()) {
                    continue;
                }

                string filename = entry.path().filename().string();
                if (entry.is_directory()) {
                    dirs.push_back(filename);
                } else if (contains(allowedExtentions, lower_ext(filename))) {
                    books.push_back(filename);
                }
            }
            sort(books.begin(), books.end(), natural_less);
            if (dirs.size() > 1) {
                sort(dirs.begin() + 1, dirs.end(), natural_less); // keep ".." first
            }
            allEntries.insert(allEntries.end(), dirs.begin(), dirs.end());
            allEntries.insert(allEntries.end(), books.begin(), books.end());
        }
        int amountOfBooks = (int) books.size();
        int amountOfDirs = (int) dirs.size();
        int amountOfEntries = (int) allEntries.size();

        // Keep the selection within range even if the file count changed.
        if (amountOfEntries == 0) {
            choosenIndex = 0;
        } else {
            choosenIndex = std::min(std::max(0, choosenIndex), amountOfEntries - 1);
        }

        SDL_ClearScreen(RENDERER, backColor);
        SDL_RenderClear(RENDERER);

        padUpdate(&pad);

        u64 kDown = padGetButtonsDown(&pad);
        u64 kUp = padGetButtonsUp(&pad);

        Uint32 now = SDL_GetTicks();

        if (kDown & HidNpadButton_Plus) {
            break;
        }

        if (kDown & HidNpadButton_B) {
            break;
        }

        // ── D-pad selection ───────────────────────────────────────────────────
        if ((kDown & HidNpadButton_Up) && amountOfEntries > 0) {
            choosenIndex = (choosenIndex == 0) ? amountOfEntries - 1 : choosenIndex - 1;
        }
        if ((kDown & HidNpadButton_Down) && amountOfEntries > 0) {
            choosenIndex = (choosenIndex == amountOfEntries - 1) ? 0 : choosenIndex + 1;
        }

        // ── Analog-stick selection (both sticks), with a repeat cooldown ──────
        if (amountOfEntries > 0 && now - last_stick_nav > 150) {
            HidAnalogStickState ls = padGetStickPos(&pad, 0);
            HidAnalogStickState rs = padGetStickPos(&pad, 1);
            int sy = (abs(ls.y) > abs(rs.y)) ? ls.y : rs.y;
            if (sy > 12000) {
                choosenIndex = (choosenIndex == 0) ? amountOfEntries - 1 : choosenIndex - 1;
                last_stick_nav = now;
            } else if (sy < -12000) {
                choosenIndex = (choosenIndex == amountOfEntries - 1) ? 0 : choosenIndex + 1;
                last_stick_nav = now;
            }
        }

        int openIndex = -1;
        if ((kDown & HidNpadButton_A) && amountOfEntries > 0) {
            openIndex = choosenIndex;
        }

        // ── Touch: swipe to scroll, tap to open ───────────────────────────────
        HidTouchScreenState state = {0};
        hidGetTouchScreenStates(&state, 1);

        if (state.count > 0) {
            HidTouchState t = state.touches[0];
            if (!touch_active) {
                touch_active = true;
                touch_moved  = false;
                touch_start_y = touch_last_y = t.y;
                touch_scroll_start = scroll_y;
                touch_start_ms = now;
            } else {
                if (abs((int)t.y - touch_start_y) > TOUCH_SLOP) {
                    touch_moved = true;
                }
                scroll_y = touch_scroll_start - (float)((int)t.y - touch_start_y);
                touch_last_y = t.y;
            }
        } else if (touch_active) {
            touch_active = false;
            Uint32 duration = now - touch_start_ms;
            if (!touch_moved && duration < 500 && amountOfEntries > 0) {
                int ty = touch_last_y;
                if (ty >= LIST_TOP && ty <= LIST_BOTTOM) {
                    int rel = (int)(scroll_y + (ty - LIST_TOP));
                    if (rel >= 0) {
                        int idx = rel / ENTRY_STRIDE;
                        int within = rel % ENTRY_STRIDE;
                        if (idx >= 0 && idx < amountOfEntries && within < ENTRY_H) {
                            choosenIndex = idx;
                            openIndex = idx;
                        }
                    }
                }
            }
        }

        if (kUp & HidNpadButton_Minus) {
            configDarkMode = !configDarkMode;
        }

        // ── Scrolling maths ───────────────────────────────────────────────────
        int content_h = amountOfEntries * ENTRY_STRIDE;
        float max_scroll = (float) std::max(0, content_h - VIEW_H);

        // Keep the selected entry visible when navigating with buttons/sticks.
        if (amountOfEntries > 0 && !touch_active) {
            int sel_top = choosenIndex * ENTRY_STRIDE;
            int sel_bottom = sel_top + ENTRY_H;
            if (sel_top < scroll_y) scroll_y = (float) sel_top;
            else if (sel_bottom > scroll_y + VIEW_H) scroll_y = (float)(sel_bottom - VIEW_H);
        }
        scroll_y = std::min(std::max(0.f, scroll_y), max_scroll);

        // ── Header ────────────────────────────────────────────────────────────
        SDL_DrawRect(RENDERER, 0, 0, 1280, 74, configDarkMode ? SDL_MakeColour(38, 38, 41, 255) : SDL_MakeColour(214, 214, 218, 255));
        SDL_DrawRect(RENDERER, 0, 74, 1280, 3, accent);
        SDL_DrawText(RENDERER, ROBOTO_35, LIST_LEFT, 16, accent, "Library");
        {
            char count_txt[48];
            snprintf(count_txt, sizeof(count_txt), "%d book%s", amountOfEntries, amountOfEntries == 1 ? "" : "s");
            int cw = 0;
            TTF_SizeText(ROBOTO_20, count_txt, &cw, NULL);
            SDL_DrawText(RENDERER, ROBOTO_20, LIST_RIGHT - cw, 30, subtle, count_txt);
        }

        if (amountOfEntries == 0) {
            SDL_DrawText(RENDERER, ROBOTO_25, LIST_LEFT, LIST_TOP + 10, textColor, "No books found in /switch/eBookReader/books");
        }

        // ── Entry list (clipped to the list viewport) ─────────────────────────
        SDL_Rect clip = { LIST_LEFT - 8, LIST_TOP, (LIST_RIGHT - LIST_LEFT) + 16, VIEW_H };
        SDL_RenderSetClipRect(RENDERER, &clip);

        for (int i = 0; i < amountOfEntries; i++) {
            int y = LIST_TOP + i * ENTRY_STRIDE - (int) scroll_y;
            if (y + ENTRY_H < LIST_TOP || y > LIST_BOTTOM) {
                continue; // off-screen
            }

            bool selected = (choosenIndex == i);
            draw_round_rect(LIST_LEFT, y, LIST_RIGHT - LIST_LEFT, ENTRY_H, 14, selected ? cardSel : cardColor);
            if (selected) {
                SDL_DrawRect(RENDERER, LIST_LEFT, y + 14, 6, ENTRY_H - 28, accent);
            }

            bool isDir = i < amountOfDirs;
            string entryName = isDir ? dirs[i] : books[i - amountOfDirs];

            // Saved reading progress (books only).
            int cur = 0, total = 0;
            if (!isDir) {
                BookReader::GetSavedProgress((path + "/" + entryName).c_str(), &cur, &total);
            }

            int pill_x = LIST_LEFT + 22;
            int pill_w = 150;
            int pill_y = y + (ENTRY_H - 52) / 2;

            char prog_txt[32];
            float frac = 0.f;
            bool started = total > 0;
            if (isDir) {
                snprintf(prog_txt, sizeof(prog_txt), "%s", entryName == ".." ? "Up" : "Dir");
            } else {
                if (started) {
                    int shown = std::min(cur + 1, total);
                    snprintf(prog_txt, sizeof(prog_txt), "%d/%d", shown, total);
                    frac = (total > 1) ? (float) cur / (float)(total - 1) : 1.f;
                    frac = std::min(std::max(0.f, frac), 1.f);
                } else {
                    snprintf(prog_txt, sizeof(prog_txt), "New");
                }
            }

            SDL_Color pill_col = !started ? subtle : (frac >= 0.999f ? doneColor : accent);
            draw_round_rect(pill_x, pill_y, pill_w, 34, 12, pill_col);
            {
                int tw = 0, th = 0;
                TTF_SizeText(ROBOTO_20, prog_txt, &tw, &th);
                SDL_DrawText(RENDERER, ROBOTO_20, pill_x + (pill_w - tw) / 2, pill_y + (34 - th) / 2 - 1, WHITE, prog_txt);
            }

            // Progress bar under the pill.
            int bar_x = pill_x;
            int bar_w = pill_w;
            int bar_y = pill_y + 40;
            draw_round_rect(bar_x, bar_y, bar_w, 6, 3, trackColor);
            if (!isDir && started && frac > 0.f) {
                int fill_w = std::max(6, (int)(bar_w * frac));
                draw_round_rect(bar_x, bar_y, fill_w, 6, 3, frac >= 0.999f ? doneColor : accent);
            }

            // Title + type tag.
            int title_x = pill_x + pill_w + 26;
            string tag;
            if (isDir) {
                tag = (entryName == "..") ? "UP" : "FOLDER";
            } else {
                string ext = lower_ext(entryName);
                tag = ext.empty() ? "" : ext.substr(1);
                transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c){ return toupper(c); });
            }

            int tag_w = 0;
            if (!tag.empty()) TTF_SizeText(ROBOTO_15, tag.c_str(), &tag_w, NULL);
            int tag_box_w = tag.empty() ? 0 : tag_w + 20;

            int title_max = (LIST_RIGHT - 20) - title_x - (tag_box_w ? tag_box_w + 16 : 0);
            string title = fit_text(ROBOTO_30, isDir ? entryName : strip_ext(entryName), title_max);
            SDL_DrawText(RENDERER, ROBOTO_30, title_x, y + (ENTRY_H - 34) / 2, textColor, title.c_str());

            if (!tag.empty()) {
                int tag_x = LIST_RIGHT - 16 - tag_box_w;
                int tag_y = y + (ENTRY_H - 26) / 2;
                draw_round_rect(tag_x, tag_y, tag_box_w, 26, 8, trackColor);
                SDL_DrawText(RENDERER, ROBOTO_15, tag_x + 10, tag_y + 4, textColor, tag.c_str());
            }
        }

        SDL_RenderSetClipRect(RENDERER, NULL);

        // ── Scrollbar ─────────────────────────────────────────────────────────
        if (content_h > VIEW_H) {
            int track_x = LIST_RIGHT + 6;
            draw_round_rect(track_x, LIST_TOP, 4, VIEW_H, 2, trackColor);
            int thumb_h = std::max(40, (int)((float) VIEW_H * VIEW_H / content_h));
            int thumb_y = LIST_TOP + (int)((VIEW_H - thumb_h) * (scroll_y / max_scroll));
            draw_round_rect(track_x, thumb_y, 4, thumb_h, 2, accent);
        }

        // ── Open the chosen/tapped book or folder ───────────────────────────────────────
        if (openIndex >= 0 && openIndex < amountOfDirs) {
            if (dirs[openIndex] == "..")
            {
                // Go up one directory.
                fs::path p(path);
                if (p.has_parent_path())
                {
                    path = p.parent_path().string();
                }
            }
            else
            {
                path = (fs::path(path) / dirs[openIndex]).string();
            }

            cout << "Opening directory: " << path << endl;
            choosenIndex = 0;
            scroll_y = 0.f;
            continue;
        }
        else if (openIndex >= 0 && openIndex < amountOfEntries) {
            string book = path + "/" + books[openIndex - amountOfDirs];
            cout << "Opening book: " << book << endl;
            Menu_OpenBook((char*) book.c_str());
            readingBook = true;
            break;
        }

        // ── Bottom prompts ────────────────────────────────────────────────────
        int openWidth = 0;
        TTF_SizeText(ROBOTO_20, "Open", &openWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_a, ROBOTO_20, textColor, "Open", LIST_LEFT + 40, windowY - 10, 35, 35, 5, 0);

        int exitWidth = 0;
        TTF_SizeText(ROBOTO_20, "Exit", &exitWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_b, ROBOTO_20, textColor, "Exit", windowX - exitWidth - 50, windowY - 10, 35, 35, 5, 0);

        int themeWidth = 0;
        TTF_SizeText(ROBOTO_20, "Switch Theme", &themeWidth, NULL);
        SDL_DrawButtonPrompt(RENDERER, button_minus, ROBOTO_20, textColor, "Switch Theme", windowX - themeWidth - 50, windowY - 40, 35, 35, 5, 0);

        SDL_RenderPresent(RENDERER);
    }
}

void Menu_StartChoosing() {
    Menu_StartChoosingAtPath("/switch/eBookReader/books");
}
