#ifndef EBOOK_READER_PAGE_LAYOUT_HPP
#define EBOOK_READER_PAGE_LAYOUT_HPP

#include <mupdf/pdf.h>
#include <SDL2/SDL.h>

extern fz_context *ctx;

static inline void FreeTextureIfNeeded(SDL_Texture **texture)
{
    if (texture && *texture)
    {
        SDL_DestroyTexture(*texture);
        *texture = NULL;
    }
}

class PageLayout
{
    public:
        PageLayout(fz_document *doc, int current_page, bool spread_mode = true, int draw_angle = 0);
        virtual ~PageLayout();
    
        int current_page()
        {
            return _current_page;
        }
    
        virtual void previous_page(int n);
        virtual void next_page(int n);
        virtual void zoom_in();
        virtual void zoom_out();
        virtual void move_up();
        virtual void move_down();
        virtual void move_left();
        virtual void move_right();
        virtual void reset();
        virtual void draw_page();
        virtual char* info();

        // Page navigation.
        void go_to_page(int n);
        int  page_count()         const { return pages_count; }
        int  second_page_number() const;

        // Coordinate helpers used for link hit-testing and highlight drawing.
        virtual SDL_Rect page_rect_to_screen(const fz_rect &r, int page_index) const;
        virtual bool     screen_to_page_coords(int sx, int sy, int page_index,
                                               float *px, float *py) const;

        // Zoom / pan helpers (mouse-centred zoom, pinch, touch pan).
        float current_zoom() const { return zoom; }
        bool  is_zoomed()    const { return zoom > min_zoom * 1.01f; }
        void  set_zoom_at(float value, float ax, float ay);
        void  set_zoom_ratio_at(float ratio, float ax, float ay) { set_zoom_at(zoom * ratio, ax, ay); }
        void  zoom_in_at(float ax, float ay)  { set_zoom_at(zoom + 0.03f, ax, ay); }
        void  zoom_out_at(float ax, float ay) { set_zoom_at(zoom - 0.03f, ax, ay); }
        virtual void pan_by_screen(float dx, float dy);
        bool  at_screen_edge(float dx, float dy);

    protected:
        virtual void render_page_to_texture(int num, bool reset_zoom);
        virtual void set_zoom(float value);
        virtual void move_page(float x, float y);
    
        fz_document *doc = NULL;
        pdf_document *pdf = NULL;
        const int pages_count = 0;
    
        int _current_page = 0;
        fz_rect page_bounds = fz_empty_rect;
        fz_point page_center = fz_make_point(0, 0);
        float min_zoom = 1, max_zoom = 1, zoom = 1;
        bool spread_mode = false;
    
        SDL_Rect viewport;
        SDL_Texture *page_texture = NULL;
        fz_rect second_page_bounds = fz_empty_rect;
        SDL_Texture *second_page_texture = NULL;
        int _draw_angle = 0;

        // Defer expensive re-rasterization while zoom is actively changing.
        float texture_zoom = 1.0f;
        Uint32 last_raster_ms = 0;
        Uint32 last_zoom_input_ms = 0;
        bool raster_pending = false;
};

#endif
