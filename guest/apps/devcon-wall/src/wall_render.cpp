// SPDX-License-Identifier: Apache-2.0
//
// The show, in canvas coordinates (kCanvasWidth x kCanvasHeight, origin top
// left of spot 1). Every Gotchi evaluates the same scene for the same time
// and keeps only the pixels inside its own tile.
//
// Look: Devcon 8 India's hero art. Sunset-to-indigo sky with stars, a glowing
// cyan moon inside a rotating teal mandala, floating Ether crystals, then the
// Devcon VIII India logo on a soft light plate and the dates.
//
//   0.0- 2.5 s  fade in, stars
//   0.0- 7.0 s  moon rises to the centre spot
//   4.5- 9.0 s  mandala blooms around the moon; crystals drift up throughout
//   9.0-12.0 s  two light rings sweep out from the centre across all nine
//  11.0-14.6 s  logo plate fades in and the logo opens from the centre
//  15.2-16.6 s  "MUMBAI · 3–6 NOV 2026" wipes in
//  19.5-21.5 s  shimmer sweeps across the logo
//  30.0-32.0 s  fade out, loop
#include "wall_render.hpp"

#include "brand_data.hpp"

namespace devcon_wall {
namespace {

constexpr int32_t kCenterX = kCanvasWidth / 2;
constexpr int32_t kCenterY = kCanvasHeight / 2;
constexpr float kPi = 3.14159265F;

struct Rgb final {
    int32_t r, g, b;
};

constexpr Rgb kSkyTop{214, 128, 104};
constexpr Rgb kSkyMid{150, 92, 196};
constexpr Rgb kSkyLow{70, 52, 150};
constexpr Rgb kSkyBottom{28, 24, 80};
constexpr Rgb kMoon{172, 252, 246};
constexpr Rgb kMoonShade{128, 214, 222};
constexpr Rgb kMoonGlow{90, 210, 220};
constexpr Rgb kMandala{120, 232, 205};
constexpr Rgb kPlate{250, 238, 255};
constexpr Rgb kOrange{245, 160, 60};

// Ether crystal faces: top-left, top-right, bottom-left, bottom-right.
constexpr Rgb kPinkFaces[4] = {{252, 230, 255}, {222, 182, 252}, {186, 150, 244}, {140, 112, 226}};
constexpr Rgb kTealFaces[4] = {{226, 255, 244}, {164, 246, 228}, {112, 218, 214}, {78, 170, 204}};

constexpr int32_t kMoonRadius = 118;
constexpr int32_t kMoonGlowRadius = 220;
constexpr int32_t kLogoCenterY = kCenterY - 20;
constexpr int32_t kLogoX = kCenterX - brand::kLogoWidth / 2;
constexpr int32_t kLogoY = kLogoCenterY - brand::kLogoHeight / 2;
constexpr int32_t kTaglineX = kCenterX - brand::kTaglineWidth / 2;
constexpr int32_t kTaglineY = kLogoY + brand::kLogoHeight + 22;
constexpr int32_t kPlateRadiusX = 440;
constexpr int32_t kPlateRadiusY = 215;

float Clamp01(float value) { return value < 0.0F ? 0.0F : (value > 1.0F ? 1.0F : value); }
float Smooth(float from, float to, float s) {
    const float x = Clamp01((s - from) / (to - from));
    return x * x * (3.0F - 2.0F * x);
}
float EaseOut(float x) {
    x = Clamp01(x);
    return 1.0F - (1.0F - x) * (1.0F - x) * (1.0F - x);
}

uint32_t Hash(uint32_t x) {
    x ^= x >> 16U;
    x *= 0x7FEB352DU;
    x ^= x >> 15U;
    x *= 0x846CA68BU;
    x ^= x >> 16U;
    return x;
}

constexpr uint16_t Pack(int32_t r, int32_t g, int32_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
inline Rgb Unpack(uint16_t c) {
    const int32_t r = (c >> 11) & 0x1F;
    const int32_t g = (c >> 5) & 0x3F;
    const int32_t b = c & 0x1F;
    return Rgb{(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}
inline int32_t Min255(int32_t v) { return v > 255 ? 255 : v; }

inline void Blend(uint16_t& dst, Rgb c, int32_t alpha) {
    if (alpha <= 0) return;
    if (alpha >= 255) {
        dst = Pack(c.r, c.g, c.b);
        return;
    }
    const Rgb d = Unpack(dst);
    dst = Pack(d.r + ((c.r - d.r) * alpha >> 8), d.g + ((c.g - d.g) * alpha >> 8), d.b + ((c.b - d.b) * alpha >> 8));
}
inline void Add(uint16_t& dst, Rgb c, int32_t alpha) {
    if (alpha <= 0) return;
    const Rgb d = Unpack(dst);
    dst = Pack(Min255(d.r + (c.r * alpha >> 8)), Min255(d.g + (c.g * alpha >> 8)), Min255(d.b + (c.b * alpha >> 8)));
}

Rgb Lerp(Rgb a, Rgb b, float t) {
    return Rgb{a.r + static_cast<int32_t>(static_cast<float>(b.r - a.r) * t),
               a.g + static_cast<int32_t>(static_cast<float>(b.g - a.g) * t),
               a.b + static_cast<int32_t>(static_cast<float>(b.b - a.b) * t)};
}

// Sine table, 1024 steps per turn.
constexpr int32_t kSineSteps = 1024;
float g_sine[kSineSteps];
// Integer sine, Q14, same steps.
int16_t g_sine_q14[kSineSteps];
bool g_sine_ready = false;
void EnsureSine() {
    if (g_sine_ready) return;
    // Rotate a unit vector by one step at a time (no libm in the Guest).
    constexpr double kStepCos = 0.9999811752826011;
    constexpr double kStepSin = 0.006135884649154475;
    double c = 1.0;
    double sn = 0.0;
    for (int32_t i = 0; i < kSineSteps; ++i) {
        g_sine[i] = static_cast<float>(sn);
        g_sine_q14[i] = static_cast<int16_t>(sn * 16383.0);
        const double next_c = c * kStepCos - sn * kStepSin;
        sn = sn * kStepCos + c * kStepSin;
        c = next_c;
    }
    g_sine_ready = true;
}
inline float Sin(float radians) {
    const int32_t i = static_cast<int32_t>(radians * (kSineSteps / (2.0F * kPi))) & (kSineSteps - 1);
    return g_sine[i];
}
inline float Cos(float radians) { return Sin(radians + kPi * 0.5F); }

// A tile seen as canvas coordinates. With `shift` 1 it is a half-resolution
// buffer (148x120): loops walk the canvas in steps of 2 and At() maps each
// step to one buffer pixel. The background layers render that way, then get
// doubled; the logo and text render at full resolution.
class Frame final {
   public:
    Frame(const Tile& tile, uint16_t* pixels, int32_t pitch, int32_t shift)
        : x0_(tile.origin_x), y0_(tile.origin_y), pixels_(pixels), pitch_(pitch), shift_(shift) {}

    [[nodiscard]] bool Inside(int32_t x, int32_t y) const {
        return x >= x0_ && y >= y0_ && x < x0_ + kTileWidth && y < y0_ + kTileHeight;
    }
    uint16_t& At(int32_t x, int32_t y) { return pixels_[((y - y0_) >> shift_) * pitch_ + ((x - x0_) >> shift_)]; }
    // Clip a canvas span to this tile and snap its start to the sample grid;
    // false when nothing is left.
    bool ClipRows(int32_t& top, int32_t& bottom) const {
        if (top < y0_) top = y0_;
        if (bottom > y0_ + kTileHeight) bottom = y0_ + kTileHeight;
        top = Snap(top, y0_);
        return top < bottom;
    }
    bool ClipColumns(int32_t& left, int32_t& right) const {
        if (left < x0_) left = x0_;
        if (right > x0_ + kTileWidth) right = x0_ + kTileWidth;
        left = Snap(left, x0_);
        return left < right;
    }
    void Dot(int32_t x, int32_t y, int32_t size, Rgb c, int32_t alpha) {
        size = size >> shift_ > 0 ? size >> shift_ : 1;
        const int32_t step = Step();
        for (int32_t dy = 0; dy < size; ++dy)
            for (int32_t dx = 0; dx < size; ++dx) {
                const int32_t px = x + dx * step;
                const int32_t py = y + dy * step;
                if (Inside(px, py)) Add(At(px, py), c, alpha);
            }
    }
    [[nodiscard]] int32_t x0() const { return x0_; }
    [[nodiscard]] int32_t y0() const { return y0_; }
    [[nodiscard]] int32_t Step() const { return 1 << shift_; }

   private:
    [[nodiscard]] int32_t Snap(int32_t v, int32_t origin) const {
        const int32_t mask = Step() - 1;
        return origin + (((v - origin) + mask) & ~mask);
    }
    int32_t x0_, y0_;
    uint16_t* pixels_;
    int32_t pitch_;
    int32_t shift_;
};

void DrawSky(Frame& f) {
    for (int32_t y = f.y0(); y < f.y0() + kTileHeight; y += f.Step()) {
        const float v = static_cast<float>(y) / kCanvasHeight;
        Rgb c;
        if (v < 0.38F) {
            c = Lerp(kSkyTop, kSkyMid, v / 0.38F);
        } else if (v < 0.75F) {
            c = Lerp(kSkyMid, kSkyLow, (v - 0.38F) / 0.37F);
        } else {
            c = Lerp(kSkyLow, kSkyBottom, (v - 0.75F) / 0.25F);
        }
        // 4x4 ordered dither hides RGB565 banding in the long gradient.
        constexpr int32_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        uint16_t packed[4];
        for (int32_t i = 0; i < 4; ++i) {
            const int32_t d = kBayer[(y >> 1) & 3][i] >> 1;
            packed[i] = Pack(Min255(c.r + d), Min255(c.g + (d >> 1)), Min255(c.b + d));
        }
        uint16_t* row = &f.At(f.x0(), y);
        const int32_t count = kTileWidth / f.Step();
        for (int32_t x = 0; x < count; ++x) row[x] = packed[x & 3];
    }
}

void DrawStars(Frame& f, float s) {
    constexpr uint32_t kStars = 320U;
    for (uint32_t i = 0; i < kStars; ++i) {
        const uint32_t h = Hash(i * 2654435761U + 17U);
        const int32_t x = static_cast<int32_t>(h % kCanvasWidth);
        const int32_t y = static_cast<int32_t>((h >> 10) % (kCanvasHeight * 85 / 100));
        if (x < f.x0() - 2 || x > f.x0() + kTileWidth + 2 || y < f.y0() - 2 || y > f.y0() + kTileHeight + 2) continue;
        const float speed = 1.2F + static_cast<float>((h >> 20) & 7U) * 0.45F;
        const float twinkle = 0.55F + 0.45F * Sin(s * speed + static_cast<float>(h & 1023U));
        const int32_t alpha = static_cast<int32_t>(static_cast<float>(90 + ((h >> 24) & 127U)) * twinkle);
        const Rgb white{255, 250, 240};
        f.Dot(x, y, 1, white, alpha);
        if (i % 11U == 0U) {
            f.Dot(x - 1, y, 1, white, alpha / 2);
            f.Dot(x + 1, y, 1, white, alpha / 2);
            f.Dot(x, y - 1, 1, white, alpha / 2);
            f.Dot(x, y + 1, 1, white, alpha / 2);
        }
    }
}

// One mandala curve: r(θ) = radius + bulge·|sin(petals·θ/2)|, rotated.
void MandalaCurve(Frame& f, float scale, float rotation, float radius, float bulge, int32_t petals, int32_t alpha,
                  int32_t dot) {
    const float outer = (radius + bulge) * scale;
    if (outer < 4.0F) return;
    if (kCenterX + outer < f.x0() || kCenterX - outer > f.x0() + kTileWidth || kCenterY + outer < f.y0() ||
        kCenterY - outer > f.y0() + kTileHeight)
        return;
    // Spaced so the curve reads as a dotted line, like the hero art.
    const int32_t samples = static_cast<int32_t>(outer * 3.6F) / f.Step() + 32;
    const int32_t base = static_cast<int32_t>(radius * scale * 16.0F);  // Q4
    const int32_t swell = static_cast<int32_t>(bulge * scale * 16.0F);
    const int32_t turn = static_cast<int32_t>(rotation * (kSineSteps / (2.0F * kPi)));
    // Angle in Q16 steps of the 1024-step table.
    const int32_t delta = (kSineSteps << 16) / samples;
    for (int32_t i = 0; i < samples; ++i) {
        const int32_t angle = (i * delta) >> 16;
        int32_t wave = g_sine_q14[(angle * petals / 2) & (kSineSteps - 1)];
        if (wave < 0) wave = -wave;
        const int32_t r = base + (swell * wave >> 14);  // Q4
        const int32_t a = angle + turn;
        const int32_t x = kCenterX + ((r * g_sine_q14[(a + kSineSteps / 4) & (kSineSteps - 1)]) >> 18);
        const int32_t y = kCenterY + ((r * g_sine_q14[a & (kSineSteps - 1)]) >> 18);
        f.Dot(x, y, dot, kMandala, alpha);
    }
}

void DrawMandala(Frame& f, float s) {
    const float bloom = Smooth(4.5F, 9.0F, s);
    if (bloom <= 0.0F) return;
    const float scale = 0.35F + 0.65F * EaseOut(bloom);
    const int32_t alpha = static_cast<int32_t>(150.0F * bloom);
    const float spin = s * 0.10F;
    MandalaCurve(f, scale, spin, 150.0F, 0.0F, 1, alpha, 2);
    MandalaCurve(f, scale, spin, 150.0F, 72.0F, 16, alpha, 2);
    MandalaCurve(f, scale, -spin + kPi / 8.0F, 150.0F, 72.0F, 16, alpha / 2, 1);
    MandalaCurve(f, scale, -spin, 228.0F, 58.0F, 24, alpha, 2);
    MandalaCurve(f, scale, spin * 1.5F, 298.0F, 0.0F, 1, alpha * 2 / 3, 1);
    MandalaCurve(f, scale, spin * 0.7F, 310.0F, 70.0F, 32, alpha, 2);
    MandalaCurve(f, scale, -spin * 0.7F + kPi / 16.0F, 310.0F, 70.0F, 32, alpha / 2, 1);
    // Ring of beads.
    const float bead_radius = 400.0F * scale;
    for (int32_t i = 0; i < 48; ++i) {
        const float theta = static_cast<float>(i) * (2.0F * kPi / 48.0F) - spin;
        f.Dot(kCenterX + static_cast<int32_t>(bead_radius * Cos(theta)) - 2,
              kCenterY + static_cast<int32_t>(bead_radius * Sin(theta)) - 2, 4, kMandala, alpha);
    }
}

void DrawMoon(Frame& f, float s) {
    const float rise = EaseOut(s / 7.0F);
    const int32_t cy = kCenterY + static_cast<int32_t>((1.0F - rise) * static_cast<float>(kCanvasHeight / 2 + 260));
    const int32_t cx = kCenterX;
    int32_t top = cy - kMoonGlowRadius;
    int32_t bottom = cy + kMoonGlowRadius;
    if (!f.ClipRows(top, bottom)) return;
    constexpr int32_t kR2 = kMoonRadius * kMoonRadius;
    constexpr int32_t kG2 = kMoonGlowRadius * kMoonGlowRadius;
    // Craters, relative to the centre: x, y, radius.
    constexpr int32_t kCraters[5][3] = {{-40, -30, 26}, {30, 18, 34}, {-18, 52, 18}, {48, -48, 14}, {-62, 22, 12}};
    constexpr int32_t kLimbScale = 60 * 65536 / kR2;
    constexpr int32_t kGlowScale = 255 * 65536 / (kG2 - kR2);
    const int32_t step = f.Step();
    for (int32_t y = top; y < bottom; y += step) {
        const int32_t dy = y - cy;
        const int32_t span = static_cast<int32_t>(__builtin_sqrtf(static_cast<float>(kG2 - dy * dy)));
        int32_t left = cx - span;
        int32_t right = cx + span + 1;
        if (!f.ClipColumns(left, right)) continue;
        for (int32_t x = left; x < right; x += step) {
            const int32_t dx = x - cx;
            const int32_t d2 = dx * dx + dy * dy;
            uint16_t& p = f.At(x, y);
            if (d2 < kR2) {
                const int32_t limb = d2 * kLimbScale >> 16;
                Rgb c{kMoon.r - limb, kMoon.g - limb / 2, kMoon.b - limb / 3};
                for (const auto& crater : kCraters) {
                    const int32_t ex = dx - crater[0];
                    const int32_t ey = dy - crater[1];
                    if (ex * ex + ey * ey < crater[2] * crater[2]) {
                        c = Rgb{(c.r + kMoonShade.r) >> 1, (c.g + kMoonShade.g) >> 1, (c.b + kMoonShade.b) >> 1};
                        break;
                    }
                }
                Blend(p, c, 255);
            } else if (d2 < kG2) {
                const int32_t k = (kG2 - d2) * kGlowScale >> 16;
                Add(p, kMoonGlow, k * k / 255 * 3 / 4);
            }
        }
    }
}

void DrawCrystal(Frame& f, int32_t cx, int32_t cy, int32_t half_width, int32_t up, int32_t down, const Rgb* faces,
                 int32_t alpha) {
    int32_t top = cy - up;
    int32_t bottom = cy + down;
    if (cx + half_width < f.x0() || cx - half_width >= f.x0() + kTileWidth || !f.ClipRows(top, bottom)) return;
    const int32_t step = f.Step();
    for (int32_t y = top; y < bottom; y += step) {
        const bool upper = y < cy;
        const int32_t dy = upper ? cy - y : y - cy;
        const int32_t h = upper ? up : down;
        const int32_t span = half_width * (h - dy) / h;
        int32_t left = cx - span;
        int32_t right = cx + span;
        if (!f.ClipColumns(left, right)) continue;
        const Rgb& lc = faces[upper ? 0 : 2];
        const Rgb& rc = faces[upper ? 1 : 3];
        for (int32_t x = left; x < right; x += step) Blend(f.At(x, y), x < cx ? lc : rc, alpha);
    }
}

void DrawCrystals(Frame& f, float s) {
    const float appear = Smooth(1.5F, 4.0F, s);
    if (appear <= 0.0F) return;
    constexpr int32_t kCount = 14;
    for (int32_t i = 0; i < kCount; ++i) {
        const uint32_t h = Hash(static_cast<uint32_t>(i) * 7919U + 3U);
        // Keep the big ones to the sides so the moon and logo stay clear.
        const int32_t lane = i % 2 == 0 ? static_cast<int32_t>(h % 230U) : kCanvasWidth - static_cast<int32_t>(h % 230U);
        const int32_t size = 18 + static_cast<int32_t>((h >> 8) % 42U);
        const float speed = 14.0F + static_cast<float>((h >> 16) % 22U);
        const int32_t travel = kCanvasHeight + 280;
        const int32_t rise = static_cast<int32_t>(s * speed) + static_cast<int32_t>((h >> 4) % static_cast<uint32_t>(travel));
        const int32_t cy = kCanvasHeight + 140 - rise % travel;
        const int32_t cx = lane + static_cast<int32_t>(18.0F * Sin(s * 0.6F + static_cast<float>(i)));
        DrawCrystal(f, cx, cy, size * 6 / 10, size, size * 11 / 10, (h & 1U) ? kPinkFaces : kTealFaces,
                    static_cast<int32_t>(225.0F * appear));
    }
}

void DrawRing(Frame& f, float radius, int32_t alpha) {
    constexpr int32_t kBand = 16;
    const int32_t r = static_cast<int32_t>(radius);
    const int32_t outer = r + kBand;
    const int32_t inner = r > kBand ? r - kBand : 0;
    int32_t top = kCenterY - outer;
    int32_t bottom = kCenterY + outer + 1;
    if (!f.ClipRows(top, bottom)) return;
    const int32_t r2 = r * r;
    const int32_t band2 = outer * outer - r2;
    const int32_t inv = 65536 / (band2 > 0 ? band2 : 1) * 256;  // (256 << 16) / band2, roughly
    const Rgb light{200, 255, 250};
    const int32_t step = f.Step();
    for (int32_t y = top; y < bottom; y += step) {
        const int32_t dy = y - kCenterY;
        const int32_t out2 = outer * outer - dy * dy;
        if (out2 <= 0) continue;
        const int32_t in2 = inner * inner - dy * dy;
        const int32_t out_span = static_cast<int32_t>(__builtin_sqrtf(static_cast<float>(out2)));
        const int32_t in_span = in2 > 0 ? static_cast<int32_t>(__builtin_sqrtf(static_cast<float>(in2))) : 0;
        // Two arcs: left and right of the hollow middle.
        for (int32_t side = -1; side <= 1; side += 2) {
            int32_t a = kCenterX + (side < 0 ? -out_span : in_span);
            int32_t b = kCenterX + (side < 0 ? -in_span : out_span + 1);
            if (!f.ClipColumns(a, b)) continue;
            for (int32_t x = a; x < b; x += step) {
                const int32_t dx = x - kCenterX;
                int32_t diff = dx * dx + dy * dy - r2;
                if (diff < 0) diff = -diff;
                const int32_t k = 256 - static_cast<int32_t>((static_cast<int64_t>(diff) * inv) >> 16);
                if (k <= 0) continue;
                Add(f.At(x, y), light, k * k * alpha >> 16);
            }
        }
    }
}

void DrawRipples(Frame& f, float s) {
    for (int32_t k = 0; k < 2; ++k) {
        const float age = (s - 9.0F - 1.4F * static_cast<float>(k)) / 2.6F;
        if (age <= 0.0F || age >= 1.0F) continue;
        DrawRing(f, age * 780.0F, static_cast<int32_t>((1.0F - age) * 220.0F));
    }
}

void DrawPlate(Frame& f, float amount) {
    if (amount <= 0.0F) return;
    int32_t top = kLogoCenterY - kPlateRadiusY;
    int32_t bottom = kLogoCenterY + kPlateRadiusY;
    if (!f.ClipRows(top, bottom)) return;
    const int32_t peak = static_cast<int32_t>(205.0F * amount);
    // q = 256 at the ellipse edge, 0 at its centre: dx²·kQx + dy²·kQy, >> 16.
    constexpr int32_t kQx = (256 << 16) / (kPlateRadiusX * kPlateRadiusX);
    constexpr int32_t kQy = (256 << 16) / (kPlateRadiusY * kPlateRadiusY);
    const int32_t step = f.Step();
    for (int32_t y = top; y < bottom; y += step) {
        const int32_t dy = y - kLogoCenterY;
        const int32_t qy = dy * dy * kQy;
        const float ry = 1.0F - static_cast<float>(dy * dy) / static_cast<float>(kPlateRadiusY * kPlateRadiusY);
        if (ry <= 0.0F) continue;
        const int32_t span = static_cast<int32_t>(static_cast<float>(kPlateRadiusX) * __builtin_sqrtf(ry));
        int32_t left = kCenterX - span;
        int32_t right = kCenterX + span;
        if (!f.ClipColumns(left, right)) continue;
        for (int32_t x = left; x < right; x += step) {
            const int32_t dx = x - kCenterX;
            const int32_t q = (dx * dx * kQx + qy) >> 16;
            int32_t a = (256 - q) * 3;
            if (a > 256) a = 256;
            Blend(f.At(x, y), kPlate, peak * a >> 8);
        }
    }
}

// Blits a run-length image with its top left at (ox, oy). `visible(x, y)`
// gates each pixel; `shimmer(x, y)` returns 0..255 extra light.
template <typename Visible, typename Shimmer>
void DrawImage(Frame& f, int32_t ox, int32_t oy, int32_t width, int32_t height, const uint16_t* colors,
               const uint8_t* alphas, const uint32_t* offsets, const uint8_t* runs, int32_t fade, bool plain,
               Visible visible, Shimmer shimmer) {
    int32_t top = oy;
    int32_t bottom = oy + height;
    if (ox + width < f.x0() || ox >= f.x0() + kTileWidth || !f.ClipRows(top, bottom)) return;
    const Rgb white{255, 255, 255};
    for (int32_t y = top; y < bottom; ++y) {
        const int32_t row = y - oy;
        int32_t x = ox;
        for (uint32_t i = offsets[row]; i < offsets[row + 1]; i += 2U) {
            const uint8_t index = runs[i];
            const int32_t length = runs[i + 1U];
            if (index != 0U) {
                int32_t a = x;
                int32_t b = x + length;
                if (f.ClipColumns(a, b)) {
                    const Rgb c = Unpack(colors[index]);
                    const int32_t alpha = alphas[index] * fade >> 8;
                    if (plain && alpha >= 255) {
                        uint16_t* p = &f.At(a, y);
                        for (int32_t n = b - a; n > 0; --n) *p++ = colors[index];
                        x += length;
                        continue;
                    }
                    for (int32_t px = a; px < b; ++px) {
                        if (!visible(px, y)) continue;
                        uint16_t& p = f.At(px, y);
                        Blend(p, c, alpha);
                        const int32_t glow = shimmer(px, y);
                        if (glow > 0) Add(p, white, glow * alpha >> 8);
                    }
                }
            }
            x += length;
        }
    }
}

void DrawLogo(Frame& f, float s) {
    const float open = Smooth(11.6F, 14.6F, s);
    if (open <= 0.0F) return;
    const float reveal = 30.0F + 540.0F * open;
    const int32_t reveal2 = static_cast<int32_t>(reveal * reveal);
    const bool full = open >= 1.0F;
    const float sweep = (s - 19.5F) / 2.0F;
    const bool shimmering = sweep > 0.0F && sweep < 1.0F;
    const int32_t band = static_cast<int32_t>(sweep * static_cast<float>(kCanvasWidth + kCanvasHeight + 300)) - 150;
    DrawImage(
        f, kLogoX, kLogoY, brand::kLogoWidth, brand::kLogoHeight, brand::kLogoColor, brand::kLogoAlpha,
        brand::kLogoRowOffset, brand::kLogoRuns, 256, full && !shimmering,
        [&](int32_t x, int32_t y) {
            if (full) return true;
            const int32_t dx = x - kCenterX;
            const int32_t dy = y - kLogoCenterY;
            return dx * dx + dy * dy < reveal2;
        },
        [&](int32_t x, int32_t y) {
            if (!shimmering) return 0;
            const int32_t d = x + y - band;
            if (d < -40 || d > 40) return 0;
            return (40 - (d < 0 ? -d : d)) * 4;
        });
}

void DrawTagline(Frame& f, float s) {
    const float wipe = Smooth(15.2F, 16.6F, s);
    if (wipe <= 0.0F) return;
    const int32_t edge = kTaglineX + static_cast<int32_t>(wipe * static_cast<float>(brand::kTaglineWidth + 40));
    DrawImage(
        f, kTaglineX, kTaglineY, brand::kTaglineWidth, brand::kTaglineHeight, brand::kTaglineColor,
        brand::kTaglineAlpha, brand::kTaglineRowOffset, brand::kTaglineRuns, 256, false,
        [&](int32_t x, int32_t) { return x < edge; },
        [&](int32_t x, int32_t) {
            const int32_t d = edge - x;
            return d >= 0 && d < 40 ? (40 - d) * 5 : 0;
        });
}

void Dim(uint16_t* pixels, int32_t pitch, float level) {
    const int32_t k = static_cast<int32_t>(Clamp01(level) * 256.0F);
    if (k >= 256) return;
    for (int32_t y = 0; y < kTileHeight; ++y) {
        uint16_t* row = pixels + y * pitch;
        for (int32_t x = 0; x < kTileWidth; ++x) {
            const Rgb c = Unpack(row[x]);
            row[x] = Pack(c.r * k >> 8, c.g * k >> 8, c.b * k >> 8);
        }
    }
}

void FillRect(uint16_t* pixels, int32_t pitch, int32_t x, int32_t y, int32_t w, int32_t h, Rgb c, int32_t alpha) {
    for (int32_t yy = y; yy < y + h; ++yy)
        for (int32_t xx = x; xx < x + w; ++xx) Blend(pixels[yy * pitch + xx], c, alpha);
}

}  // namespace

void RenderTile(const Tile& tile, uint32_t time_ms, uint16_t* pixels, int32_t pitch_pixels) {
    EnsureSine();
    const float s = static_cast<float>(time_ms % kLoopMs) / 1000.0F;
    // Background at half resolution: a quarter of the pixel work.
    constexpr int32_t kHalfWidth = kTileWidth / 2;
    constexpr int32_t kHalfHeight = kTileHeight / 2;
    static uint16_t half[kHalfWidth * kHalfHeight];
    Frame back(tile, half, kHalfWidth, 1);
    DrawSky(back);
    DrawStars(back, s);
    DrawMandala(back, s);
    DrawMoon(back, s);
    DrawCrystals(back, s);
    DrawRipples(back, s);
    DrawPlate(back, Smooth(11.0F, 12.5F, s));
    for (int32_t y = 0; y < kHalfHeight; ++y) {
        const uint16_t* src = half + y * kHalfWidth;
        uint16_t* a = pixels + (2 * y) * pitch_pixels;
        uint16_t* b = a + pitch_pixels;
        for (int32_t x = 0; x < kHalfWidth; ++x) {
            const uint16_t c = src[x];
            a[2 * x] = c;
            a[2 * x + 1] = c;
            b[2 * x] = c;
            b[2 * x + 1] = c;
        }
    }
    Frame front(tile, pixels, pitch_pixels, 0);
    DrawLogo(front, s);
    DrawTagline(front, s);
    Dim(pixels, pitch_pixels, Smooth(0.0F, 2.5F, s) * (1.0F - Smooth(30.0F, 32.0F, s)));
}

void RenderBadge(uint32_t spot, LinkStatus status, uint16_t* pixels, int32_t pitch_pixels) {
    constexpr int32_t kCell = 9;
    constexpr int32_t kGap = 2;
    FillRect(pixels, pitch_pixels, 4, 4, 3 * kCell + 2 * kGap + 28, 3 * kCell + 2 * kGap + 8, Rgb{10, 8, 30}, 190);
    for (uint32_t i = 1; i <= 9U; ++i) {
        const int32_t col = static_cast<int32_t>((i - 1U) % 3U);
        const int32_t row = static_cast<int32_t>((i - 1U) / 3U);
        const Rgb c = i == spot ? kOrange : (i == kLeaderSpot ? Rgb{90, 200, 190} : Rgb{90, 86, 120});
        FillRect(pixels, pitch_pixels, 8 + col * (kCell + kGap), 8 + row * (kCell + kGap), kCell, kCell, c, 255);
    }
    const Rgb dot = status == LinkStatus::kSynced
                        ? Rgb{72, 220, 120}
                        : (status == LinkStatus::kSearching ? Rgb{250, 180, 40} : Rgb{240, 72, 88});
    FillRect(pixels, pitch_pixels, 8 + 3 * kCell + 2 * kGap + 8, 8 + kCell + 1, 9, 9, dot, 255);
}

}  // namespace devcon_wall
