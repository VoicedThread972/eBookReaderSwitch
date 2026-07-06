#include "PageLayout.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <iostream>

extern "C" {
    #include "common.h"
    #include "config.h"
    #include "SDL_helper.h"
}

static const float PAGE_SPREAD_GUTTER = 24.0f;
static const float PAGE_SPREAD_MARGIN = 20.0f;

static float clamp_page_center(float center, float delta, float content_size, float viewport_size) {
    if (content_size <= viewport_size) {
        return viewport_size / 2;
    }

    return fmin(fmax(center + delta, viewport_size - content_size / 2), content_size / 2);
}

static SDL_Texture* render_page_texture(fz_page *page, float zoom) {
    fz_pixmap *pix = fz_new_pixmap_from_page_contents(ctx, page, fz_scale(zoom, zoom), fz_device_rgb(ctx), 0);
    SDL_Surface *image = SDL_CreateRGBSurfaceFrom(pix->samples, pix->w, pix->h, pix->n * 8, pix->w * pix->n, 0x000000FF, 0x0000FF00, 0x00FF0000, 0);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(RENDERER, image);

    SDL_FreeSurface(image);
    fz_drop_pixmap(ctx, pix);

    return texture;
}

PageLayout::PageLayout(fz_document *doc, int current_page, bool spread_mode, int draw_angle):doc(doc),pdf(pdf_specifics(ctx, doc)),pages_count(fz_count_pages(ctx, doc)),spread_mode(spread_mode),_draw_angle(draw_angle) {
    _current_page = std::min(std::max(0, current_page), pages_count - 1);
    
    SDL_RenderGetViewport(RENDERER, &viewport);
    render_page_to_texture(_current_page, false);
}

PageLayout::~PageLayout() {
    FreeTextureIfNeeded(&page_texture);
    FreeTextureIfNeeded(&second_page_texture);
}

void PageLayout::previous_page(int n) {
    // In spread mode each "page turn" advances one spread = 2 document pages.
    long long step = spread_mode ? (long long)n * 2 : n;
    long long target = (long long)_current_page - step;
    render_page_to_texture((int)std::max<long long>(0, target), false);
}

void PageLayout::next_page(int n) {
    long long step = spread_mode ? (long long)n * 2 : n;
    long long target = (long long)_current_page + step;
    render_page_to_texture((int)std::min<long long>(pages_count - 1, target), false);
}

void PageLayout::zoom_in() {
    set_zoom(zoom + 0.03);
};

void PageLayout::zoom_out() {
    set_zoom(zoom - 0.03);
};

void PageLayout::move_up() {
    move_page(0, 3);
};

void PageLayout::move_down() {
    move_page(0, -3);
};

void PageLayout::move_left() {
    move_page(-3, 0);
};

void PageLayout::move_right() {
    move_page(3, 0);
};

void PageLayout::reset() {
    page_center = fz_make_point(viewport.w / 2, viewport.h / 2);
    set_zoom(min_zoom);
};

void PageLayout::draw_page() {
    if (raster_pending) {
        Uint32 now = SDL_GetTicks();
        bool idle_after_input = (now - last_zoom_input_ms) > 70;
        bool drift_large = fabsf(zoom - texture_zoom) > 0.18f;
        bool cadence_ok = (now - last_raster_ms) > 45;
        if (idle_after_input || (drift_large && cadence_ok)) {
            render_page_to_texture(_current_page, false);
        }
    }

    if (spread_mode) {
        float w = page_bounds.x1 * zoom;
        float h = page_bounds.y1 * zoom;
        bool has_second_page = second_page_bounds.x1 > 0.f && second_page_bounds.y1 > 0.f;
        float w2 = has_second_page ? (second_page_bounds.x1 * zoom) : 0.f;
        float h2 = has_second_page ? (second_page_bounds.y1 * zoom) : 0.f;

        float total_width = w + (has_second_page ? PAGE_SPREAD_GUTTER + w2 : 0);
        float spread_left = page_center.x - total_width / 2;

        SDL_Rect rect;
        rect.x = spread_left;
        rect.y = page_center.y - h / 2;
        rect.w = w;
        rect.h = h;
        SDL_RenderCopyEx(RENDERER, page_texture, NULL, &rect, _draw_angle, NULL, SDL_FLIP_NONE);

        if (has_second_page) {
            SDL_Rect second_rect;
            second_rect.x = spread_left + w + PAGE_SPREAD_GUTTER;
            second_rect.y = page_center.y - h2 / 2;
            second_rect.w = w2;
            second_rect.h = h2;
            SDL_RenderCopyEx(RENDERER, second_page_texture, NULL, &second_rect, _draw_angle, NULL, SDL_FLIP_NONE);
        }

        return;
    }

    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;
    
    SDL_Rect rect;
    rect.x = page_center.x - w / 2;
    rect.y = page_center.y - h / 2;
    rect.w = w;
    rect.h = h;
    
    SDL_RenderCopyEx(RENDERER, page_texture, NULL, &rect, _draw_angle, NULL, SDL_FLIP_NONE);
}

char* PageLayout::info() {
    static char title[128];
    if (spread_mode && _current_page + 1 < pages_count) {
        sprintf(title, "%i-%i/%i, %.2f%%", _current_page + 1, _current_page + 2, pages_count, zoom * 100);
    } else {
        sprintf(title, "%i/%i, %.2f%%", _current_page + 1, pages_count, zoom * 100);
    }
    return title;
}

void PageLayout::render_page_to_texture(int num, bool reset_zoom) {
    FreeTextureIfNeeded(&page_texture);
    FreeTextureIfNeeded(&second_page_texture);
    
    _current_page = std::min(std::max(0, num), pages_count - 1);

    // In spread mode always start each spread on an even (0-based) page so that
    // odd page numbers (1-based) stay on the left and even ones on the right.
    if (spread_mode) {
        _current_page -= (_current_page % 2);
    }

    fz_page *page = fz_load_page(ctx, doc, _current_page);
    fz_rect bounds = fz_bound_page(ctx, page);
    fz_page *second_page = NULL;
    fz_rect second_bounds = fz_empty_rect;

    if (spread_mode && _current_page + 1 < pages_count) {
        second_page = fz_load_page(ctx, doc, _current_page + 1);
        second_bounds = fz_bound_page(ctx, second_page);
    }
    
    if (page_bounds.x1 != bounds.x1 || page_bounds.y1 != bounds.y1 ||
        second_page_bounds.x1 != second_bounds.x1 || second_page_bounds.y1 != second_bounds.y1 ||
        reset_zoom) {
        page_bounds = bounds;
        second_page_bounds = second_bounds;
        page_center = fz_make_point(viewport.w / 2, viewport.h / 2);

        float available_width = viewport.w;
        float available_height = viewport.h;
        float widest_page = bounds.x1;
        float tallest_page = bounds.y1;

        if (spread_mode) {
            available_width = (viewport.w - PAGE_SPREAD_GUTTER - PAGE_SPREAD_MARGIN * 2) / 2;
            available_height = viewport.h - PAGE_SPREAD_MARGIN * 2;

            if (second_page) {
                widest_page = fmax(widest_page, second_bounds.x1);
                tallest_page = fmax(tallest_page, second_bounds.y1);
            }
        }

        min_zoom = fmin(available_width / widest_page, available_height / tallest_page);
        max_zoom = fmax(viewport.w / bounds.x1, viewport.h / bounds.y1);
        if (max_zoom < min_zoom) {
            max_zoom = min_zoom;
        }
        zoom = min_zoom;
    }
    
    page_texture = render_page_texture(page, zoom);
    if (second_page) {
        second_page_texture = render_page_texture(second_page, zoom);
        fz_drop_page(ctx, second_page);
    }

    texture_zoom = zoom;
    last_raster_ms = SDL_GetTicks();
    raster_pending = false;
    
    fz_drop_page(ctx, page);
}

void PageLayout::set_zoom(float value) {
    value = fmin(fmax(min_zoom, value), max_zoom);
    
    if (value == zoom)
        return;
    
    zoom = value;

    last_zoom_input_ms = SDL_GetTicks();
    raster_pending = true;
    move_page(0, 0);
}

void PageLayout::move_page(float x, float y) {
    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;

    if (spread_mode) {
        bool has_second_page = second_page_bounds.x1 > 0.f && second_page_bounds.y1 > 0.f;
        if (has_second_page) {
            float w2 = second_page_bounds.x1 * zoom;
            float h2 = second_page_bounds.y1 * zoom;
            w = w + PAGE_SPREAD_GUTTER + w2;
            h = fmax(h, h2);
        } else {
            w = page_bounds.x1 * zoom;
            h = page_bounds.y1 * zoom;
        }
    }

    page_center.x = clamp_page_center(page_center.x, x, w, viewport.w);
    page_center.y = clamp_page_center(page_center.y, y, h, viewport.h);
}

// ── New helpers ───────────────────────────────────────────────────────────────

void PageLayout::go_to_page(int n) {
    render_page_to_texture(std::min(std::max(0, n), pages_count - 1), false);
}

int PageLayout::second_page_number() const {
    return (spread_mode && second_page_texture && _current_page + 1 < pages_count)
           ? _current_page + 1 : -1;
}

// Returns the screen-space SDL_Rect for a link given in page-space coordinates.
// page_index: 0 = left/only page, 1 = right page (spread mode only).
SDL_Rect PageLayout::page_rect_to_screen(const fz_rect &r, int page_index) const {
    float w  = page_bounds.x1        * zoom;
    float h  = page_bounds.y1        * zoom;
    float w2 = second_page_bounds.x1 * zoom;
    float h2 = second_page_bounds.y1 * zoom;
    float rx, ry;

    if (spread_mode) {
        bool  has2  = (second_page_texture != nullptr);
        float total = w + (has2 ? PAGE_SPREAD_GUTTER + w2 : 0.f);
        float left  = page_center.x - total / 2.f;
        if (page_index == 0) {
            rx = left;
            ry = page_center.y - h / 2.f;
        } else if (page_index == 1 && has2) {
            rx = left + w + PAGE_SPREAD_GUTTER;
            ry = page_center.y - h2 / 2.f;
        } else {
            return {0, 0, 0, 0};
        }
    } else {
        if (page_index != 0) return {0, 0, 0, 0};
        rx = page_center.x - w / 2.f;
        ry = page_center.y - h / 2.f;
    }

    // A 180° spread renders each page upside-down about its own centre, so mirror
    // the link rect within page space before mapping it to the screen.
    float max_x = (page_index == 1) ? second_page_bounds.x1 : page_bounds.x1;
    float max_y = (page_index == 1) ? second_page_bounds.y1 : page_bounds.y1;
    fz_rect rr = r;
    if (_draw_angle == 180) {
        rr.x0 = max_x - r.x1;
        rr.x1 = max_x - r.x0;
        rr.y0 = max_y - r.y1;
        rr.y1 = max_y - r.y0;
    }

    return {
        (int)(rx + rr.x0 * zoom),
        (int)(ry + rr.y0 * zoom),
        (int)((rr.x1 - rr.x0) * zoom),
        (int)((rr.y1 - rr.y0) * zoom)
    };
}

// Maps a screen-space tap back to page-space coordinates.
// Returns true when the point lies within the rendered page rect.
bool PageLayout::screen_to_page_coords(int sx, int sy, int page_index,
                                        float *px, float *py) const {
    float w  = page_bounds.x1        * zoom;
    float h  = page_bounds.y1        * zoom;
    float w2 = second_page_bounds.x1 * zoom;
    float h2 = second_page_bounds.y1 * zoom;
    float rx, ry;
    float max_x, max_y;

    if (spread_mode) {
        bool  has2  = (second_page_texture != nullptr);
        float total = w + (has2 ? PAGE_SPREAD_GUTTER + w2 : 0.f);
        float left  = page_center.x - total / 2.f;
        if (page_index == 0) {
            rx = left; ry = page_center.y - h  / 2.f;
            max_x = page_bounds.x1; max_y = page_bounds.y1;
        } else if (page_index == 1 && has2) {
            rx = left + w + PAGE_SPREAD_GUTTER; ry = page_center.y - h2 / 2.f;
            max_x = second_page_bounds.x1; max_y = second_page_bounds.y1;
        } else {
            return false;
        }
    } else {
        if (page_index != 0) return false;
        rx = page_center.x - w / 2.f;
        ry = page_center.y - h / 2.f;
        max_x = page_bounds.x1; max_y = page_bounds.y1;
    }

    *px = (sx - rx) / zoom;
    *py = (sy - ry) / zoom;
    if (_draw_angle == 180) {
        *px = max_x - *px;
        *py = max_y - *py;
    }
    return (*px >= 0.f && *py >= 0.f && *px <= max_x && *py <= max_y);
}

// ── Zoom / pan helpers used by mouse-centred zoom, pinch and touch pan ─────────

// Zoom to an absolute value while keeping the world point under (ax, ay) fixed.
// Works for both portrait and landscape layouts because scaling and rotation
// share the same centre (page_center).
void PageLayout::set_zoom_at(float value, float ax, float ay) {
    value = fmin(fmax(min_zoom, value), max_zoom);
    if (value == zoom)
        return;

    float ratio = value / zoom;
    page_center.x = ax + (page_center.x - ax) * ratio;
    page_center.y = ay + (page_center.y - ay) * ratio;
    zoom = value;

    last_zoom_input_ms = SDL_GetTicks();
    raster_pending = true;
    move_page(0, 0); // clamp back into view
}

// Pan the page directly in screen space (clamped). Overridden for landscape.
void PageLayout::pan_by_screen(float dx, float dy) {
    move_page(dx, dy);
}

// True when a pan in the given screen direction cannot move the page any
// further (i.e. the corresponding edge is already reached).
bool PageLayout::at_screen_edge(float dx, float dy) {
    fz_point before = page_center;
    pan_by_screen(dx, dy);
    bool moved = (fabsf(page_center.x - before.x) > 0.5f) ||
                 (fabsf(page_center.y - before.y) > 0.5f);
    page_center = before;
    return !moved;
}
