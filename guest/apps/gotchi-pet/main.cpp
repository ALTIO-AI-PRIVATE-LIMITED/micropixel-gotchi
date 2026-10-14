// SPDX-License-Identifier: Apache-2.0
//
// Gotchi Pet: a desk pet face for the CGotchi. It blinks and glances around,
// its eyes follow the tilt of the device, it smiles when tapped, eats on VOL+,
// plays on VOL-, gets dizzy when shaken and falls asleep when left alone.
// Food and joy drain slowly while the App runs and are saved between runs.
#include <stdint.h>

#include <span>

#include "gotchi-pet_sfx_profiles.hpp"
#include "sdk/micropixel.hpp"

namespace {

namespace mp = micropixel;
namespace math = micropixel::math;
using mp::Color;
using mp::Rect;

constexpr Color kBackground = Color::Rgb(8U, 8U, 12U);
constexpr Color kEye = Color::Rgb(120U, 236U, 255U);
constexpr Color kHungryEye = Color::Rgb(255U, 170U, 60U);
constexpr Color kCheek = Color::Rgb(255U, 92U, 170U);
constexpr Color kText = Color::Rgb(236U, 238U, 242U);
constexpr Color kTrack = Color::Rgb(40U, 44U, 56U);
constexpr Color kFood = Color::Rgb(255U, 150U, 40U);
constexpr Color kJoy = Color::Rgb(255U, 92U, 170U);

constexpr uint32_t kMaxStat = 100U;
constexpr uint32_t kHungryBelow = 25U;
constexpr uint64_t kSecond = 1'000'000U;
constexpr uint64_t kFoodDecayUs = 36U * kSecond;  // full to empty in one hour of play
constexpr uint64_t kJoyDecayUs = 24U * kSecond;
constexpr uint64_t kSleepyAfterUs = 45U * kSecond;
constexpr uint64_t kSleepAfterUs = 60U * kSecond;
constexpr uint64_t kSaveEveryUs = 30U * kSecond;
constexpr uint64_t kMessageUs = 2500U * 1000U;
constexpr uint64_t kBlinkUs = 160U * 1000U;

// Acceleration change between two readings, m/s^2 (sum of axes).
constexpr float kMoveDelta = 2.5F;   // picked up or nudged: wakes the pet
constexpr float kShakeDelta = 14.0F;  // one hard jolt; three in a row is a shake
constexpr uint64_t kShakeWindowUs = 700U * 1000U;

enum class Mood : uint8_t { kIdle, kHappy, kEating, kPlaying, kDizzy, kSleeping };

float Phase(uint64_t elapsed_us, float hertz) {
    return static_cast<float>(elapsed_us % (10U * kSecond)) * 1e-6F * hertz * math::kTwoPi;
}

class GotchiPet final {
   public:
    explicit GotchiPet(mp::Application& app)
        : app_(app),
          tones_(app.audio()),
          info_(app.renderer().info()),
          scene_(app.renderer().CreateScene(kBackground).value()),
          frame_(app.timers().Every(mp::Duration::Milliseconds(33U)).value()) {
        width_ = static_cast<int32_t>(info_.width());
        height_ = static_cast<int32_t>(info_.height());
        cx_ = width_ / 2;
        cy_ = height_ / 2 - 6;

        mp::RoundedRectStyle eye{};
        eye.fill = kEye;
        for (auto& node : eyes_) {
            node = scene_.CreateRoundedRect({}, eye).value();
        }
        mp::RoundedRectStyle cheek{};
        cheek.fill = kCheek;
        cheek.opacity = 150U;
        for (auto& node : cheeks_) {
            node = scene_.CreateRoundedRect({}, cheek).value();
            node.SetVisible(false);
        }
        mouth_ = scene_.CreateRoundedRect({}, eye).value();
        zzz_ = scene_.CreateLabel({}, "z Z z", kText, mp::SystemFont::kLarge, true).value();
        zzz_.SetVisible(false);
        message_ = scene_.CreateLabel({cx_, 16}, "", kText, mp::SystemFont::kMedium, true).value();

        const int32_t bar_width = width_ * 30 / 100;
        const int32_t bar_y = height_ - 18;
        mp::RoundedRectStyle track{};
        track.fill = kTrack;
        track.radius = 4U;
        food_track_ = {cx_ - bar_width - 10, bar_y, bar_width, 8};
        joy_track_ = {cx_ + 10, bar_y, bar_width, 8};
        (void)scene_.CreateRoundedRect(food_track_, track).value();
        (void)scene_.CreateRoundedRect(joy_track_, track).value();
        mp::RoundedRectStyle fill = track;
        fill.fill = kFood;
        food_bar_ = scene_.CreateRoundedRect(food_track_, fill).value();
        fill.fill = kJoy;
        joy_bar_ = scene_.CreateRoundedRect(joy_track_, fill).value();

        if (auto sensor = app_.sensors().OpenFirst<mp::Acceleration>(app_.devices(), mp::Duration::Milliseconds(20U))) {
            accelerometer_ = std::move(*sensor);
        }

        food_ = math::Min(app_.storage().GetU32Or("food", 80U), kMaxStat);
        joy_ = math::Min(app_.storage().GetU32Or("joy", 80U), kMaxStat);
        const uint64_t now = Now();
        Restart(now);
        Say(food_ < kHungryBelow ? "I'm hungry! Press VOL+" : "Tap me! VOL+ feed, VOL- play", now);
        Frame(now);
    }

    mp::EventResult Handle(const mp::Event& event) {
        const uint64_t now = Now();
        if (event.TimerFrom(frame_) != nullptr) {
            Frame(now);
        } else if (const auto* touch = event.touch()) {
            if (touch->phase() == mp::TouchPhase::kDown) {
                Pet(now);
            }
        } else if (const auto* key = event.key()) {
            if (key->phase() == mp::KeyPhase::kDown) {
                if (key->code() == mp::KeyCode::kRight) {
                    Feed(now);
                } else if (key->code() == mp::KeyCode::kLeft) {
                    Play(now);
                } else {
                    Pet(now);
                }
            }
        } else if (event.type() == mp::EventType::kResume) {
            Restart(now);
        } else if (event.type() == mp::EventType::kStop) {
            Save(now);
        }
        return mp::EventResult::kContinue;
    }

   private:
    [[nodiscard]] uint64_t Now() const { return app_.clock().Now().microseconds(); }

    // Stats only drain while the App is on screen, so time away costs nothing.
    void Restart(uint64_t now) {
        food_tick_us_ = joy_tick_us_ = save_tick_us_ = now;
        last_touch_us_ = now;
        next_blink_us_ = now + kSecond;
        next_glance_us_ = now + 2U * kSecond;
        tilt_.Recalibrate();
    }

    // ---- interactions ------------------------------------------------------

    // Any input or movement. Returns true when it woke the pet, which then
    // swallows the input so the first tap only wakes it.
    bool Touched(uint64_t now) {
        last_touch_us_ = now;
        if (mood_ != Mood::kSleeping) {
            return false;
        }
        SetMood(Mood::kIdle, now, 0U);
        Sound(gotchi_pet_sfx::kWake);
        Say("Good morning!", now);
        return true;
    }

    void Pet(uint64_t now) {
        if (Touched(now)) {
            return;
        }
        Add(joy_, 8);
        SetMood(Mood::kHappy, now, 1500U);
        Sound(gotchi_pet_sfx::kChirp);
        Say("Hehe!", now);
    }

    void Feed(uint64_t now) {
        if (Touched(now)) {
            return;
        }
        if (food_ >= 95U) {
            SetMood(Mood::kHappy, now, 1000U);
            Sound(gotchi_pet_sfx::kChirp);
            Say("I'm full!", now);
            return;
        }
        Add(food_, 25);
        SetMood(Mood::kEating, now, 1600U);
        Sound(gotchi_pet_sfx::kMunch);
        Say("Yum!", now);
    }

    void Play(uint64_t now) {
        if (Touched(now)) {
            return;
        }
        if (food_ < 10U) {
            Sound(gotchi_pet_sfx::kSad);
            Say("Too hungry to play", now);
            return;
        }
        Add(joy_, 20);
        Add(food_, -3);
        SetMood(Mood::kPlaying, now, 1400U);
        Sound(gotchi_pet_sfx::kChirp);
        Say("Wheee!", now);
    }

    void Shaken(uint64_t now) {
        Touched(now);
        Add(joy_, -5);
        SetMood(Mood::kDizzy, now, 2500U);
        Sound(gotchi_pet_sfx::kDizzy);
        Say("Whoa... dizzy", now);
    }

    void SetMood(Mood mood, uint64_t now, uint32_t milliseconds) {
        mood_ = mood;
        mood_started_us_ = now;
        mood_until_us_ = now + static_cast<uint64_t>(milliseconds) * 1000U;
    }

    void Add(uint32_t& stat, int32_t amount) {
        const int32_t value = math::Clamp(static_cast<int32_t>(stat) + amount, 0, static_cast<int32_t>(kMaxStat));
        stat = static_cast<uint32_t>(value);
        dirty_ = true;
    }

    // ---- per-frame update --------------------------------------------------

    void ReadMotion(uint64_t now) {
        if (!accelerometer_.valid()) {
            return;
        }
        auto sample = accelerometer_.Read();
        if (!sample || !tilt_.Sample(sample->value, sample->timestamp)) {
            return;
        }
        const auto a = sample->value.meters_per_second_squared;
        if (has_last_accel_) {
            const float delta =
                math::Abs(a.x - last_accel_.x) + math::Abs(a.y - last_accel_.y) + math::Abs(a.z - last_accel_.z);
            if (delta > kShakeDelta) {
                shake_hits_ = now - last_jolt_us_ < kShakeWindowUs ? shake_hits_ + 1U : 1U;
                last_jolt_us_ = now;
                if (shake_hits_ >= 3U && mood_ != Mood::kDizzy) {
                    shake_hits_ = 0U;
                    Shaken(now);
                }
            } else if (delta > kMoveDelta) {
                Touched(now);
            }
        }
        last_accel_ = a;
        has_last_accel_ = true;
    }

    void Tick(uint64_t now) {
        const uint32_t food_before = food_;
        while (now - food_tick_us_ >= kFoodDecayUs) {
            food_tick_us_ += kFoodDecayUs;
            Add(food_, -1);
        }
        while (now - joy_tick_us_ >= kJoyDecayUs) {
            joy_tick_us_ += kJoyDecayUs;
            Add(joy_, food_ < kHungryBelow ? -2 : -1);
        }
        if (food_before >= kHungryBelow && food_ < kHungryBelow) {
            Sound(gotchi_pet_sfx::kSad);
            Say("I'm hungry! Press VOL+", now);
        }
        if (mood_ != Mood::kIdle && mood_ != Mood::kSleeping && now >= mood_until_us_) {
            mood_ = Mood::kIdle;
        }
        if (mood_ == Mood::kIdle && now - last_touch_us_ >= kSleepAfterUs) {
            SetMood(Mood::kSleeping, now, 0U);
            Say("", now);
        }
        if (message_until_us_ != 0U && now >= message_until_us_) {
            message_until_us_ = 0U;
            message_.SetText("");
        }
        if (dirty_ && now - save_tick_us_ >= kSaveEveryUs) {
            Save(now);
        }
        if (now >= next_blink_us_) {
            blink_started_us_ = now;
            next_blink_us_ = now + kSecond + static_cast<uint64_t>(app_.random().Below(4000U)) * 1000U;
        }
        if (now >= next_glance_us_) {
            glance_target_x_ = static_cast<float>(app_.random().Below(29U)) - 14.0F;
            glance_target_y_ = static_cast<float>(app_.random().Below(13U)) - 6.0F;
            next_glance_us_ = now + 2U * kSecond + static_cast<uint64_t>(app_.random().Below(4000U)) * 1000U;
        }
        glance_x_ += (glance_target_x_ - glance_x_) * 0.2F;
        glance_y_ += (glance_target_y_ - glance_y_) * 0.2F;
    }

    void Frame(uint64_t now) {
        ReadMotion(now);
        Tick(now);
        Draw(now);
        (void)app_.renderer().Present(scene_);
    }

    // ---- drawing -----------------------------------------------------------

    void Draw(uint64_t now) {
        const uint64_t t = now - mood_started_us_;
        const bool hungry = food_ < kHungryBelow;
        const bool sleepy = mood_ == Mood::kIdle && now - last_touch_us_ >= kSleepyAfterUs;

        // Eyes look where the device tilts plus an occasional glance.
        float look_x = glance_x_;
        float look_y = glance_y_;
        if (tilt_.calibrated()) {
            look_x += tilt_.x() * 22.0F;
            look_y += tilt_.y() * 14.0F;
        }

        int32_t eye_w = 60;
        int32_t eye_h = 80;
        int32_t face_dy = 0;
        int32_t mouth_w = 24;
        int32_t mouth_h = 8;
        int32_t mouth_dy = 0;
        bool show_cheeks = false;
        bool show_mouth = true;
        bool show_zzz = false;
        float left_dx = 0.0F;
        float left_dy = 0.0F;
        float right_dx = 0.0F;
        float right_dy = 0.0F;

        // Blink: a quick close and open, skipped while the eyes are not open.
        float open = 1.0F;
        if (now - blink_started_us_ < kBlinkUs) {
            const float half = static_cast<float>(kBlinkUs) * 0.5F;
            open = math::Abs(static_cast<float>(now - blink_started_us_) - half) / half;
        }

        switch (mood_) {
            case Mood::kIdle:
                eye_h = static_cast<int32_t>(80.0F * open * (sleepy ? 0.4F : 1.0F) * (hungry ? 0.7F : 1.0F));
                if (hungry) {
                    face_dy = 8;
                    mouth_w = 18;
                    mouth_h = 6;
                }
                break;
            case Mood::kHappy:
                eye_h = 22;
                face_dy = -static_cast<int32_t>(math::Abs(math::Sin(Phase(t, 2.0F))) * 8.0F);
                mouth_w = 56;
                mouth_h = 20;
                show_cheeks = true;
                look_x = look_y = 0.0F;
                break;
            case Mood::kEating:
                eye_h = 30;
                mouth_w = 40;
                mouth_h = 6 + static_cast<int32_t>(math::Abs(math::Sin(Phase(t, 2.5F))) * 24.0F);
                show_cheeks = true;
                look_x = look_y = 0.0F;
                break;
            case Mood::kPlaying:
                eye_h = static_cast<int32_t>(80.0F * open);
                face_dy = -static_cast<int32_t>(math::Abs(math::Sin(Phase(t, 2.0F))) * 24.0F);
                mouth_w = 26;
                mouth_h = 26;
                break;
            case Mood::kDizzy: {
                eye_w = eye_h = 40;
                const float angle = Phase(t, 1.5F);
                left_dx = math::Cos(angle) * 12.0F;
                left_dy = math::Sin(angle) * 12.0F;
                right_dx = -left_dx;
                right_dy = -left_dy;
                mouth_w = 30 + static_cast<int32_t>(math::Sin(Phase(t, 3.0F)) * 8.0F);
                mouth_h = 10;
                look_x = look_y = 0.0F;
                break;
            }
            case Mood::kSleeping: {
                eye_h = 6;
                face_dy = 10 + static_cast<int32_t>(math::Sin(Phase(now, 0.25F)) * 3.0F);
                show_mouth = false;
                show_zzz = true;
                look_x = look_y = 0.0F;
                const int32_t float_up = static_cast<int32_t>((now / 1000U) % 2000U) / 50;
                zzz_.SetPosition({cx_ + 70, cy_ - 50 - float_up});
                break;
            }
        }
        eye_h = math::Max(eye_h, 6);

        const Color eye_color = hungry ? kHungryEye : kEye;
        const int32_t eye_cy = cy_ - 14 + face_dy + static_cast<int32_t>(look_y);
        const int32_t eye_gap = 56;
        const float dx[2] = {left_dx, right_dx};
        const float dy[2] = {left_dy, right_dy};
        for (uint32_t index = 0U; index < 2U; ++index) {
            const int32_t center_x =
                cx_ + (index == 0U ? -eye_gap : eye_gap) + static_cast<int32_t>(look_x + dx[index]);
            const int32_t center_y = eye_cy + static_cast<int32_t>(dy[index]);
            eyes_[index].SetRect({center_x - eye_w / 2, center_y - eye_h / 2, eye_w, eye_h});
            eyes_[index].SetRadius(static_cast<uint32_t>(math::Min(eye_w, eye_h) / 2));
            eyes_[index].SetFillColor(eye_color);
            cheeks_[index].SetRect({center_x - 18 + (index == 0U ? -10 : 10), cy_ + 34 + face_dy, 36, 14});
            cheeks_[index].SetRadius(7U);
            cheeks_[index].SetVisible(show_cheeks);
        }
        const int32_t mouth_y = cy_ + 50 + face_dy + mouth_dy + static_cast<int32_t>(look_y * 0.5F);
        mouth_.SetRect({cx_ - mouth_w / 2 + static_cast<int32_t>(look_x * 0.5F), mouth_y - mouth_h / 2, mouth_w, mouth_h});
        mouth_.SetRadius(static_cast<uint32_t>(math::Min(mouth_w, mouth_h) / 2));
        mouth_.SetFillColor(eye_color);
        mouth_.SetVisible(show_mouth);
        zzz_.SetVisible(show_zzz);

        food_bar_.SetRect(BarRect(food_track_, food_));
        joy_bar_.SetRect(BarRect(joy_track_, joy_));
    }

    static Rect BarRect(Rect track, uint32_t value) {
        track.width = math::Max(track.height, track.width * static_cast<int32_t>(value) / static_cast<int32_t>(kMaxStat));
        return track;
    }

    void Say(const char* text, uint64_t now) {
        message_.SetText(text);
        message_until_us_ = text[0] == '\0' ? 0U : now + kMessageUs;
    }

    void Save(uint64_t now) {
        save_tick_us_ = now;
        if (!dirty_) {
            return;
        }
        const bool saved =
            app_.storage().SetU32("food", food_).has_value() && app_.storage().SetU32("joy", joy_).has_value();
        dirty_ = !saved;
    }

    void Sound(std::span<const mp::ToneSpec> effect) { (void)tones_.Play(effect); }

    mp::Application& app_;
    mp::ToneSequencer<4U> tones_;
    mp::RendererInfo info_;
    mp::Scene scene_;
    mp::Timer frame_;
    mp::Accelerometer accelerometer_;
    mp::TiltFilter tilt_;
    mp::RoundedRectNode eyes_[2];
    mp::RoundedRectNode cheeks_[2];
    mp::RoundedRectNode mouth_;
    mp::LabelNode zzz_;
    mp::LabelNode message_;
    mp::RoundedRectNode food_bar_;
    mp::RoundedRectNode joy_bar_;
    Rect food_track_{};
    Rect joy_track_{};
    int32_t width_{};
    int32_t height_{};
    int32_t cx_{};
    int32_t cy_{};

    Mood mood_{Mood::kIdle};
    uint32_t food_{};
    uint32_t joy_{};
    bool dirty_{};
    uint64_t mood_started_us_{};
    uint64_t mood_until_us_{};
    uint64_t last_touch_us_{};
    uint64_t food_tick_us_{};
    uint64_t joy_tick_us_{};
    uint64_t save_tick_us_{};
    uint64_t message_until_us_{};
    uint64_t blink_started_us_{};
    uint64_t next_blink_us_{};
    uint64_t next_glance_us_{};
    float glance_x_{};
    float glance_y_{};
    float glance_target_x_{};
    float glance_target_y_{};

    mp::Vector3 last_accel_{};
    bool has_last_accel_{};
    uint64_t last_jolt_us_{};
    uint32_t shake_hits_{};
};

}  // namespace

int main() {
    micropixel::Application app;
    GotchiPet pet(app);
    app.log().Info("Gotchi Pet ready");
    app.Run([&pet](const micropixel::Event& event) { return pet.Handle(event); });
    return 0;
}
