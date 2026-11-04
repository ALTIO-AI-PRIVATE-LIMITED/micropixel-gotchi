// SPDX-License-Identifier: Apache-2.0
//
// DevCon Wall renderer. Draws one Gotchi's tile of the shared wall animation.
// Pure C++ with no SDK dependency, so tools/preview.cpp renders the very same
// frames on a computer.
#pragma once

#include <stdint.h>

namespace devcon_wall {

inline constexpr int32_t kColumns = 3;
inline constexpr int32_t kRows = 3;
inline constexpr int32_t kTileWidth = 296;
inline constexpr int32_t kTileHeight = 240;
// Screen-to-screen gap in panel pixels (bezels plus spacing). 0 treats the
// wall as one seamless canvas; set it to the measured gap so moving things
// line up across the physical frames.
inline constexpr int32_t kGapX = 0;
inline constexpr int32_t kGapY = 0;
inline constexpr int32_t kCanvasWidth = kColumns * kTileWidth + (kColumns - 1) * kGapX;
inline constexpr int32_t kCanvasHeight = kRows * kTileHeight + (kRows - 1) * kGapY;
// One loop of the show.
inline constexpr uint32_t kLoopMs = 32000U;

// Spot 1..9, row-major from the top left as seen from the front. Spot 5 leads.
inline constexpr uint32_t kLeaderSpot = 5U;

struct Tile final {
    int32_t origin_x;
    int32_t origin_y;
};

[[nodiscard]] constexpr Tile TileForSpot(uint32_t spot) {
    const int32_t index = static_cast<int32_t>(spot) - 1;
    return Tile{(index % kColumns) * (kTileWidth + kGapX), (index / kColumns) * (kTileHeight + kGapY)};
}

// Renders `time_ms` (any value; wrapped to the loop) of the wall into a
// kTileWidth x kTileHeight RGB565 buffer, `pitch_pixels` per row, native
// byte order. The caller byte-swaps for the panel if it needs to.
void RenderTile(const Tile& tile, uint32_t time_ms, uint16_t* pixels, int32_t pitch_pixels);

// Small status badge in the tile's top-left corner: a 3x3 map with this
// spot lit, and a dot (green synced, amber searching, red offline).
enum class LinkStatus : uint8_t { kSynced, kSearching, kOffline };
void RenderBadge(uint32_t spot, LinkStatus status, uint16_t* pixels, int32_t pitch_pixels);

}  // namespace devcon_wall
