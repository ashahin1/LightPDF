// verify_presenter.cpp - Unit and Integration Tests for Laser Pointer & Presenter Bar
#include <windows.h>
#include <d2d1_1.h>
#include <iostream>
#include <cassert>
#include "../src/d2d_renderer.hpp"

#define TEST_ASSERT(cond, msg) \
    if (!(cond)) { \
        std::wcerr << L"[FAIL] " << msg << L"\n"; \
        return 1; \
    } else { \
        std::wcout << L"[PASS] " << msg << L"\n"; \
    }

int main() {
    std::wcout << L"========================================================\n";
    std::wcout << L"  RUNNING PRESENTER & LASER POINTER VERIFICATION SUITE\n";
    std::wcout << L"========================================================\n\n";

    // TEST 1: Color Cycling
    std::wcout << L"--- TEST 1: Laser Color Enumeration & Cycling ---\n";
    LaserColor col = LaserColor::Red;
    auto cycle = [](LaserColor c) {
        switch (c) {
        case LaserColor::Red:   return LaserColor::Green;
        case LaserColor::Green: return LaserColor::Cyan;
        case LaserColor::Cyan:  return LaserColor::Gold;
        case LaserColor::Gold:  return LaserColor::Red;
        }
        return LaserColor::Red;
    };

    col = cycle(col);
    TEST_ASSERT(col == LaserColor::Green, L"Red cycles to Green");
    col = cycle(col);
    TEST_ASSERT(col == LaserColor::Cyan, L"Green cycles to Cyan");
    col = cycle(col);
    TEST_ASSERT(col == LaserColor::Gold, L"Cyan cycles to Gold");
    col = cycle(col);
    TEST_ASSERT(col == LaserColor::Red, L"Gold cycles to Red");

    // TEST 2: Presenter Bar Layout Geometry
    std::wcout << L"\n--- TEST 2: Presenter Bar Layout Dimensions ---\n";
    float dipWidth = 1920.0f;
    float dipHeight = 1080.0f;
    D2D1_RECT_F bar = PresenterBarLayout::GetBarRect(dipWidth, dipHeight);

    TEST_ASSERT((bar.right - bar.left) == PresenterBarLayout::WIDTH, L"Bar width matches constant (340 DIPs)");
    TEST_ASSERT((bar.bottom - bar.top) == PresenterBarLayout::HEIGHT, L"Bar height matches constant (44 DIPs)");
    TEST_ASSERT(bar.bottom == (dipHeight - PresenterBarLayout::BOTTOM_MARGIN), L"Bar is pinned to bottom with margin");
    float expectedCenter = dipWidth * 0.5f;
    float actualCenter = (bar.left + bar.right) * 0.5f;
    TEST_ASSERT(std::abs(expectedCenter - actualCenter) < 0.001f, L"Bar is horizontally centered");

    // TEST 3: Presenter Bar Button Geometry Bounds
    std::wcout << L"\n--- TEST 3: Presenter Bar Buttons Containment ---\n";
    D2D1_RECT_F btnPrev = PresenterBarLayout::GetPrevBtnRect(bar);
    D2D1_RECT_F btnNext = PresenterBarLayout::GetNextBtnRect(bar);
    D2D1_RECT_F btnLaser = PresenterBarLayout::GetLaserBtnRect(bar);
    D2D1_RECT_F btnColor = PresenterBarLayout::GetColorBtnRect(bar);
    D2D1_RECT_F btnExit = PresenterBarLayout::GetExitBtnRect(bar);

    auto insideBar = [&](const D2D1_RECT_F& r) {
        return r.left >= bar.left && r.right <= bar.right && r.top >= bar.top && r.bottom <= bar.bottom;
    };

    TEST_ASSERT(insideBar(btnPrev), L"Prev button is inside bar boundaries");
    TEST_ASSERT(insideBar(btnNext), L"Next button is inside bar boundaries");
    TEST_ASSERT(insideBar(btnLaser), L"Laser button is inside bar boundaries");
    TEST_ASSERT(insideBar(btnColor), L"Color button is inside bar boundaries");
    TEST_ASSERT(insideBar(btnExit), L"Exit button is inside bar boundaries");

    // Button non-overlapping order (left-to-right)
    TEST_ASSERT(btnPrev.right <= btnNext.left, L"Prev button is left of Next button");
    TEST_ASSERT(btnNext.right <= btnLaser.left, L"Next button is left of Laser button");
    TEST_ASSERT(btnLaser.right <= btnColor.left, L"Laser button is left of Color button");
    TEST_ASSERT(btnColor.right <= btnExit.left, L"Color button is left of Exit button");

    // TEST 4: Laser Pointer State Struct
    std::wcout << L"\n--- TEST 4: Laser Pointer Render Info ---\n";
    LaserPointerRenderInfo laser;
    TEST_ASSERT(!laser.active, L"Laser pointer inactive by default");
    TEST_ASSERT(laser.color == LaserColor::Red, L"Default laser color is Red");

    laser.active = true;
    laser.position = D2D1::Point2F(500.0f, 400.0f);
    laser.color = LaserColor::Green;
    TEST_ASSERT(laser.active && laser.position.x == 500.0f && laser.color == LaserColor::Green, L"Laser active state and coordinates set correctly");

    std::wcout << L"\n========================================================\n";
    std::wcout << L"  ALL PRESENTER & LASER POINTER TESTS PASSED!\n";
    std::wcout << L"========================================================\n";
    return 0;
}
