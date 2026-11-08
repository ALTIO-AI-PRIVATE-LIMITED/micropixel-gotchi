// SPDX-License-Identifier: CC0-1.0
//
// CardLife for the Gotchi. Ported from CardLife by Mystereon
// (https://github.com/Mystereon/CardLife), released under CC0.
// Keeps the original rule set: survive on 2 or 3, born on 3 or 7.
//
// Controls: tap to nuke and reseed, BOOT to change the cell size.
#include "apps/portkit/portkit.hpp"

namespace {

using micropixel::Application;
using portkit::Canvas;
using portkit::Rgb;

constexpr int32_t kCellSizes[] = {6, 4, 10};
constexpr int32_t kMaxX = 74, kMaxY = 60;
constexpr portkit::Color kPalette[] = {Rgb(255, 106, 72), Rgb(62, 230, 196), Rgb(255, 200, 61), Rgb(169, 139, 255),
                                       Rgb(120, 220, 110), Rgb(255, 120, 190)};

uint8_t grid[kMaxX][kMaxY];
uint8_t next_grid[kMaxX][kMaxY];

class CardLife final {
   public:
    explicit CardLife(Application& app) : app_(app), canvas_(app, Rgb(0, 0, 0), 1900U, 6U) { Seed(); }

    void Tick(uint32_t ms) {
        elapsed_ += ms;
        if (nuke_ms_ > 0U) { nuke_ms_ = nuke_ms_ > ms ? nuke_ms_ - ms : 0U; }
        if (elapsed_ >= 100U) {
            elapsed_ = 0U;
            Compute();
            ++generation_;
        }
        Draw();
    }

    void Touch(const micropixel::TouchEvent& touch) {
        if (touch.phase() == micropixel::TouchPhase::kUp) {
            nuke_ms_ = 500U;
            Seed();
            portkit::Beep(app_, 120U, 180U);
        }
    }

    void Key(const micropixel::KeyEvent& key) {
        if (key.phase() == micropixel::KeyPhase::kDown && key.code() == micropixel::KeyCode::kConfirm) {
            size_index_ = (size_index_ + 1U) % 3U;
            Seed();
        }
    }

   private:
    [[nodiscard]] int32_t cell() const { return kCellSizes[size_index_]; }
    [[nodiscard]] int32_t columns() const {
        const int32_t n = canvas_.width() / cell();
        return n > kMaxX ? kMaxX : n;
    }
    [[nodiscard]] int32_t rows() const {
        const int32_t n = canvas_.height() / cell();
        return n > kMaxY ? kMaxY : n;
    }

    void Seed() {
        for (int32_t x = 0; x < kMaxX; ++x) {
            for (int32_t y = 0; y < kMaxY; ++y) {
                const bool edge = x == 0 || y == 0 || x >= columns() - 1 || y >= rows() - 1;
                grid[x][y] = !edge && app_.random().Below(3U) == 1U ? 1U : 0U;
            }
        }
        color_ = kPalette[app_.random().Below(6U)];
        generation_ = 0U;
        ++run_;
    }

    void Compute() {
        const int32_t w = columns(), h = rows();
        for (int32_t x = 1; x < w - 1; ++x) {
            for (int32_t y = 1; y < h - 1; ++y) {
                const int n = grid[x - 1][y] + grid[x - 1][y - 1] + grid[x][y - 1] + grid[x + 1][y - 1] + grid[x + 1][y] +
                              grid[x + 1][y + 1] + grid[x][y + 1] + grid[x - 1][y + 1];
                next_grid[x][y] = grid[x][y] != 0U ? static_cast<uint8_t>(n == 2 || n == 3)
                                                   : static_cast<uint8_t>(n == 3 || n == 7);
            }
        }
        for (int32_t x = 1; x < w - 1; ++x) {
            for (int32_t y = 1; y < h - 1; ++y) { grid[x][y] = next_grid[x][y]; }
        }
    }

    void Draw() {
        canvas_.Begin();
        const int32_t w = columns(), h = rows(), size = cell();
        for (int32_t y = 0; y < h; ++y) {
            int32_t start = -1;
            for (int32_t x = 0; x <= w; ++x) {
                const bool alive = x < w && grid[x][y] != 0U;
                if (alive && start < 0) { start = x; }
                if (!alive && start >= 0) {
                    canvas_.Fill(start * size, y * size, (x - start) * size, size, color_);
                    start = -1;
                }
            }
        }
        canvas_.Fill(canvas_.width() - 62, 4, 58, 44, Rgb(0, 0, 0), 190U);
        canvas_.Text(canvas_.width() - 56, 7, "GEN", Rgb(160, 160, 170));
        canvas_.Number(canvas_.width() - 56, 24, static_cast<int32_t>(generation_), Rgb(255, 255, 255));
        if (nuke_ms_ > 0U) {
            canvas_.Round(canvas_.width() / 2 - 60, canvas_.height() / 2 - 20, 120, 40, 8, Rgb(200, 30, 40));
            canvas_.Text(canvas_.width() / 2, canvas_.height() / 2 - 12, "NUKE", Rgb(255, 255, 255),
                         micropixel::SystemFont::kMedium, true);
        }
        canvas_.Present();
    }

    Application& app_;
    Canvas canvas_;
    portkit::Color color_{Rgb(255, 106, 72)};
    uint32_t elapsed_{}, nuke_ms_{}, generation_{}, run_{}, size_index_{};
};

}  // namespace

int main() {
    Application app;
    CardLife game(app);
    return portkit::Run(app, game);
}
