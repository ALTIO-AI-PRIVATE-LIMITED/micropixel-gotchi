// SPDX-License-Identifier: Apache-2.0
//
// Gotchi Flap: a one-touch flying game. Tap anywhere (or press any key) to
// flap the little Gotchi up through the gaps between the pipes. Each pipe you
// pass scores a point; touching a pipe, the floor or the ceiling ends the run.
// The best score is saved on the device.
#include <stdint.h>

#include <span>

#include "gotchi-flap_sfx_profiles.hpp"
#include "sdk/micropixel.hpp"

namespace {

namespace mp = micropixel;
namespace math = micropixel::math;
using mp::Color;
using mp::Rect;

constexpr Color kSky = Color::Rgb(18U, 22U, 40U);
constexpr Color kPipe = Color::Rgb(72U, 200U, 120U);
constexpr Color kPipeEdge = Color::Rgb(40U, 140U, 80U);
constexpr Color kGround = Color::Rgb(46U, 40U, 60U);
constexpr Color kBody = Color::Rgb(255U, 150U, 40U);
constexpr Color kEye = Color::Rgb(20U, 20U, 28U);
constexpr Color kText = Color::Rgb(236U, 238U, 242U);
constexpr Color kDim = Color::Rgb(150U, 156U, 175U);

constexpr uint32_t kPipeCount = 3U;
constexpr int32_t kPipeWidth = 38;
constexpr int32_t kGroundHeight = 16;
constexpr int32_t kBirdW = 26;
constexpr int32_t kBirdH = 22;
constexpr float kGravity = 900.0F;      // px/s^2
constexpr float kFlapSpeed = -270.0F;   // px/s
constexpr float kMaxFall = 420.0F;
constexpr float kStartSpeed = 105.0F;   // pipe scroll, px/s
constexpr float kSpeedPerPoint = 2.0F;
constexpr float kMaxSpeed = 170.0F;
constexpr uint64_t kRetryDelayUs = 700'000U;

enum class State : uint8_t { kTitle, kPlaying, kOver };

struct Pipe {
    float x{};
    int32_t gap_y{};  // top of the gap
    bool scored{};
};

class GotchiFlap final {
   public:
    explicit GotchiFlap(mp::Application& app)
        : app_(app),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kSky).value()),
          frame_(app.timers().Every(mp::Duration::Milliseconds(20U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        play_height_ = height_ - kGroundHeight;
        gap_ = height_ * 38 / 100;
        spacing_ = static_cast<float>(width_) * 0.55F + static_cast<float>(kPipeWidth);

        mp::RoundedRectStyle pipe{};
        pipe.fill = kPipe;
        pipe.stroke = kPipeEdge;
        pipe.stroke_width = 3U;
        pipe.radius = 6U;
        for (uint32_t index = 0U; index < kPipeCount; ++index) {
            tops_[index] = scene_.CreateRoundedRect({}, pipe).value();
            bottoms_[index] = scene_.CreateRoundedRect({}, pipe).value();
        }
        (void)scene_.CreateShape({0, play_height_, width_, kGroundHeight}, kGround).value();

        mp::RoundedRectStyle body{};
        body.fill = kBody;
        body.stroke = Color::White();
        body.stroke_width = 2U;
        body.radius = 9U;
        bird_ = scene_.CreateRoundedRect({}, body).value();
        mp::RoundedRectStyle eye{};
        eye.fill = kEye;
        eye.radius = 2U;
        for (auto& node : bird_eyes_) {
            node = scene_.CreateRoundedRect({}, eye).value();
        }

        score_ = scene_.CreateLabel({width_ / 2, 22}, "", kText, mp::SystemFont::kTitle, true).value();
        title_ = scene_.CreateLabel({width_ / 2, height_ * 34 / 100}, "", kText, mp::SystemFont::kTitle, true).value();
        hint_ = scene_.CreateLabel({width_ / 2, height_ * 62 / 100}, "", kDim, mp::SystemFont::kMedium, true).value();

        best_ = app_.storage().GetU32Or("best", 0U);
        Reset();
        ShowTitle("Gotchi Flap", "Tap to flap");
        Draw();
    }

    mp::EventResult Handle(const mp::Event& event) {
        const uint64_t now = app_.clock().Now().microseconds();
        if (const auto* tick = event.TimerFrom(frame_)) {
            Step(now, static_cast<float>(tick->delta().count_microseconds()) * 1e-6F);
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
        bird_y_ = static_cast<float>(play_height_) * 0.45F;
        bird_vy_ = 0.0F;
        speed_ = kStartSpeed;
        points_ = 0U;
        for (uint32_t index = 0U; index < kPipeCount; ++index) {
            pipes_[index] = {static_cast<float>(width_) + 40.0F + spacing_ * static_cast<float>(index), RandomGap(),
                             false};
        }
    }

    [[nodiscard]] int32_t RandomGap() const {
        const int32_t margin = 18;
        const int32_t range = play_height_ - gap_ - margin * 2;
        return margin + static_cast<int32_t>(app_.random().Below(static_cast<uint32_t>(math::Max(range, 1))));
    }

    void Press(uint64_t now) {
        switch (state_) {
            case State::kTitle:
                state_ = State::kPlaying;
                ShowTitle("", "");
                Flap();
                break;
            case State::kPlaying:
                Flap();
                break;
            case State::kOver:
                if (now - over_us_ >= kRetryDelayUs) {
                    Reset();
                    state_ = State::kPlaying;
                    ShowTitle("", "");
                    Flap();
                }
                break;
        }
    }

    void Flap() {
        bird_vy_ = kFlapSpeed;
        (void)tones_.Play(gotchi_flap_sfx::kFlap);
    }

    void Step(uint64_t now, float dt) {
        dt = math::Clamp(dt, 0.0F, 0.05F);
        if (state_ == State::kTitle) {
            // Hover gently on the title screen.
            bird_y_ = static_cast<float>(play_height_) * 0.45F +
                      math::Sin(static_cast<float>(now % 2'000'000U) * 1e-6F * math::kPi) * 8.0F;
        } else if (state_ == State::kPlaying) {
            bird_vy_ = math::Min(bird_vy_ + kGravity * dt, kMaxFall);
            bird_y_ += bird_vy_ * dt;
            for (auto& pipe : pipes_) {
                pipe.x -= speed_ * dt;
                if (pipe.x + static_cast<float>(kPipeWidth) < 0.0F) {
                    float farthest = 0.0F;
                    for (const auto& other : pipes_) {
                        farthest = math::Max(farthest, other.x);
                    }
                    pipe = {farthest + spacing_, RandomGap(), false};
                }
                if (!pipe.scored && pipe.x + static_cast<float>(kPipeWidth) < static_cast<float>(BirdX())) {
                    pipe.scored = true;
                    ++points_;
                    speed_ = math::Min(speed_ + kSpeedPerPoint, kMaxSpeed);
                    (void)tones_.Play(gotchi_flap_sfx::kPoint);
                }
            }
            if (Collides()) {
                GameOver(now);
            }
        }
        Draw();
    }

    [[nodiscard]] int32_t BirdX() const { return width_ / 4; }

    [[nodiscard]] bool Collides() const {
        const int32_t y = static_cast<int32_t>(bird_y_);
        if (y < 0 || y + kBirdH > play_height_) {
            return true;
        }
        // A slightly smaller hit box than the drawing feels fair.
        const Rect bird{BirdX() + 3, y + 3, kBirdW - 6, kBirdH - 6};
        for (const auto& pipe : pipes_) {
            const int32_t x = static_cast<int32_t>(pipe.x);
            const Rect top{x, 0, kPipeWidth, pipe.gap_y};
            const Rect bottom{x, pipe.gap_y + gap_, kPipeWidth, play_height_ - pipe.gap_y - gap_};
            if (bird.intersects(top) || bird.intersects(bottom)) {
                return true;
            }
        }
        return false;
    }

    void GameOver(uint64_t now) {
        state_ = State::kOver;
        over_us_ = now;
        (void)tones_.Play(gotchi_flap_sfx::kCrash);
        const bool record = points_ > best_;
        if (record) {
            best_ = points_;
            (void)app_.storage().SetU32("best", best_);
        }
        char line[32] = {};
        Format(line, record ? "New best! " : "Best ", best_);
        ShowTitle("Game over", line);
    }

    void ShowTitle(const char* title, const char* hint) {
        title_.SetText(title);
        hint_.SetText(hint);
    }

    void Draw() {
        for (uint32_t index = 0U; index < kPipeCount; ++index) {
            const auto& pipe = pipes_[index];
            const int32_t x = static_cast<int32_t>(pipe.x);
            // Pipes run past the screen edge so only their gap ends look rounded.
            tops_[index].SetRect({x, -10, kPipeWidth, pipe.gap_y + 10});
            bottoms_[index].SetRect({x, pipe.gap_y + gap_, kPipeWidth, play_height_ - pipe.gap_y - gap_ + 10});
        }
        const int32_t x = BirdX();
        const int32_t y = static_cast<int32_t>(bird_y_);
        // Squash a little while rising, stretch while falling.
        const int32_t squash = state_ == State::kPlaying ? math::Clamp(static_cast<int32_t>(bird_vy_ / 100.0F), -2, 2) : 0;
        bird_.SetRect({x, y + squash, kBirdW, kBirdH - squash});
        const int32_t eye_h = state_ == State::kOver ? 2 : 7;
        bird_eyes_[0].SetRect({x + 12, y + 6, 4, eye_h});
        bird_eyes_[1].SetRect({x + 19, y + 6, 4, eye_h});

        char text[12] = {};
        Format(text, "", points_);
        score_.SetText(state_ == State::kTitle ? "" : text);
        (void)app_.renderer().Present(scene_);
    }

    // Writes prefix followed by value into a 32-byte (or smaller) buffer.
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
    mp::RoundedRectNode tops_[kPipeCount];
    mp::RoundedRectNode bottoms_[kPipeCount];
    mp::RoundedRectNode bird_;
    mp::RoundedRectNode bird_eyes_[2];
    mp::LabelNode score_;
    mp::LabelNode title_;
    mp::LabelNode hint_;
    int32_t width_{};
    int32_t height_{};
    int32_t play_height_{};
    int32_t gap_{};
    float spacing_{};

    State state_{State::kTitle};
    Pipe pipes_[kPipeCount];
    float bird_y_{};
    float bird_vy_{};
    float speed_{};
    uint32_t points_{};
    uint32_t best_{};
    uint64_t over_us_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    GotchiFlap game(app);
    app.log().Info("Gotchi Flap ready");
    app.Run([&game](const micropixel::Event& event) { return game.Handle(event); });
    return 0;
}
