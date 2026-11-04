// SPDX-License-Identifier: Apache-2.0
//
// Renders the 3x3 wall on a computer with the Gotchi renderer, as raw RGB24
// frames on stdout (pipe into ffmpeg), with dark bezels between the tiles.
//
//   c++ -std=c++20 -O2 -Isrc tools/preview.cpp src/wall_render.cpp -o preview
//   ./preview 15 | ffmpeg -f rawvideo -pix_fmt rgb24 -s 936x768 -r 15 -i - wall.mp4
//   ./preview still 14000 > frame.rgb
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wall_render.hpp"

using namespace devcon_wall;

namespace {
constexpr int kBezel = 24;
constexpr int kOutWidth = kColumns * kTileWidth + (kColumns + 1) * kBezel - 2 * kBezel + 2 * (kBezel / 2);
constexpr int kOutHeight = kRows * kTileHeight + (kRows + 1) * kBezel - 2 * kBezel + 2 * (kBezel / 2);
uint16_t tile[kTileWidth * kTileHeight];
unsigned char image[kOutWidth * kOutHeight * 3];

void Frame(uint32_t ms, bool badges) {
    memset(image, 18, sizeof(image));
    for (uint32_t spot = 1; spot <= 9U; ++spot) {
        RenderTile(TileForSpot(spot), ms, tile, kTileWidth);
        if (badges) RenderBadge(spot, spot == kLeaderSpot ? LinkStatus::kSynced : LinkStatus::kSynced, tile, kTileWidth);
        const int col = static_cast<int>(spot - 1U) % kColumns;
        const int row = static_cast<int>(spot - 1U) / kColumns;
        const int ox = kBezel / 2 + col * (kTileWidth + kBezel);
        const int oy = kBezel / 2 + row * (kTileHeight + kBezel);
        for (int y = 0; y < kTileHeight; ++y) {
            for (int x = 0; x < kTileWidth; ++x) {
                const uint16_t c = tile[y * kTileWidth + x];
                unsigned char* p = &image[((oy + y) * kOutWidth + ox + x) * 3];
                p[0] = static_cast<unsigned char>(((c >> 11) & 31) * 255 / 31);
                p[1] = static_cast<unsigned char>(((c >> 5) & 63) * 255 / 63);
                p[2] = static_cast<unsigned char>((c & 31) * 255 / 31);
            }
        }
    }
    fwrite(image, 1, sizeof(image), stdout);
}
}  // namespace

int main(int argc, char** argv) {
    fprintf(stderr, "%dx%d\n", kOutWidth, kOutHeight);
    if (argc > 2 && strcmp(argv[1], "still") == 0) {
        Frame(static_cast<uint32_t>(atoi(argv[2])), argc > 3);
        return 0;
    }
    const int fps = argc > 1 ? atoi(argv[1]) : 15;
    for (uint32_t ms = 0; ms < kLoopMs; ms += 1000U / static_cast<uint32_t>(fps)) Frame(ms, false);
    return 0;
}
