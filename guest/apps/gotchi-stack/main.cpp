// SPDX-License-Identifier: Apache-2.0
//
// Gotchi Stack: a one-touch tower game. A block slides back and forth above
// the tower; tap (or press any key) to drop it. Whatever hangs over the edge
// is cut off, so the tower gets narrower; a near-perfect drop keeps the full
// width. Miss the tower completely and the run ends. Best height is saved.
#include <stdint.h>

#include <span>

#include "gotchi-stack_sfx_profiles.hpp"
#include "sdk/micropixel.hpp"

namespace {

namespace mp = micropixel;
namespace math = micropixel::math;
using mp::Color;
using mp::Rect;

constexpr Color kBackground = Color::Rgb(12U, 12U, 20U);
constexpr Color kText = Color::Rgb(236U, 238U, 242U);
constexpr Color kDim = Color::Rgb(150U, 156U, 175U);
constexpr Color kGold = Color::Rgb(255U, 214U, 60U);
// CGotchi shell colours, cycled up the tower.
constexpr Color kPalette[] = {Color::Rgb(255U, 150U, 40U), Color::Rgb(170U, 230U, 60U), Color::Rgb(255U, 92U, 170U),
                              Color::Rgb(120U, 236U, 255U), Color::Rgb(236U, 238U, 242U), Color::Rgb(160U, 120U, 255U)};
constexpr uint32_t kPaletteSize = sizeof(kPalette) / sizeof(kPalette[0]);

constexpr uint32_t kVisible = 14U;       // block nodes drawn (tower top plus the moving block)
constexpr uint32_t kHistory = 32U;       // ring of remembered blocks
constexpr int32_t kBlockH = 18;
constexpr int32_t kPerfectPx = 4;
constexpr float kStartSpeed = 110.0F;    // px/s
constexpr float kSpeedPerLevel = 6.0F;
constexpr float kMaxSpeed = 280.0F;
constexpr float kDebrisFall = 420.0F;    // px/s^2
constexpr uint64_t kRetryDelayUs = 700'000U;

enum class State : uint8_t { kTitle, kPlaying, kOver };

struct Block {
    float x{};
    int32_t width{};
};

class GotchiStack final {
   public:
    explicit GotchiStack(mp::Application& app)
        : app_(app),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          frame_(app.timers().Every(mp::Duration::Milliseconds(20U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());

        mp::RoundedRectStyle style{};
        style.radius = 4U;
        style.stroke = kBackground;
        style.stroke_width = 1U;
        for (auto& node : nodes_) {
            node = scene_.CreateRoundedRect({}, style).value();
            node.SetVisible(false);
        }
        debris_ = scene_.CreateRoundedRect({}, style).value();
        debris_.SetVisible(false);

        score_ = scene_.CreateLabel({width_ / 2, 22}, "", kText, mp::SystemFont::kTitle, true).value();
        title_ = scene_.CreateLabel({width_ / 2, height_ * 34 / 100}, "", kText, mp::SystemFont::kTitle, true).value();
        hint_ = scene_.CreateLabel({width_ / 2, height_ * 50 / 100}, "", kDim, mp::SystemFont::kMedium, true).value();

        best_ = app_.storage().GetU32Or("best", 0U);
        Reset();
        title_.SetText("Gotchi Stack");
        hint_.SetText("Tap to drop the block");
        Draw();
    }

    mp::EventResult Handle(const mp::Event& event) {
        const uint64_t now = app_.clock().Now().microseconds();
        if (const auto* tick = event.TimerFrom(frame_)) {
            Step(static_cast<float>(tick->delta().count_microseconds()) * 1e-6F);
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == mp::TouchPhase::kDown) {
                Press(now);
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == mp::KeyPhase::kDown) {
                Press(now);
            }
        }
        return mp::EventResult::kContinue;
    }

   private:
    void Reset() {
        level_ = 0U;
        const int32_t base_width = width_ * 45 / 100;
        blocks_[0] = {static_cast<float>((width_ - base_width) / 2), base_width};
        speed_ = kStartSpeed;
        direction_ = 1.0F;
        perfect_streak_ = 0U;
        camera_ = 0.0F;
        NewMovingBlock();
    }

    void NewMovingBlock() {
        const Block& top = blocks_[level_ % kHistory];
        // Enter from alternating sides.
        direction_ = (level_ % 2U == 0U) ? 1.0F : -1.0F;
        moving_ = {direction_ > 0.0F ? 0.0F : static_cast<float>(width_ - top.width), top.width};
    }

    void Press(uint64_t now) {
        if (state_ == State::kTitle) {
            state_ = State::kPlaying;
            title_.SetText("");
            hint_.SetText("");
            return;
        }
        if (state_ == State::kOver) {
            if (now - over_us_ >= kRetryDelayUs) {
                Reset();
                state_ = State::kPlaying;
                title_.SetText("");
                hint_.SetText("");
            }
            return;
        }
        Drop(now);
    }

    void Drop(uint64_t now) {
        const Block& top = blocks_[level_ % kHistory];
        const int32_t moving_x = static_cast<int32_t>(moving_.x);
        const int32_t top_x = static_cast<int32_t>(top.x);
        int32_t left = math::Max(moving_x, top_x);
        int32_t right = math::Min(moving_x + moving_.width, top_x + top.width);
        if (right <= left) {
            Fall(moving_x, moving_.width);
            GameOver(now);
            return;
        }
        Block placed{};
        if (math::Abs(static_cast<float>(moving_x - top_x)) <= static_cast<float>(kPerfectPx)) {
            placed = {top.x, top.width};
            ++perfect_streak_;
            // Every third perfect drop in a row grows the block back a little.
            if (perfect_streak_ % 3U == 0U) {
                placed.width = math::Min(placed.width + 10, width_ * 45 / 100);
                placed.x = math::Max(0.0F, placed.x - 5.0F);
            }
            hint_.SetText("PERFECT!");
            hint_.SetColor(kGold);
            (void)tones_.Play(gotchi_stack_sfx::kPerfect);
        } else {
            perfect_streak_ = 0U;
            placed = {static_cast<float>(left), right - left};
            // The cut-off piece falls away.
            if (moving_x < left) {
                Fall(moving_x, left - moving_x);
            } else {
                Fall(right, moving_x + moving_.width - right);
            }
            hint_.SetText("");
            (void)tones_.Play(gotchi_stack_sfx::kDrop);
        }
        ++level_;
        blocks_[level_ % kHistory] = placed;
        speed_ = math::Min(speed_ + kSpeedPerLevel, kMaxSpeed);
        NewMovingBlock();
    }

    void Fall(int32_t x, int32_t width) {
        debris_x_ = x;
        debris_w_ = width;
        debris_y_ = static_cast<float>(BlockY(level_ + 1U));
        debris_vy_ = 0.0F;
        debris_color_ = kPalette[(level_ + 1U) % kPaletteSize];
        debris_live_ = true;
    }

    void GameOver(uint64_t now) {
        state_ = State::kOver;
        over_us_ = now;
        (void)tones_.Play(gotchi_stack_sfx::kFail);
        const bool record = level_ > best_;
        if (record) {
            best_ = level_;
            (void)app_.storage().SetU32("best", best_);
        }
        char line[32] = {};
        Format(line, record ? "New best! " : "Best ", best_);
        title_.SetText("Tower fell!");
        hint_.SetText(line);
        hint_.SetColor(kDim);
    }

    // Screen y of the block at `level`, after the camera has risen.
    [[nodiscard]] int32_t BlockY(uint32_t level) const {
        return height_ - 8 - kBlockH * static_cast<int32_t>(level + 1U) + static_cast<int32_t>(camera_);
    }

    void Step(float dt) {
        dt = math::Clamp(dt, 0.0F, 0.05F);
        if (state_ == State::kPlaying) {
            moving_.x += direction_ * speed_ * dt;
            const float limit = static_cast<float>(width_ - moving_.width);
            if (moving_.x <= 0.0F) {
                moving_.x = 0.0F;
                direction_ = 1.0F;
            } else if (moving_.x >= limit) {
                moving_.x = limit;
                direction_ = -1.0F;
            }
        }
        // Keep the moving block about a third of the way down the screen.
        const float target = math::Max(0.0F, static_cast<float>(kBlockH * static_cast<int32_t>(level_ + 2U) -
                                                                height_ * 62 / 100));
        camera_ += (target - camera_) * math::Min(1.0F, dt * 6.0F);
        if (debris_live_) {
            debris_vy_ += kDebrisFall * dt;
            debris_y_ += debris_vy_ * dt;
            debris_live_ = debris_y_ < static_cast<float>(height_);
        }
        Draw();
    }

    void Draw() {
        // Node 0 is the moving block; the rest show the top of the tower.
        const bool show_moving = state_ == State::kPlaying;
        nodes_[0].SetVisible(show_moving);
        if (show_moving) {
            nodes_[0].SetRect({static_cast<int32_t>(moving_.x), BlockY(level_ + 1U), moving_.width, kBlockH});
            nodes_[0].SetFillColor(kPalette[(level_ + 1U) % kPaletteSize]);
        }
        for (uint32_t slot = 1U; slot < kVisible; ++slot) {
            const uint32_t back = slot - 1U;
            const bool exists = back <= level_;
            const uint32_t level = exists ? level_ - back : 0U;
            const int32_t y = BlockY(level);
            const bool visible = exists && y < height_;
            nodes_[slot].SetVisible(visible);
            if (visible) {
                const Block& block = blocks_[level % kHistory];
                nodes_[slot].SetRect({static_cast<int32_t>(block.x), y, block.width, kBlockH});
                nodes_[slot].SetFillColor(kPalette[level % kPaletteSize]);
            }
        }
        debris_.SetVisible(debris_live_);
        if (debris_live_) {
            debris_.SetRect({debris_x_, static_cast<int32_t>(debris_y_), debris_w_, kBlockH});
            debris_.SetFillColor(debris_color_);
        }
        char text[12] = {};
        Format(text, "", level_);
        score_.SetText(state_ == State::kTitle ? "" : text);
        (void)app_.renderer().Present(scene_);
    }

    template <size_t N>
    static void Format(char (&out)[N], const char* prefix, uint32_t value) {
        size_t length = 0U;
        while (prefix[length] != '\0' && length + 11U < N) {
            out[length] = prefix[length];
            ++length;
        }
        char digits[10] = {};
        size_t count = 0U;
        do {
            digits[count++] = static_cast<char>('0' + value % 10U);
            value /= 10U;
        } while (value != 0U && count < sizeof(digits));
        while (count > 0U) {
            out[length++] = digits[--count];
        }
        out[length] = '\0';
    }

    mp::Application& app_;
    mp::ToneSequencer<4U> tones_;
    mp::RendererInfo info_;
    mp::Scene scene_;
    mp::Timer frame_;
    mp::RoundedRectNode nodes_[kVisible];
    mp::RoundedRectNode debris_;
    mp::LabelNode score_;
    mp::LabelNode title_;
    mp::LabelNode hint_;
    int32_t width_{};
    int32_t height_{};

    State state_{State::kTitle};
    Block blocks_[kHistory];
    Block moving_{};
    uint32_t level_{};
    uint32_t best_{};
    uint32_t perfect_streak_{};
    float speed_{};
    float direction_{1.0F};
    float camera_{};
    uint64_t over_us_{};

    bool debris_live_{};
    int32_t debris_x_{};
    int32_t debris_w_{};
    float debris_y_{};
    float debris_vy_{};
    Color debris_color_{kBackground};
};

}  // namespace

int main() {
    micropixel::Application app;
    GotchiStack game(app);
    app.log().Info("Gotchi Stack ready");
    app.Run([&game](const micropixel::Event& event) { return game.Handle(event); });
    return 0;
}
