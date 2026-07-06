extern "C" {
    #include "menu_book_reader.h"
    #include "MenuChooser.h"
    #include "common.h"
    #include "config.h"
    #include "SDL_helper.h"
}

#include <iostream>
#include <algorithm>
#include <climits>
#include <cmath>
#include <string>
#include "BookReader.hpp"

static const int SCREEN_WIDTH = 1280;
static const int SCREEN_HEIGHT = 720;
static const int TAP_SIDE_WIDTH = 360;
static const int TAP_TOP_SAFE = 70;
static const int TAP_BOTTOM_SAFE = 650;
static const int ROTATED_TAP_EDGE = 220;
static const int ROTATED_TAP_LEFT_SAFE = 90;
static const int ROTATED_TAP_RIGHT_SAFE = 1190;
static const int TAP_SLOP = 34;
static const int SWIPE_DISTANCE = 110;
static const Uint32 TAP_MAX_MS = 420;
static const Uint32 HOLD_DELAY_MS = 360;
static const Uint32 HOLD_REPEAT_MS = 85;

typedef struct {
    int direction;
    Uint32 start_ms;
    Uint32 last_ms;
} PageHold;

typedef struct {
    bool active;
    bool repeated;
    bool moved;
    bool panning;
    bool suppress;      // ignore gesture after multi-touch until full release
    bool pinching;
    float pinch_last_dist;
    int pinch_last_mx;
    int pinch_last_my;
    u32 finger_id;
    int start_x;
    int start_y;
    int last_x;
    int last_y;
    Uint32 start_ms;
    PageHold hold;
    bool edge_lock_set;
    bool edge_turn_allowed;
    int edge_swipe_direction;
    float edge_px;
    float edge_py;
    float inertia_vx;
    float inertia_vy;
    bool inertia_active;
} TouchGesture;

static void reset_page_hold(PageHold *hold) {
    hold->direction = 0;
    hold->start_ms = 0;
    hold->last_ms = 0;
}

static void start_page_hold(PageHold *hold, int direction, Uint32 now) {
    hold->direction = direction;
    hold->start_ms = now;
    hold->last_ms = now;
}

static int accelerated_page_step(Uint32 held_ms) {
    if (held_ms < HOLD_DELAY_MS) {
        return 0;
    }

    double seconds = (held_ms - HOLD_DELAY_MS) / 1000.0;
    double pages = pow(2.0, seconds * 1.15);

    if (pages >= INT_MAX) {
        return INT_MAX;
    }

    return std::max(1, (int)pages);
}

static void turn_pages(BookReader *reader, int direction, int pages) {
    if (direction > 0) {
        reader->next_page(pages);
    } else if (direction < 0) {
        reader->previous_page(pages);
    }
}

static bool process_page_hold(BookReader *reader, PageHold *hold, Uint32 now) {
    if (hold->direction == 0 || now - hold->last_ms < HOLD_REPEAT_MS) {
        return false;
    }

    int pages = accelerated_page_step(now - hold->start_ms);
    if (pages <= 0) {
        return false;
    }

    turn_pages(reader, hold->direction, pages);
    hold->last_ms = now;
    return true;
}

// Tap-zone direction for each rotation angle.
// Returns +1 (next page), -1 (prev page), or 0 (neutral zone).
static int page_tap_direction(int rotation, int x, int y) {
    switch (rotation) {
        case 0:   // Normal spread: left = prev, right = next
            if (y < TAP_TOP_SAFE || y > TAP_BOTTOM_SAFE) return 0;
            if (x <= TAP_SIDE_WIDTH) return -1;
            if (x >= SCREEN_WIDTH - TAP_SIDE_WIDTH) return 1;
            return 0;
        case 90:  // CW 90°: top = prev, bottom = next
            if (x < ROTATED_TAP_LEFT_SAFE || x > ROTATED_TAP_RIGHT_SAFE) return 0;
            if (y <= ROTATED_TAP_EDGE) return -1;
            if (y >= SCREEN_HEIGHT - ROTATED_TAP_EDGE) return 1;
            return 0;
        case 180: // 180° spread: left = next, right = prev (reversed)
            if (y < TAP_TOP_SAFE || y > TAP_BOTTOM_SAFE) return 0;
            if (x <= TAP_SIDE_WIDTH) return 1;
            if (x >= SCREEN_WIDTH - TAP_SIDE_WIDTH) return -1;
            return 0;
        case 270: // CW 270°: top = next, bottom = prev
            if (x < ROTATED_TAP_LEFT_SAFE || x > ROTATED_TAP_RIGHT_SAFE) return 0;
            if (y <= ROTATED_TAP_EDGE) return 1;
            if (y >= SCREEN_HEIGHT - ROTATED_TAP_EDGE) return -1;
            return 0;
    }
    return 0;
}

static int page_swipe_direction(int rotation, int dx, int dy) {
    switch (rotation) {
        case 0:   // swipe left = next
            if (abs(dx) < SWIPE_DISTANCE || abs(dx) < abs(dy)) return 0;
            return dx < 0 ? 1 : -1;
        case 90:  // swipe up = next
            if (abs(dy) < SWIPE_DISTANCE || abs(dy) < abs(dx)) return 0;
            return dy < 0 ? 1 : -1;
        case 180: // swipe right = next (reversed from 0°)
            if (abs(dx) < SWIPE_DISTANCE || abs(dx) < abs(dy)) return 0;
            return dx > 0 ? 1 : -1;
        case 270: // swipe down = next (reversed from 90°)
            if (abs(dy) < SWIPE_DISTANCE || abs(dy) < abs(dx)) return 0;
            return dy > 0 ? 1 : -1;
    }
    return 0;
}

// Same mapping as page_swipe_direction, but without distance thresholds.
static int page_swipe_direction_dominant_only(int rotation, int dx, int dy) {
    if (dx == 0 && dy == 0) return 0;
    switch (rotation) {
        case 0:
            if (abs(dx) < abs(dy)) return 0;
            return dx < 0 ? 1 : -1;
        case 90:
            if (abs(dy) < abs(dx)) return 0;
            return dy < 0 ? 1 : -1;
        case 180:
            if (abs(dx) < abs(dy)) return 0;
            return dx > 0 ? 1 : -1;
        case 270:
            if (abs(dy) < abs(dx)) return 0;
            return dy > 0 ? 1 : -1;
    }
    return 0;
}

void Menu_OpenBook(char *path) {
    BookReader *reader = NULL;
    int result = 0;

    reader = new BookReader(path, &result);
    
    if(result < 0){
        std::cout << "Menu_OpenBook: document not loaded" << std::endl;
    }
    
    /*TouchInfo touchInfo;
    Touch_Init(&touchInfo);*/
    hidInitializeTouchScreen();

    bool helpMenu = false;
    TouchGesture touch = {0};
    PageHold buttonHold = {0};
    float cursor_x = 640.f, cursor_y = 360.f;  // virtual cursor / "mouse"
    int   focused_link = -1;                    // D-pad-selected link index
    Uint32 last_mouse_activity = SDL_GetTicks(); // for idle-hide of cursor+links
    const Uint32 CURSOR_IDLE_MS = 2000;
    
    // Configure our supported input layout: a single player with standard controller syles
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    PadState pad;
    padInitializeDefault(&pad);

    // Some documents return no links on the very first query; retry briefly so
    // hyperlink hitboxes work without needing a manual orientation change.
    Uint32 link_retry_deadline = SDL_GetTicks() + 3000;
    Uint32 last_link_retry = 0;

    while(result >= 0 && appletMainLoop()) {
        // Clamp focused link in case the page changed since last frame.
        {
            int n = (int)reader->page_links().size();
            if (n == 0) focused_link = -1;
            else if (focused_link >= n) focused_link = 0;
        }

	padUpdate(&pad);

	u64 kDown = padGetButtonsDown(&pad);
	u64 kHeld = padGetButtons(&pad);	
	u64 kUp = padGetButtonsUp(&pad);

	HidTouchScreenState state={0};
    hidGetTouchScreenStates(&state, 1);
    Uint32 now = SDL_GetTicks();

    // Retry loading links for the first few seconds while they are still empty.
    if (!helpMenu && reader->page_links().empty() && now < link_retry_deadline) {
        if (now - last_link_retry >= 200) {
            reader->reload_links();
            last_link_retry = now;
        }
    }

    // ── Left stick drives the virtual cursor / "mouse" ───────────────────────
    if (!helpMenu) {
        HidAnalogStickState ls = padGetStickPos(&pad, 0);
        const int DZ = 3000;
        auto axis = [DZ](int v) -> float {
            if (v >  DZ) return  (float)(v - DZ) / (32767 - DZ);
            if (v < -DZ) return  (float)(v + DZ) / (32767 - DZ);
            return 0.f;
        };
        float mvx = axis(ls.x), mvy = axis(ls.y);
        if (mvx != 0.f || mvy != 0.f) {
            float prev_x = cursor_x, prev_y = cursor_y;
            cursor_x = std::clamp(cursor_x + mvx * 8.f, 0.f, 1279.f);
            cursor_y = std::clamp(cursor_y - mvy * 8.f, 0.f, 719.f);
            last_mouse_activity = now;

            // When zoomed and the cursor is pinned against a screen edge, keep
            // pushing to pan the page (like a swipe) — never turns the page.
            if (reader->is_zoomed()) {
                const float EDGE_PAN = 9.f;
                float cvx = cursor_x - prev_x; // 0 while pinned at an edge
                float cvy = cursor_y - prev_y;
                float pdx = 0.f, pdy = 0.f;
                if (cvx == 0.f) {
                    if (cursor_x <= 0.5f    && mvx < 0.f) pdx =  mvx * EDGE_PAN * -1.f;
                    if (cursor_x >= 1278.5f && mvx > 0.f) pdx = -mvx * EDGE_PAN;
                }
                if (cvy == 0.f) {
                    if (cursor_y <= 0.5f    && mvy > 0.f) pdy =  mvy * EDGE_PAN;
                    if (cursor_y >= 718.5f  && mvy < 0.f) pdy =  mvy * EDGE_PAN;
                }
                if (pdx != 0.f || pdy != 0.f) reader->pan(pdx, pdy);
            }
        }
    }

    if (!helpMenu && state.count >= 2) {
        // ── Pinch to zoom, anchored at the midpoint (phone-like) ─────────────
        HidTouchState a = state.touches[0];
        HidTouchState b = state.touches[1];
        float dist = hypotf((float)b.x - (float)a.x, (float)b.y - (float)a.y);
        int mx = (a.x + b.x) / 2;
        int my = (a.y + b.y) / 2;

        if (!touch.pinching) {
            touch.pinching = true;
            touch.pinch_last_dist = dist;
            touch.pinch_last_mx = mx;
            touch.pinch_last_my = my;
            reset_page_hold(&touch.hold);
            touch.active = false;
            touch.inertia_active = false;
            touch.inertia_vx = 0.f;
            touch.inertia_vy = 0.f;
        } else {
            if (touch.pinch_last_dist > 1.f && dist > 1.f) {
                reader->zoom_by_ratio_at(dist / touch.pinch_last_dist, mx, my);
            }
            // Move the zoom focus with the pinch midpoint (single-gesture pan).
            float pan_dx = (float)(mx - touch.pinch_last_mx);
            float pan_dy = (float)(my - touch.pinch_last_my);
            reader->pan(pan_dx, pan_dy);
            touch.inertia_vx = touch.inertia_vx * 0.72f + pan_dx * 0.28f;
            touch.inertia_vy = touch.inertia_vy * 0.72f + pan_dy * 0.28f;
            touch.pinch_last_dist = dist;
            touch.pinch_last_mx = mx;
            touch.pinch_last_my = my;
        }
        touch.suppress = true;   // don't fire a swipe when the fingers lift
    } else if (!helpMenu && state.count == 1) {
        HidTouchState current_touch = state.touches[0];
        touch.pinching = false;

        if (touch.suppress) {
            // A finger from the pinch remains; ignore until full release.
        } else if (!touch.active) {
            touch.active = true;
            touch.repeated = false;
            touch.moved = false;
            touch.panning = false;
            touch.edge_lock_set = false;
            touch.edge_turn_allowed = false;
            touch.edge_swipe_direction = 0;
            touch.edge_px = 0.f;
            touch.edge_py = 0.f;
            touch.finger_id = current_touch.finger_id;
            touch.start_x = touch.last_x = current_touch.x;
            touch.start_y = touch.last_y = current_touch.y;
            touch.start_ms = now;
            touch.inertia_active = false;
            touch.inertia_vx = 0.f;
            touch.inertia_vy = 0.f;
            start_page_hold(&touch.hold, page_tap_direction(reader->rotation(), current_touch.x, current_touch.y), now);
        } else {
            int dx_move = current_touch.x - touch.last_x;
            int dy_move = current_touch.y - touch.last_y;

            if (abs((int)current_touch.x - touch.start_x) > TAP_SLOP ||
                abs((int)current_touch.y - touch.start_y) > TAP_SLOP) {
                touch.moved = true;
                reset_page_hold(&touch.hold);
            }

            if (reader->is_zoomed() && touch.moved) {
                // Pan the zoomed page so content follows the finger.
                touch.panning = true;

                if (!touch.edge_lock_set) {
                    int dx0 = current_touch.x - touch.start_x;
                    int dy0 = current_touch.y - touch.start_y;
                    if (abs(dx0) >= abs(dy0)) {
                        touch.edge_px = (dx0 < 0) ? -8.f : 8.f;
                        touch.edge_py = 0.f;
                    } else {
                        touch.edge_px = 0.f;
                        touch.edge_py = (dy0 < 0) ? -8.f : 8.f;
                    }
                    touch.edge_swipe_direction = page_swipe_direction_dominant_only(reader->rotation(), dx0, dy0);
                    touch.edge_turn_allowed = reader->at_edge(touch.edge_px, touch.edge_py);
                    touch.edge_lock_set = true;
                }

                reader->pan((float)dx_move, (float)dy_move);
                touch.inertia_vx = touch.inertia_vx * 0.72f + (float)dx_move * 0.28f;
                touch.inertia_vy = touch.inertia_vy * 0.72f + (float)dy_move * 0.28f;
            } else if (!reader->is_zoomed() && !touch.moved &&
                       process_page_hold(reader, &touch.hold, now)) {
                touch.repeated = true;
            }

            touch.last_x = current_touch.x;
            touch.last_y = current_touch.y;
        }
    } else if (touch.active || touch.suppress) {
        // ── Release ──────────────────────────────────────────────────────────
        int dx = touch.last_x - touch.start_x;
        int dy = touch.last_y - touch.start_y;
        Uint32 duration = now - touch.start_ms;

        if (touch.suppress) {
            // Came out of a pinch; consume the release without action.
            if (reader->is_zoomed() && (fabsf(touch.inertia_vx) > 0.15f || fabsf(touch.inertia_vy) > 0.15f)) {
                touch.inertia_active = true;
            }
        } else if (touch.panning) {
            // Edge lock is decided at gesture start; it cannot flip mid-gesture.
            int direction = touch.edge_swipe_direction;
            int final_direction = page_swipe_direction(reader->rotation(), dx, dy);
            if (touch.edge_turn_allowed && direction != 0 && final_direction == direction) {
                turn_pages(reader, direction, 1);
            } else if (reader->is_zoomed() && (fabsf(touch.inertia_vx) > 0.15f || fabsf(touch.inertia_vy) > 0.15f)) {
                touch.inertia_active = true;
            }
        } else if (!touch.repeated) {
            int direction = page_swipe_direction(reader->rotation(), dx, dy);

            if (direction == 0 && duration <= TAP_MAX_MS && abs(dx) <= TAP_SLOP && abs(dy) <= TAP_SLOP) {
                // Hyperlink taps take priority over page-turn taps.
                std::string link_uri = reader->hit_link(touch.start_x, touch.start_y);
                if (!link_uri.empty()) {
                    reader->follow_link(link_uri.c_str());
                    last_mouse_activity = now;
                } else {
                    direction = page_tap_direction(reader->rotation(), touch.start_x, touch.start_y);
                    turn_pages(reader, direction, 1);
                }
            } else {
                turn_pages(reader, direction, 1);
            }
        }

        touch.active = false;
        touch.repeated = false;
        touch.moved = false;
        touch.panning = false;
        touch.suppress = false;
        touch.pinching = false;
        touch.edge_lock_set = false;
        touch.edge_turn_allowed = false;
        touch.edge_swipe_direction = 0;
        touch.edge_px = 0.f;
        touch.edge_py = 0.f;
        reset_page_hold(&touch.hold);
    }

        // Let zoomed panning coast down naturally after finger release.
        if (!helpMenu && touch.inertia_active && reader->is_zoomed() && state.count == 0) {
            reader->pan(touch.inertia_vx, touch.inertia_vy);
            touch.inertia_vx *= 0.88f;
            touch.inertia_vy *= 0.88f;
            if (fabsf(touch.inertia_vx) < 0.05f && fabsf(touch.inertia_vy) < 0.05f) {
                touch.inertia_active = false;
                touch.inertia_vx = 0.f;
                touch.inertia_vy = 0.f;
            }
        }

        if (!helpMenu && kDown & HidNpadButton_Left) {
            reader->previous_page(1);
            start_page_hold(&buttonHold, -1, now);
        } else if (!helpMenu && kDown & HidNpadButton_Right) {
            reader->next_page(1);
            start_page_hold(&buttonHold, 1, now);
        } else if (!helpMenu && (kHeld & HidNpadButton_Left) && buttonHold.direction == -1) {
            process_page_hold(reader, &buttonHold, now);
        } else if (!helpMenu && (kHeld & HidNpadButton_Right) && buttonHold.direction == 1) {
            process_page_hold(reader, &buttonHold, now);
        } else if (!(kHeld & (HidNpadButton_Left | HidNpadButton_Right))) {
            reset_page_hold(&buttonHold);
        }

        // ── A: follow the hyperlink at the cursor, or the D-pad focused one ─────
        if (!helpMenu && (kDown & HidNpadButton_A)) {
            std::string uri = reader->hit_link((int)cursor_x, (int)cursor_y);
            if (uri.empty() && focused_link >= 0) {
                const auto &lks = reader->page_links();
                if (focused_link < (int)lks.size()) uri = lks[focused_link].uri;
            }
            if (!uri.empty()) {
                reader->follow_link(uri.c_str());
                last_mouse_activity = now;
            }
        }

        // ── D-pad up/down cycles hyperlinks; cursor jumps to the focused one ───
        if (!helpMenu) {
            const auto &lks = reader->page_links();
            int n = (int)lks.size();
            if (n > 0) {
                int prev = focused_link;
                if (kDown & HidNpadButton_Down)
                    focused_link = (focused_link < 0) ? 0 : (focused_link + 1) % n;
                else if (kDown & HidNpadButton_Up)
                    focused_link = (focused_link < 0) ? n-1 : ((focused_link - 1) + n) % n;
                if (focused_link != prev && focused_link >= 0) {
                    cursor_x = lks[focused_link].screen_rect.x + lks[focused_link].screen_rect.w * 0.5f;
                    cursor_y = lks[focused_link].screen_rect.y + lks[focused_link].screen_rect.h * 0.5f;
                    last_mouse_activity = now;
                }
            }
        }

        if (!helpMenu && kDown & HidNpadButton_R) {
            reader->next_page(10);
        } else if (!helpMenu && kDown & HidNpadButton_L) {
            reader->previous_page(10);
        }

        // ── Right stick: zoom, anchored at the virtual cursor ──────────────────────
        if (!helpMenu && (kHeld & HidNpadButton_StickRUp))   { reader->zoom_in_at((int)cursor_x, (int)cursor_y);  last_mouse_activity = now; }
        if (!helpMenu && (kHeld & HidNpadButton_StickRDown)) { reader->zoom_out_at((int)cursor_x, (int)cursor_y); last_mouse_activity = now; }

        // (Left-stick panning removed; left stick is now the virtual cursor.)

	if (!helpMenu && kDown & HidNpadButton_LeftSR)
		reader->next_page(10);
	else if (!helpMenu && kDown & HidNpadButton_LeftSL)
		reader->previous_page(10);

        if (kUp & HidNpadButton_B) {
            if (helpMenu) {
                helpMenu = !helpMenu;
            } else {
                break;
            }
        }

        if (!helpMenu && kDown & HidNpadButton_X) {
            reader->permStatusBar = !reader->permStatusBar;
        }
            
        if ((!helpMenu && kDown & HidNpadButton_StickL) || kDown & HidNpadButton_StickR) {
            reader->reset_page();
        }
        
        if (!helpMenu && kDown & HidNpadButton_Y) {
            reader->switch_page_layout();
        }

        if (!helpMenu && kUp & HidNpadButton_Minus) {
            configDarkMode = !configDarkMode;
            reader->previous_page(0);
        }

        if (kDown & HidNpadButton_Plus) {
            helpMenu = !helpMenu;
        }

        ReaderOverlay overlay;
        bool cursor_active = (SDL_GetTicks() - last_mouse_activity) < CURSOR_IDLE_MS;
        overlay.show_link_rects = cursor_active && !reader->page_links().empty() && !helpMenu;
        overlay.focused_link    = focused_link;
        overlay.cursor_visible  = cursor_active && !helpMenu;
        overlay.cursor_x        = (int)cursor_x;
        overlay.cursor_y        = (int)cursor_y;
        reader->draw(helpMenu, overlay);
 
        /*if (touchInfo.state == TouchEnded && touchInfo.tapType != TapNone) {
            float tapRegion = 120;
            
            switch (reader->currentPageLayout()) {
                case BookPageLayoutPortrait:
                    if (tapped_inside(touchInfo, 0, 0, tapRegion, 720))
                        reader->previous_page(1);
                    else if (tapped_inside(touchInfo, 1280 - tapRegion, 0, 1280, 720))
                        reader->next_page(1);
                    break;
                case BookPageLayoutLandscape:
                    if (tapped_inside(touchInfo, 0, 0, 1280, tapRegion))
                        reader->previous_page(1);
                    else if (tapped_inside(touchInfo, 0, 720 - tapRegion, 1280, 720))
                        reader->next_page(1);
                    reader->reset_page();
                    break;
            }
        }*/
    }

    std::cout << "Exiting reader" << std::endl;
    std::cout << "Opening chooser" << std::endl;
    Menu_StartChoosing();
    delete reader;
    // consoleExit(NULL);
}
