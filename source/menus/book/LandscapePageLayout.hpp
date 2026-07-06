#ifndef EBOOK_READER_LANDSCAPE_PAGE_LAYOUT_HPP
#define EBOOK_READER_LANDSCAPE_PAGE_LAYOUT_HPP

#include "PageLayout.hpp"

class LandscapePageLayout: public PageLayout
{
    public:
        LandscapePageLayout(fz_document *doc, int current_page, int angle = 90);
    
        void reset();
        void draw_page();
        SDL_Rect page_rect_to_screen(const fz_rect &r, int page_index) const override;
        bool     screen_to_page_coords(int sx, int sy, int page_index,
                                       float *px, float *py) const override;
        void     pan_by_screen(float dx, float dy) override;
    
    protected:
        void move_page(float x, float y);
    
        int _angle = 90;
};

#endif
