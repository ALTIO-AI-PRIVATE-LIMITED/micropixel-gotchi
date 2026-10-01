// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <span>

#include "esp_err.h"
#include "platform/lvgl/fonts/font_registry.hpp"

namespace micropixel::platform::lvgl {

// Initialize after lv_init(), under the adapter lock, before creating widgets.
[[nodiscard]] esp_err_t InitializeSystemFonts();
// Prepare an inactive font bank, then commit under the adapter lock. Bytes must
// outlive the bank. A failed preparation or settings commit preserves the active bank.
using CommitLanguageSetting = bool (*)(void* context);
[[nodiscard]] bool PrepareSystemLanguageFont(std::span<const uint8_t> verified_bytes);
[[nodiscard]] bool CommitSystemLanguageFont(CommitLanguageSetting commit_setting, void* context);
void AbortSystemLanguageFont();
// Boot-time convenience wrapper.
[[nodiscard]] const lv_font_t* SystemFont(SystemFontRole role, const lv_font_t* fallback);
// Large digits for codes read from across a table (5/3 of the title size). Created
// on first use; call under the adapter lock. Falls back to the title font.
[[nodiscard]] const lv_font_t* SystemDisplayFont();

}  // namespace micropixel::platform::lvgl
