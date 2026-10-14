// SPDX-License-Identifier: Apache-2.0
//
// Reaction Duel: a two-player reflex game on one Gotchi. Player 1 uses VOL-
// (or taps the left half), player 2 uses VOL+ (or taps the right half). Wait
// for the screen to turn green, then press first. Pressing early gives the
// point to the other player. First to five wins. The fastest reaction ever
// is saved on the device.
#include <stdint.h>

#include <span>

#include "reaction-duel_sfx_profiles.hpp"
#include "sdk/micropixel.hpp"

namespace {

namespace mp = micropixel;
namespace math = micropixel::math;
using mp::Color;

constexpr Color kIdle1 = Color::Rgb(70U, 40U, 16U);
constexpr Color kIdle2 = Color::Rgb(40U, 58U, 16U);
constexpr Color kWait = Color::Rgb(90U, 18U, 24U);
constexpr Color kGo = Color::Rgb(40U, 190U, 90U);
constexpr Color kWinner = Color::Rgb(190U, 140U, 10U);
constexpr Color kText = Color::Rgb(240U, 242U, 246U);
constexpr Color kDim = Color::Rgb(200U, 204U, 215U);

constexpr uint32_t kWinningScore = 5U;
constexpr uint64_t kMs = 1000U;
constexpr uint64_t kMinWaitUs = 1500U * kMs;
constexpr uint32_t kExtraWaitMs = 3000U;
constexpr uint64_t kTooSlowUs = 3000U * kMs;
constexpr uint64_t kResultUs = 1600U * kMs;
constexpr uint64_t kRestartDelayUs = 1000U * kMs;

enum class State : uint8_t { kTitle, kWait, kGo, kResult, kMatchOver };

class ReactionDuel final {
   public:
    explicit ReactionDuel(mp::Application& app)
        : app_(app),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(Color::Black()).value()),
          frame_(app.timers().Every(mp::Duration::Milliseconds(10U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        const int32_t half = width_ / 2;
        halves_[0] = scene_.CreateShape({0, 0, half, height_}, kIdle1).value();
        halves_[1] = scene_.CreateShape({half, 0, width_ - half, height_}, kIdle2).value();
        (void)scene_.CreateShape({half - 1, 0, 2, height_}, Color::Black()).value();

        for (uint32_t player = 0U; player < 2U; ++player) {
            const int32_t center = player == 0U ? half / 2 : half + half / 2;
            scores_[player] =
                scene_.CreateLabel({center, height_ * 20 / 100}, "0", kText, mp::SystemFont::kTitle, true).value();
            (void)scene_
                .CreateLabel({center, height_ - 18}, player == 0U ? "P1  VOL-" : "P2  VOL+", kDim,
                             mp::SystemFont::kSmall, true)
                .value();
        }
        message_ = scene_.CreateLabel({half, height_ * 48 / 100}, "", kText, mp::SystemFont::kTitle, true).value();
        detail_ = scene_.CreateLabel({half, height_ * 66 / 100}, "", kText, mp::SystemFont::kMedium, true).value();

        best_ms_ = app_.storage().GetU32Or("best_ms", 0U);
        Show("Reaction Duel", "Tap to start");
        Paint(kIdle1, kIdle2);
    }

    mp::EventResult Handle(const mp::Event& event) {
        const uint64_t now = app_.clock().Now().microseconds();
        if (event.TimerFrom(frame_) != nullptr) {
            Tick(now);
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == mp::TouchPhase::kDown) {
                Press(touch->position().x < width_ / 2 ? 0U : 1U, now);
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == mp::KeyPhase::kDown) {
                if (key->code() == mp::KeyCode::kLeft) {
                    Press(0U, now);
                } else if (key->code() == mp::KeyCode::kRight) {
                    Press(1U, now);
                }
            }
        }
        return mp::EventResult::kContinue;
    }

   private:
    void StartRound(uint64_t now) {
        state_ = State::kWait;
        go_at_us_ = now + kMinWaitUs + static_cast<uint64_t>(app_.random().Below(kExtraWaitMs)) * kMs;
        Show("Wait...", "");
        Paint(kWait, kWait);
    }

    void Press(uint32_t player, uint64_t now) {
        switch (state_) {
            case State::kTitle:
                StartRound(now);
                break;
            case State::kWait:
                // Too early: the other player scores.
                (void)tones_.Play(reaction_duel_sfx::kFoul);
                Award(1U - player, now, player == 0U ? "P1 too early!" : "P2 too early!", "");
                break;
            case State::kGo: {
                const uint32_t ms = static_cast<uint32_t>((now - go_at_us_) / kMs);
                const bool record = best_ms_ == 0U || ms < best_ms_;
                if (record) {
                    best_ms_ = ms;
                    (void)app_.storage().SetU32("best_ms", best_ms_);
                }
                char detail[40] = {};
                size_t length = Append(detail, 0U, "");
                length = AppendNumber(detail, length, ms);
                length = Append(detail, length, record ? " ms  new record!" : " ms");
                (void)length;
                (void)tones_.Play(reaction_duel_sfx::kPoint);
                Award(player, now, player == 0U ? "P1 wins!" : "P2 wins!", detail);
                break;
            }
            case State::kResult:
                break;
            case State::kMatchOver:
                if (now - result_us_ >= kRestartDelayUs) {
                    points_[0] = points_[1] = 0U;
                    UpdateScores();
                    StartRound(now);
                }
                break;
        }
    }

    void Award(uint32_t player, uint64_t now, const char* message, const char* detail) {
        ++points_[player];
        UpdateScores();
        result_us_ = now;
        if (points_[player] >= kWinningScore) {
            state_ = State::kMatchOver;
            (void)tones_.Play(reaction_duel_sfx::kWin);
            Show(player == 0U ? "P1 champion!" : "P2 champion!", "Tap to play again");
        } else {
            state_ = State::kResult;
            Show(message, detail);
        }
        Paint(player == 0U ? kWinner : kIdle1, player == 1U ? kWinner : kIdle2);
    }

    void Tick(uint64_t now) {
        if (state_ == State::kWait && now >= go_at_us_) {
            state_ = State::kGo;
            (void)tones_.Play(reaction_duel_sfx::kGo);
            Show("GO!", "");
            Paint(kGo, kGo);
        } else if (state_ == State::kGo && now - go_at_us_ >= kTooSlowUs) {
            state_ = State::kResult;
            result_us_ = now;
            Show("Too slow!", "No point");
            Paint(kIdle1, kIdle2);
        } else if (state_ == State::kResult && now - result_us_ >= kResultUs) {
            StartRound(now);
        }
    }

    void UpdateScores() {
        for (uint32_t player = 0U; player < 2U; ++player) {
            char text[12] = {};
            (void)AppendNumber(text, 0U, points_[player]);
            scores_[player].SetText(text);
        }
    }

    void Show(const char* message, const char* detail) {
        message_.SetText(message);
        detail_.SetText(detail);
        (void)app_.renderer().Present(scene_);
    }

    void Paint(Color left, Color right) {
        halves_[0].SetColor(left);
        halves_[1].SetColor(right);
        message_.SetColor(left == kGo ? Color::Black() : kText);
        (void)app_.renderer().Present(scene_);
    }

    template <size_t N>
    static size_t Append(char (&out)[N], size_t length, const char* text) {
        while (*text != '\0' && length + 1U < N) {
            out[length++] = *text++;
        }
        out[length] = '\0';
        return length;
    }

    template <size_t N>
    static size_t AppendNumber(char (&out)[N], size_t length, uint32_t value) {
        char digits[10] = {};
        size_t count = 0U;
        do {
            digits[count++] = static_cast<char>('0' + value % 10U);
            value /= 10U;
        } while (value != 0U && count < sizeof(digits));
        while (count > 0U && length + 1U < N) {
            out[length++] = digits[--count];
        }
        out[length] = '\0';
        return length;
    }

    mp::Application& app_;
    mp::ToneSequencer<4U> tones_;
    mp::RendererInfo info_;
    mp::Scene scene_;
    mp::Timer frame_;
    mp::ShapeNode halves_[2];
    mp::LabelNode scores_[2];
    mp::LabelNode message_;
    mp::LabelNode detail_;
    int32_t width_{};
    int32_t height_{};

    State state_{State::kTitle};
    uint32_t points_[2]{};
    uint32_t best_ms_{};
    uint64_t go_at_us_{};
    uint64_t result_us_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    ReactionDuel game(app);
    app.log().Info("Reaction Duel ready");
    app.Run([&game](const micropixel::Event& event) { return game.Handle(event); });
    return 0;
}
