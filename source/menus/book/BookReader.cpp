#include "BookReader.hpp"
#include "PageLayout.hpp"
#include "LandscapePageLayout.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <libconfig.h>

extern "C"  {
    #include "SDL_helper.h"
    #include "status_bar.h"
    #include "config.h"
    #include "textures.h"
    #include "common.h"
}

fz_context *ctx = NULL;
int windowX, windowY;
config_t *config = NULL;
const char* configFile = "/switch/eBookReader/saved_pages.cfg";

static int load_last_page(const char *book_name)  {
    if (!config) {
        config = (config_t *)malloc(sizeof(config_t));
        config_init(config);
        config_read_file(config, configFile);
    }
    
    config_setting_t *setting = config_setting_get_member(config_root_setting(config), book_name);
    
    if (setting) {
        return config_setting_get_int(setting);
    }

    return 0;
}

static void save_last_page(const char *book_name, int current_page) {
    config_setting_t *setting = config_setting_get_member(config_root_setting(config), book_name);
    
    if (!setting) {
        setting = config_setting_add(config_root_setting(config), book_name, CONFIG_TYPE_INT);
    }
    
    if (setting) {
        config_setting_set_int(setting, current_page);
        config_write_file(config, configFile);
    }
}

BookReader::BookReader(const char *path, int* result) {
    if (ctx == NULL) {
        ctx = fz_new_context(NULL, NULL, FZ_STORE_UNLIMITED);
        fz_register_document_handlers(ctx);
    }

    SDL_GetWindowSize(WINDOW, &windowX, &windowY);
    
    book_name = std::string(path).substr(std::string(path).find_last_of("/\\") + 1);
    
    std::string invalid_chars = " :/?#[]@!$&'()*+,;=.";
    for (char& c: invalid_chars) {
        book_name.erase(std::remove(book_name.begin(), book_name.end(), c), book_name.end());
    }
    
    fz_try(ctx)	{
        std::cout << "fz_open_document" << std::endl;
        doc = fz_open_document(ctx, path);

        if (!doc)
        {
            std::cout << "Error opening file!" << std::endl;
            *result = -1;
            return;
        }
        
        int current_page = load_last_page(book_name.c_str());
        //int current_page = 0;

        std::cout << "current_page = " << current_page << std::endl;

        apply_rotation(0, current_page);

        if (current_page > 0) {
            show_status_bar();
        }
    }
    fz_catch(ctx){
        std::cout << "fz_catch reached, closing gracefully" << std::endl;
        *result = -2;
        return;
    }
}

BookReader::~BookReader() {
    fz_drop_document(ctx, doc);
    
    delete layout;
}

void BookReader::previous_page(int n) {
    layout->previous_page(n);
    show_status_bar();
    save_last_page(book_name.c_str(), layout->current_page());
    recompute_link_screen_rects();
}

void BookReader::next_page(int n) {
    layout->next_page(n);
    show_status_bar();
    save_last_page(book_name.c_str(), layout->current_page());
    recompute_link_screen_rects();
}

void BookReader::zoom_in() {
    layout->zoom_in();
    show_status_bar();
}

void BookReader::zoom_out() {
    layout->zoom_out();
    show_status_bar();
}

void BookReader::move_page_up() {
    layout->move_up();
    recompute_link_screen_rects();
}

void BookReader::move_page_down() {
    layout->move_down();
    recompute_link_screen_rects();
}

void BookReader::move_page_left() {
    layout->move_left();
    recompute_link_screen_rects();
}

void BookReader::move_page_right() {
    layout->move_right();
    recompute_link_screen_rects();
}

void BookReader::reset_page() {
    layout->reset();
    show_status_bar();
    recompute_link_screen_rects();
}

void BookReader::switch_page_layout() {
    apply_rotation(_rotation + 90, 0);
}

void BookReader::draw(bool drawHelp, const ReaderOverlay &overlay) {
    if (configDarkMode == true) {
        SDL_ClearScreen(RENDERER, BLACK);
    } else {
        SDL_ClearScreen(RENDERER, WHITE);
    }

    SDL_RenderClear(RENDERER);
    
    layout->draw_page();

    // Draw link highlight rectangles.
    if (overlay.show_link_rects && !cached_links_.empty()) {
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_BLEND);
        for (int i = 0; i < (int)cached_links_.size(); i++) {
            const SDL_Rect &r = cached_links_[i].screen_rect;
            if (r.w <= 0 || r.h <= 0) continue;
            bool focused = (overlay.focused_link == i);
            SDL_SetRenderDrawColor(RENDERER,
                focused ? 100 : 50,
                focused ? 180 : 120,
                255,
                focused ? 140 : 55);
            SDL_RenderFillRect(RENDERER, &r);
            SDL_SetRenderDrawColor(RENDERER, 80, 140, 255, focused ? 255 : 180);
            SDL_RenderDrawRect(RENDERER, &r);
        }
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_NONE);
    }
    
    if (drawHelp) { // Help menu
        int helpWidth = 680;
        int helpHeight = 365;
        helpHeight -= 38; // Removed due to removing the skip forward page button prompt.

        if (!configDarkMode) { // Display a dimmed background if on light mode
            SDL_DrawRect(RENDERER, 0, 0, 1280, 720, SDL_MakeColour(50, 50, 50, 150));
        }

        SDL_DrawRect(RENDERER, (windowX - helpWidth) / 2, (windowY - helpHeight) / 2, helpWidth, helpHeight, configDarkMode ? HINT_COLOUR_DARK : HINT_COLOUR_LIGHT);

        int textX = (windowX - helpWidth) / 2 + 20;
        int textY = (windowY - helpHeight) / 2 + 87;
        SDL_Color textColor = configDarkMode ? WHITE : BLACK;
        SDL_DrawText(RENDERER, ROBOTO_30, textX, (windowY - helpHeight) / 2 + 10, textColor, "Help Menu:");

        SDL_DrawButtonPrompt(RENDERER, button_b,               ROBOTO_25, textColor, "Stop reading / Close help menu.", textX, textY,          35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_minus,           ROBOTO_25, textColor, "Switch to dark/light theme.",     textX, textY + 38,     35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, right_stick_up_down,    ROBOTO_25, textColor, "Zoom in/out.",                    textX, textY + 38 * 2, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, left_stick_up_down,     ROBOTO_25, textColor, "Page up/down.",                   textX, textY + 38 * 3, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_y,               ROBOTO_25, textColor, "Rotate page.",                    textX, textY + 38 * 4, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_x,               ROBOTO_25, textColor, "Keep status bar on.",             textX, textY + 38 * 5, 35, 35, 5, 0);
        SDL_DrawButtonPrompt(RENDERER, button_dpad_left_right, ROBOTO_25, textColor, "Next/previous page.",             textX, textY + 38 * 6, 35, 35, 5, 0);
        //SDL_DrawButtonPrompt(RENDERER, button_dpad_up_down,    ROBOTO_25, textColor, "Skip forward/backward 10 pages.", textX, textY + 38 * 7, 35, 35, 5, 0);
    }

    if (permStatusBar || --status_bar_visible_counter > 0)  {
        char *title = layout->info();
        
        int title_width = 0, title_height = 0;
        TTF_SizeText(ROBOTO_15, title, &title_width, &title_height);
        
        SDL_Color color = configDarkMode ? STATUS_BAR_DARK : STATUS_BAR_LIGHT;
        
        if (currentPageLayout() == BookPageLayoutPortrait) {
            SDL_DrawRect(RENDERER, 0, 0, 1280, 45, SDL_MakeColour(color.r, color.g, color.b , 180));
            SDL_DrawText(RENDERER, ROBOTO_25, (1280 - title_width) / 2, (40 - title_height) / 2, WHITE, title);
            
            StatusBar_DisplayTime(false);
        } else if (currentPageLayout() == BookPageLayoutLandscape) {
            SDL_DrawRect(RENDERER, 1280 - 45, 0, 45, 720, SDL_MakeColour(color.r, color.g, color.b , 180));
            int x = (1280 - title_width) - ((40 - title_height) / 2);
            int y = (720 - title_height) / 2;
            SDL_DrawRotatedText(RENDERER, ROBOTO_25, (double) 90, x, y, WHITE, title);

            StatusBar_DisplayTime(true);
        }
    }

    // Draw virtual cursor crosshair.
    if (overlay.cursor_visible) {
        int cx = overlay.cursor_x, cy = overlay.cursor_y;
        bool over_link = !hit_link(cx, cy).empty();
        SDL_SetRenderDrawBlendMode(RENDERER, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(RENDERER,
            over_link ? 50  : 255,
            over_link ? 220 : 255,
            over_link ? 50  : 0,
            255);
        SDL_RenderDrawLine(RENDERER, cx - 15, cy, cx + 15, cy);
        SDL_RenderDrawLine(RENDERER, cx, cy - 15, cx, cy + 15);
        SDL_Rect dot = {cx - 2, cy - 2, 5, 5};
        SDL_RenderFillRect(RENDERER, &dot);
    }
    
    
    SDL_RenderPresent(RENDERER);
}

void BookReader::show_status_bar() {
    status_bar_visible_counter = 200;
}

void BookReader::apply_rotation(int rotation, int current_page) {
    if (layout) {
        current_page = layout->current_page();
        delete layout;
        layout = NULL;
    }
    
    _rotation = ((rotation % 360) + 360) % 360;
    
    if (_rotation == 0) {
        // Upright: two-page spread (landscape reading on the handheld).
        layout = new PageLayout(doc, current_page);
    } else {
        // 90/180/270: single page rotated clockwise by that many degrees.
        layout = new LandscapePageLayout(doc, current_page, _rotation);
    }

    load_page_links();
}

// ── Link implementation ───────────────────────────────────────────────────────

void BookReader::load_page_links() {
    cached_links_.clear();
    if (!layout || !doc) return;

    auto process = [&](int page_num, int page_index) {
        if (page_num < 0) return;
        fz_page *page = nullptr;
        fz_try(ctx) { page = fz_load_page(ctx, doc, page_num); }
        fz_catch(ctx) { return; }

        fz_link *links = nullptr;
        fz_try(ctx) { links = fz_load_links(ctx, page); }
        fz_catch(ctx) { fz_drop_page(ctx, page); return; }

        for (fz_link *l = links; l; l = l->next) {
            if (!l->uri || !*l->uri) continue;
            LinkInfo li;
            li.uri        = l->uri;
            li.page_rect  = l->rect;
            li.page_num   = page_num;
            li.page_index = page_index;
            li.screen_rect = layout->page_rect_to_screen(l->rect, page_index);
            if (li.screen_rect.w > 0 && li.screen_rect.h > 0)
                cached_links_.push_back(std::move(li));
        }
        fz_drop_link(ctx, links);
        fz_drop_page(ctx, page);
    };

    process(layout->current_page(), 0);
    process(layout->second_page_number(), 1);
}

void BookReader::recompute_link_screen_rects() {
    if (!layout) return;
    for (auto &li : cached_links_)
        li.screen_rect = layout->page_rect_to_screen(li.page_rect, li.page_index);
}

std::string BookReader::hit_link(int sx, int sy) const {
    for (const auto &li : cached_links_) {
        const SDL_Rect &r = li.screen_rect;
        if (sx >= r.x && sx <= r.x + r.w && sy >= r.y && sy <= r.y + r.h)
            return li.uri;
    }
    return "";
}

void BookReader::follow_link(const char *uri) {
    if (!uri || !*uri || !layout || !doc) return;
    // Ignore external links — Switch has no browser.
    if (strncmp(uri, "http", 4) == 0 || strncmp(uri, "mailto:", 7) == 0) return;

    fz_try(ctx) {
        float xp = 0, yp = 0;
        fz_location loc = fz_resolve_link(ctx, doc, uri, &xp, &yp);
        int pn = fz_page_number_from_location(ctx, doc, loc);
        if (pn >= 0 && pn < fz_count_pages(ctx, doc)) {
            layout->go_to_page(pn);
            show_status_bar();
            save_last_page(book_name.c_str(), layout->current_page());
            load_page_links();
        }
    }
    fz_catch(ctx) { /* unresolvable link — silently ignore */ }
}

void BookReader::reload_links() {
    load_page_links();
}
