#include "LandscapePageLayout.hpp"
#include "common.h"
#include <algorithm>

static const int SCREEN_WIDTH = 1280;
static const int SCREEN_HEIGHT = 720;

LandscapePageLayout::LandscapePageLayout(fz_document *doc, int current_page, int angle):PageLayout(doc, current_page, false) {
    _angle = ((angle % 360) + 360) % 360;

    // A 90/270 turn swaps the page's aspect versus the screen, so fit the page
    // into a viewport with width/height swapped. A 180 turn keeps the aspect.
    if (_angle == 90 || _angle == 270) {
        int w = viewport.w;
        viewport.w = viewport.h;
        viewport.h = w;
    }

    render_page_to_texture(_current_page, true);
    reset();
}

void LandscapePageLayout::reset() {
    page_center = fz_make_point(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
    set_zoom(min_zoom);
};

void LandscapePageLayout::draw_page() {
    float w = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;

    // The texture is drawn unrotated and centred, then SDL rotates it clockwise
    // about the rect centre. Because the fit above accounts for the swap, the
    // rotated result always stays inside the 1280x720 screen.
    SDL_Rect rect;
    rect.x = page_center.x - w / 2;
    rect.y = page_center.y - h / 2;
    rect.w = w;
    rect.h = h;

    SDL_RenderCopyEx(RENDERER, page_texture, NULL, &rect, _angle, NULL, SDL_FLIP_NONE);
}

void LandscapePageLayout::move_page(float x, float y) {
    float pw = page_bounds.x1 * zoom, ph = page_bounds.y1 * zoom;

    // On-screen extent of the rotated page.
    bool quarter = (_angle == 90 || _angle == 270);
    float sw = quarter ? ph : pw;
    float sh = quarter ? pw : ph;

    // Map the requested screen-space pan onto the page's local axes so that the
    // stick moves the page in the direction the user pushes, whatever the angle.
    float mx, my;
    switch (_angle) {
        case 90:  mx =  y; my = -x; break;
        case 180: mx = -x; my = -y; break;
        case 270: mx = -y; my =  x; break;
        default:  mx =  x; my =  y; break;
    }

    page_center.x = (sw <= SCREEN_WIDTH)  ? SCREEN_WIDTH  / 2.0f : fmin(fmax(page_center.x + mx, SCREEN_WIDTH  - sw / 2), sw / 2);
    page_center.y = (sh <= SCREEN_HEIGHT) ? SCREEN_HEIGHT / 2.0f : fmin(fmax(page_center.y + my, SCREEN_HEIGHT - sh / 2), sh / 2);
}

// Screen rect for a page-space link rect, accounting for the rotation applied
// by SDL_RenderCopyEx about the texture centre.
SDL_Rect LandscapePageLayout::page_rect_to_screen(const fz_rect &r, int page_index) const {
    if (page_index != 0) return {0, 0, 0, 0};
    float w  = page_bounds.x1 * zoom, h  = page_bounds.y1 * zoom;
    float cx = page_center.x,          cy = page_center.y;

    // Link centre in pre-rotation screen space.
    float pre_x = (cx - w * 0.5f) + (r.x0 + r.x1) * 0.5f * zoom;
    float pre_y = (cy - h * 0.5f) + (r.y0 + r.y1) * 0.5f * zoom;
    float lw    = (r.x1 - r.x0) * zoom;
    float lh    = (r.y1 - r.y0) * zoom;

    // Rotate the link centre _angle degrees CW around (cx, cy).
    float dx = pre_x - cx, dy = pre_y - cy;
    float sx, sy;
    switch (_angle) {
        case  90: sx = -dy + cx; sy =  dx + cy; break;
        case 180: sx = -dx + cx; sy = -dy + cy; break;
        case 270: sx =  dy + cx; sy = -dx + cy; break;
        default:  sx =  dx + cx; sy =  dy + cy; break;
    }

    // Dimensions swap for 90 / 270.
    float rw = (_angle == 90 || _angle == 270) ? lh : lw;
    float rh = (_angle == 90 || _angle == 270) ? lw : lh;

    return {
        (int)(sx - rw * 0.5f),
        (int)(sy - rh * 0.5f),
        (int)rw,
        (int)rh
    };
}

// Inverse-rotate a screen tap to recover page-space coordinates.
bool LandscapePageLayout::screen_to_page_coords(int sx, int sy, int page_index,
                                                  float *px, float *py) const {
    if (page_index != 0) return false;
    float w  = page_bounds.x1 * zoom, h = page_bounds.y1 * zoom;
    float cx = page_center.x,          cy = page_center.y;

    float dx = sx - cx, dy = sy - cy;
    float rx, ry;
    switch (_angle) {
        case  90: rx =  dy + cx; ry = -dx + cy; break;  // inverse of 90 CW = 90 CCW
        case 180: rx = -dx + cx; ry = -dy + cy; break;
        case 270: rx = -dy + cx; ry =  dx + cy; break;
        default:  rx =  dx + cx; ry =  dy + cy; break;
    }

    *px = (rx - (cx - w * 0.5f)) / zoom;
    *py = (ry - (cy - h * 0.5f)) / zoom;
    return (*px >= 0.f && *py >= 0.f && *px <= page_bounds.x1 && *py <= page_bounds.y1);
}
