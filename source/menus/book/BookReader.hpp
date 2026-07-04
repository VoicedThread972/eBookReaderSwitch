#ifndef EBOOK_READER_BOOK_READER_HPP
#define EBOOK_READER_BOOK_READER_HPP

#include <mupdf/pdf.h>
#include <string>
#include <vector>
#include "PageLayout.hpp"
#include <switch.h>
struct SDL_Texture;

typedef enum {
    BookPageLayoutPortrait,
    BookPageLayoutLandscape
} BookPageLayout;

// Per-link data cached for the currently visible page(s).
struct LinkInfo {
    std::string uri;
    fz_rect     page_rect;   // link bounds in page-space (points)
    int         page_num;    // document page number (0-based)
    int         page_index;  // 0 = main page, 1 = second page (spread only)
    SDL_Rect    screen_rect; // derived from page_rect; updated on zoom/pan
};

// State passed to BookReader::draw() for optional overlays.
struct ReaderOverlay {
    bool show_link_rects = false; // draw link highlight rectangles
    int  focused_link    = -1;    // index into page_links(), -1 = none
    bool cursor_visible  = false;
    int  cursor_x        = 640;
    int  cursor_y        = 360;
};

class BookReader {
    public:
        BookReader(const char *path, int *result);
        ~BookReader();

        bool permStatusBar = false;

        void previous_page(int n);
        void next_page(int n);
        void zoom_in();
        void zoom_out();
        void move_page_up();
        void move_page_down();
        void move_page_left();
        void move_page_right();
        void reset_page();
        void switch_page_layout();
        void draw(bool drawHelp, const ReaderOverlay &overlay);

        BookPageLayout currentPageLayout() const {
            return (_rotation == 0) ? BookPageLayoutPortrait : BookPageLayoutLandscape;
        }

        int rotation() const { return _rotation; }

        // Return the URI whose screen rect contains (sx,sy), or empty string.
        std::string hit_link(int sx, int sy) const;

        // Navigate to an internal link destination; external URLs are ignored.
        void follow_link(const char *uri);

        // Force a full link-list reload (e.g. after an external layout change).
        void reload_links();

        const std::vector<LinkInfo>& page_links() const { return cached_links_; }

    private:
        void show_status_bar();
        void apply_rotation(int rotation, int current_page);

        void load_page_links();           // full mupdf query for current page(s)
        void recompute_link_screen_rects(); // cheap: recompute screen_rect after pan/zoom

        fz_document *doc = NULL;
        int status_bar_visible_counter = 0;

        int        _rotation = 0;
        PageLayout *layout   = NULL;

        std::string           book_name;
        std::vector<LinkInfo> cached_links_;
};

#endif
