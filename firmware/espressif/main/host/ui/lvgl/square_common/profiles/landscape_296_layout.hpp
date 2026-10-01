#pragma once

#include <cstdint>

namespace micropixel::host_ui::lvgl::square_common::profiles::landscape_296 {

// The 320x240 layout with 24 px less width. Cards keep the 320 size so their
// labels and badges are unchanged; three still fit fully in the viewport.
struct Layout final {
    static constexpr int32_t kWidth = 296;
    static constexpr int32_t kHeight = 240;
    static constexpr int32_t kHallLeft = 12;
    static constexpr int32_t kHallTop = 88;
    static constexpr int32_t kHallViewportWidth = 284;
    static constexpr int32_t kHallCardWidth = 88;
    static constexpr int32_t kHallCardHeight = 120;
    static constexpr int32_t kHallCardGap = 8;
    static constexpr int32_t kHallScrollTrackWidth = 72;
    static constexpr int32_t kHallScrollTrackHeight = 3;
    static constexpr int32_t kStatusDialogVisibleY = 8;
};

static_assert(3 * Layout::kHallCardWidth + 2 * Layout::kHallCardGap <= Layout::kHallViewportWidth);

}  // namespace micropixel::host_ui::lvgl::square_common::profiles::landscape_296
