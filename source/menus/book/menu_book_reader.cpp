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
    u32 finger_id;
    int start_x;
    int start_y;
    int last_x;
    int last_y;
    Uint32 start_ms;
    PageHold hold;
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

static int page_tap_direction(BookPageLayout layout, int x, int y) {
    if (layout == BookPageLayoutPortrait) {
        if (y < TAP_TOP_SAFE || y > TAP_BOTTOM_SAFE) {
            return 0;
        }

        if (x <= TAP_SIDE_WIDTH) {
            return -1;
        }

        if (x >= SCREEN_WIDTH - TAP_SIDE_WIDTH) {
            return 1;
        }

        return 0;
    }

    if (x < ROTATED_TAP_LEFT_SAFE || x > ROTATED_TAP_RIGHT_SAFE) {
        return 0;
    }

    if (y <= ROTATED_TAP_EDGE) {
        return -1;
    }

    if (y >= SCREEN_HEIGHT - ROTATED_TAP_EDGE) {
        return 1;
    }

    return 0;
}

static int page_swipe_direction(BookPageLayout layout, int dx, int dy) {
    if (layout == BookPageLayoutPortrait) {
        if (abs(dx) < SWIPE_DISTANCE || abs(dx) < abs(dy)) {
            return 0;
        }

        return dx < 0 ? 1 : -1;
    }

    if (abs(dy) < SWIPE_DISTANCE || abs(dy) < abs(dx)) {
        return 0;
    }

    return dy < 0 ? 1 : -1;
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
    
    // Configure our supported input layout: a single player with standard controller syles
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    PadState pad;
    padInitializeDefault(&pad);

    while(result >= 0 && appletMainLoop()) {
        reader->draw(helpMenu);
        
	padUpdate(&pad);

	u64 kDown = padGetButtonsDown(&pad);
	u64 kHeld = padGetButtons(&pad);	
	u64 kUp = padGetButtonsUp(&pad);

	HidTouchScreenState state={0};
    hidGetTouchScreenStates(&state, 1);
    Uint32 now = SDL_GetTicks();

    if (!helpMenu && state.count > 0) {
        HidTouchState current_touch = state.touches[0];

        if (!touch.active) {
            touch.active = true;
            touch.repeated = false;
            touch.finger_id = current_touch.finger_id;
            touch.start_x = current_touch.x;
            touch.start_y = current_touch.y;
            touch.last_x = current_touch.x;
            touch.last_y = current_touch.y;
            touch.start_ms = now;
            start_page_hold(&touch.hold, page_tap_direction(reader->currentPageLayout(), current_touch.x, current_touch.y), now);
        } else {
            touch.last_x = current_touch.x;
            touch.last_y = current_touch.y;

            if (abs(touch.last_x - touch.start_x) > TAP_SLOP || abs(touch.last_y - touch.start_y) > TAP_SLOP) {
                reset_page_hold(&touch.hold);
            } else if (process_page_hold(reader, &touch.hold, now)) {
                touch.repeated = true;
            }
        }
    } else if (touch.active) {
        int dx = touch.last_x - touch.start_x;
        int dy = touch.last_y - touch.start_y;
        Uint32 duration = now - touch.start_ms;

        if (!touch.repeated) {
            int direction = page_swipe_direction(reader->currentPageLayout(), dx, dy);

            if (direction == 0 && duration <= TAP_MAX_MS && abs(dx) <= TAP_SLOP && abs(dy) <= TAP_SLOP) {
                direction = page_tap_direction(reader->currentPageLayout(), touch.start_x, touch.start_y);
            }

            turn_pages(reader, direction, 1);
        }

        touch.active = false;
        touch.repeated = false;
        reset_page_hold(&touch.hold);
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

        if (!helpMenu && kDown & HidNpadButton_R) {
            reader->next_page(10);
        } else if (!helpMenu && kDown & HidNpadButton_L) {
            reader->previous_page(10);
        }

        if (!helpMenu && ((kDown & HidNpadButton_Up) || (kHeld & HidNpadButton_StickRUp))) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->zoom_in();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->previous_page(1);
            }
        } else if (!helpMenu && ((kDown & HidNpadButton_Down) || (kHeld & HidNpadButton_StickRDown))) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->zoom_out();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->next_page(1);
            }
        }

        if (!helpMenu && kHeld & HidNpadButton_StickLUp) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->move_page_up();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_right();
            }
        } else if (!helpMenu && kHeld & HidNpadButton_StickLDown) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                reader->move_page_down();
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_left();
            }
        } else if (!helpMenu && kHeld & HidNpadButton_StickLRight) {
            if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_down();
            }
        } else if (!helpMenu && kHeld & HidNpadButton_StickLLeft) {
            if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->move_page_up();
            }
        }

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
