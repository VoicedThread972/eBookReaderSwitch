extern "C" {
    #include "menu_book_reader.h"
    #include "MenuChooser.h"
    #include "common.h"
    #include "config.h"
    #include "SDL_helper.h"
}

#include <iostream>
#include <cmath>
#include "BookReader.hpp"

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

    const int tapCooldownMs = 220;
    const int tapMaxDistance = 35;
    const int tapMaxDurationMs = 250;
    const int swipeMinDistance = 120;
    const int holdBaseIntervalMs = 350;
    const int holdMinIntervalMs = 70;
    const int holdMoveTolerance = 45;

    bool touchActive = false;
    int touchStartX = 0;
    int touchStartY = 0;
    int touchLastX = 0;
    int touchLastY = 0;
    u64 touchStartTick = 0;
    u64 lastTapTick = 0;

    int touchHoldDirection = 0;
    int touchHoldStepIndex = 1;
    bool touchStartedAsPageTurn = false;
    u64 touchLastHoldTurnTick = 0;

    bool helpMenu = false;
    
    // Configure our supported input layout: a single player with standard controller syles
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    // Initialize the default gamepad (which reads handheld mode inputs as well as the first connected controller)
    PadState pad;
    padInitializeDefault(&pad);
    //Touch_Process(&touchInfo);

    while(result >= 0 && appletMainLoop()) {
        reader->draw(helpMenu);

        auto apply_page_turn = [&](int direction, int step = 1) {
            if (direction > 0) {
                reader->next_page(step);
            } else if (direction < 0) {
                reader->previous_page(step);
            }
        };

        auto page_turn_direction_for_point = [&](int x, int y) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait) {
                if (x > 1000 && (y > 200 && y < 500)) return 1;
                if (x < 280 && (y > 200 && y < 500)) return -1;
            } else if (reader->currentPageLayout() == BookPageLayoutLandscape) {
                if (y < 200) return -1;
                if (y > 500) return 1;
            }
            return 0;
        };

        auto apply_swipe = [&](int direction) {
            if (direction > 0) {
                apply_page_turn(1);
            } else if (direction < 0) {
                apply_page_turn(-1);
            }
        };

        auto apply_tap_non_turn = [&](int x, int y) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait) {
                if (y < 200)
                    reader->zoom_in();
                else if (y > 500)
                    reader->zoom_out();
            } else if (reader->currentPageLayout() == BookPageLayoutLandscape) {
                if (x > 1000 && (y > 200 && y < 500))
                    reader->zoom_in();
                else if (x < 280 && (y > 200 && y < 500))
                    reader->zoom_out();
            }
        };
        
	padUpdate(&pad);

	u64 kDown = padGetButtonsDown(&pad);
	u64 kHeld = padGetButtons(&pad);	
	u64 kUp = padGetButtonsUp(&pad);

	HidTouchScreenState state={0};
	
    if(hidGetTouchScreenStates(&state, 1)) {
        if (state.count > 0 && !helpMenu) {
            int x = state.touches[0].x;
            int y = state.touches[0].y;
            u64 now = armGetSystemTick();
            if (!touchActive) {
                touchActive = true;
                touchStartX = x;
                touchStartY = y;
                touchStartTick = now;

                touchHoldDirection = page_turn_direction_for_point(x, y);
                touchStartedAsPageTurn = (touchHoldDirection != 0);
                touchHoldStepIndex = 1;
                touchLastHoldTurnTick = now;
                if (touchHoldDirection != 0) {
                    // A tap should always turn exactly one page immediately.
                    apply_page_turn(touchHoldDirection);
                }
            }

            touchLastX = x;
            touchLastY = y;

            if (touchHoldDirection != 0) {
                int moveDx = std::abs(x - touchStartX);
                int moveDy = std::abs(y - touchStartY);
                int currentDirection = page_turn_direction_for_point(x, y);

                if (currentDirection == touchHoldDirection && moveDx <= holdMoveTolerance && moveDy <= holdMoveTolerance) {
                    int nextIntervalMs = holdBaseIntervalMs / touchHoldStepIndex;
                    if (nextIntervalMs < holdMinIntervalMs) {
                        nextIntervalMs = holdMinIntervalMs;
                    }

                    int elapsedMs = (int)armTicksToNs(now - touchLastHoldTurnTick) / 1000000;
                    if (elapsedMs >= nextIntervalMs) {
                        apply_page_turn(touchHoldDirection);
                        touchLastHoldTurnTick = now;
                        touchHoldStepIndex++;
                    }
                } else {
                    touchHoldDirection = 0;
                }
            }
        } else if (state.count == 0 && touchActive && !helpMenu) {
            int dx = touchLastX - touchStartX;
            int dy = touchLastY - touchStartY;
            int absDx = std::abs(dx);
            int absDy = std::abs(dy);
            u64 now = armGetSystemTick();
            int durationMs = (int)armTicksToNs(now - touchStartTick) / 1000000;

            bool isHorizontalSwipe = absDx >= swipeMinDistance && absDx > absDy;
            bool isTap = absDx <= tapMaxDistance && absDy <= tapMaxDistance && durationMs <= tapMaxDurationMs;

            if (isHorizontalSwipe && !touchStartedAsPageTurn) {
                apply_swipe(dx > 0 ? 1 : -1);
            } else if (isTap) {
                int tapElapsedMs = (int)armTicksToNs(now - lastTapTick) / 1000000;
                if (lastTapTick == 0 || tapElapsedMs >= tapCooldownMs) {
                    apply_tap_non_turn(touchLastX, touchLastY);
                    lastTapTick = now;
                }
            }

            touchActive = false;
            touchHoldDirection = 0;
            touchHoldStepIndex = 1;
            touchStartedAsPageTurn = false;
        }
    }

        if (!helpMenu && kDown & HidNpadButton_Left) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                apply_page_turn(-1);
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->zoom_out();
            }
        } else if (!helpMenu && kDown & HidNpadButton_Right) {
            if (reader->currentPageLayout() == BookPageLayoutPortrait ) {
                apply_page_turn(1);
            } else if ((reader->currentPageLayout() == BookPageLayoutLandscape) ) {
                reader->zoom_in();
            }
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
        apply_page_turn(1);
	else if (!helpMenu && kDown & HidNpadButton_LeftSL)
        apply_page_turn(-1);

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
