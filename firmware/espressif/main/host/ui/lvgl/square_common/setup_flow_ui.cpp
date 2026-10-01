#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "host/ui/lvgl/square_common/system_detail_ui.hpp"
#include "host/ui/lvgl/square_common/system_detail_ui_internal.hpp"
#include "host/ui/ui_text.hpp"
#include "platform/lvgl/fonts/system_fonts.hpp"
#include "platform/lvgl/lvgl_wakeup.hpp"

namespace micropixel::host_ui::lvgl::square_common {
namespace {
using system_detail_internal::Button;
using system_detail_internal::kTag;
using system_detail_internal::Label;

constexpr char kPairUrlPrefix[] = "https://cgotchi.com/pair?code=";
// Version 3 (29 modules) holds the pairing URL; four pixels per module.
constexpr int32_t kQrSize = 116;
constexpr int32_t kQrFrame = 6;

lv_obj_t* CenteredColumn(lv_obj_t* parent, int32_t gap) {
    lv_obj_t* column = lv_obj_create(parent);
    StyleTransparentContainer(column);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, gap, 0);
    return column;
}

lv_obj_t* WrappedLabel(lv_obj_t* parent, const char* text, platform::lvgl::SystemFontRole role, uint32_t color,
                       lv_text_align_t align = LV_TEXT_ALIGN_CENTER) {
    lv_obj_t* label = Label(parent, text, role, color);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

lv_obj_t* Badge(lv_obj_t* parent, int32_t size, uint32_t color) {
    lv_obj_t* badge = lv_obj_create(parent);
    StyleTransparentContainer(badge);
    lv_obj_set_size(badge, size, size);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    return badge;
}

// A small face: the Gotchi greeting its owner.
void DrawFace(lv_obj_t* parent, int32_t size) {
    lv_obj_t* face = Badge(parent, size, theme::kAccent);
    const int32_t eye = size / 7;
    for (const int32_t x : {-size / 5, size / 5}) {
        lv_obj_t* pupil = Badge(face, eye, 0xFFFFFF);
        lv_obj_align(pupil, LV_ALIGN_CENTER, x, -size / 10);
    }
    lv_obj_t* smile = lv_arc_create(face);
    lv_obj_remove_style(smile, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(smile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(smile, size / 2, size / 2);
    lv_arc_set_bg_angles(smile, 20, 160);
    lv_arc_set_value(smile, 0);
    lv_obj_set_style_arc_width(smile, eye / 2 + 1, LV_PART_MAIN);
    lv_obj_set_style_arc_color(smile, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(smile, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_align(smile, LV_ALIGN_CENTER, 0, -size / 12);
}

lv_obj_t* Spinner(lv_obj_t* parent, int32_t size) {
    lv_obj_t* spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, size, size);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(theme::kBorder), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(theme::kAccent), LV_PART_INDICATOR);
    return spinner;
}

lv_obj_t* SetupButton(const SystemPageLayout& layout, lv_obj_t* parent, const char* text, uint32_t color,
                      int32_t width_percent) {
    lv_obj_t* button = Button(layout, parent, text, color);
    lv_obj_set_width(button, LV_PCT(width_percent));
    return button;
}

bool SameSetupScreen(const host_ui::SetupModel& left, const host_ui::SetupModel& right) {
    return left.step == right.step && left.connect_failed == right.connect_failed && left.code == right.code &&
           left.owner_name == right.owner_name && left.wifi_name == right.wifi_name;
}

}  // namespace

std::expected<void, host_ui::SystemUiError> SystemDetailUi::ShowSetupLocked(lv_obj_t* root,
                                                                             const host_ui::SetupModel& model,
                                                                             host_ui::SystemUiActionSink action_sink,
                                                                             void* action_context) {
    if (root == nullptr) {
        return std::unexpected(host_ui::SystemUiError::kUnavailable);
    }
    ResetActiveScreen();
    root_ = root;
    setup_model_ = model;
    action_sink_ = action_sink;
    action_context_ = action_context;
    active_screen_ = Screen::kSetup;
    RenderSetupLocked();
    return {};
}

void SystemDetailUi::UpdateSetupLocked(const host_ui::SetupModel& model) {
    if (!SetupVisible()) {
        return;
    }
    const bool same_screen = SameSetupScreen(setup_model_, model);
    setup_model_ = model;
    if (same_screen && setup_expiry_label_ != nullptr) {
        SetSetupExpiryTextLocked();
        platform::lvgl::RequestDisplayRefresh(lv_obj_get_display(root_));
        return;
    }
    if (!same_screen) {
        RenderSetupLocked();
    }
}

void SystemDetailUi::LeaveSetup() {
    if (SetupVisible()) {
        setup_model_ = {};
        ResetActiveScreen();
    }
}

bool SystemDetailUi::SetupVisible() const { return active_screen_ == Screen::kSetup; }

void* SystemDetailUi::SetupActionContext() const { return action_context_; }

template <host_ui::SystemUiActionType Type>
void SystemDetailUi::SetupActionEvent(lv_event_t* event) {
    auto* ui = static_cast<SystemDetailUi*>(lv_event_get_user_data(event));
    if (ui != nullptr && ui->SetupVisible() && ui->action_sink_ != nullptr) {
        ui->action_sink_(ui->action_context_, host_ui::SystemUiAction{.type = Type});
    }
}

void SystemDetailUi::RenderSetupLocked() {
    if (!SetupVisible() || root_ == nullptr) {
        return;
    }
    setup_animation_refresh_.Stop();
    setup_expiry_label_ = nullptr;
    lv_obj_clean(root_);
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme::kMenuBackground), 0);
    lv_obj_t* page = lv_obj_create(root_);
    StyleTransparentContainer(page);
    lv_obj_set_size(page, layout_.width, layout_.height);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_pad_hor(page, layout_.safe_horizontal, 0);
    lv_obj_set_style_pad_ver(page, layout_.safe_horizontal, 0);
    switch (setup_model_.step) {
        case host_ui::SetupStep::kWelcome:
            DrawSetupWelcomeLocked(CenteredColumn(page, 8));
            break;
        case host_ui::SetupStep::kConnecting:
            DrawSetupConnectingLocked(CenteredColumn(page, 8));
            break;
        case host_ui::SetupStep::kLink:
            DrawSetupLinkLocked(page);
            break;
        case host_ui::SetupStep::kDone:
            DrawSetupDoneLocked(CenteredColumn(page, 8));
            break;
    }
    lv_obj_move_foreground(root_);
    platform::lvgl::RequestDisplayRefresh(lv_obj_get_display(root_));
}

void SystemDetailUi::DrawSetupWelcomeLocked(lv_obj_t* column) {
    DrawFace(column, 56);
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupWelcomeTitle), platform::lvgl::SystemFontRole::kTitle,
                       theme::kPrimaryText);
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupWelcomeBody), platform::lvgl::SystemFontRole::kMedium,
                       theme::kSecondaryText);
    lv_obj_t* start =
        SetupButton(layout_, column, UiText(host_strings::Id::kSetupGetStarted), theme::kAccent, 70);
    lv_obj_add_event_cb(start, SetupActionEvent<host_ui::SystemUiActionType::kSetupStart>, LV_EVENT_SHORT_CLICKED,
                        this);
}

void SystemDetailUi::DrawSetupConnectingLocked(lv_obj_t* column) {
    char wifi[64]{};
    std::snprintf(wifi, sizeof(wifi), UiText(host_strings::Id::kSetupConnectingWifi), setup_model_.wifi_name.data());
    if (!setup_model_.connect_failed) {
        (void)Spinner(column, 44);
        (void)WrappedLabel(column, UiText(host_strings::Id::kSetupConnectingTitle),
                           platform::lvgl::SystemFontRole::kTitle, theme::kPrimaryText);
        (void)WrappedLabel(column, wifi, platform::lvgl::SystemFontRole::kMedium, theme::kSecondaryText);
        setup_animation_refresh_.Start(lv_obj_get_display(root_));
        return;
    }
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupConnectFailedTitle),
                       platform::lvgl::SystemFontRole::kTitle, theme::kPrimaryText);
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupConnectFailedBody),
                       platform::lvgl::SystemFontRole::kMedium, theme::kSecondaryText);
    (void)WrappedLabel(column, wifi, platform::lvgl::SystemFontRole::kSmall, theme::kMutedText);
    lv_obj_t* retry = SetupButton(layout_, column, UiText(host_strings::Id::kSetupTryAgain), theme::kAccent, 80);
    lv_obj_add_event_cb(retry, SetupActionEvent<host_ui::SystemUiActionType::kSetupRetry>, LV_EVENT_SHORT_CLICKED,
                        this);
    lv_obj_t* change =
        SetupButton(layout_, column, UiText(host_strings::Id::kSetupChangeWifi), theme::kPrimaryText, 80);
    lv_obj_add_event_cb(change, SetupActionEvent<host_ui::SystemUiActionType::kSetupChangeWifi>,
                        LV_EVENT_SHORT_CLICKED, this);
}

void SystemDetailUi::DrawSetupLinkLocked(lv_obj_t* page) {
    // The QR code and a readable code share one row on a landscape panel.
    lv_obj_set_style_pad_hor(page, layout_.safe_horizontal * 2 / 3, 0);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(page, 10, 0);

    // A white frame keeps the code scannable on every theme.
    lv_obj_t* frame = lv_obj_create(page);
    StyleTransparentContainer(frame);
    lv_obj_set_size(frame, kQrSize + kQrFrame * 2, kQrSize + kQrFrame * 2);
    lv_obj_set_style_radius(frame, 10, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    const bool has_code = setup_model_.code[0] != '\0';
    if (has_code) {
        char url[sizeof(kPairUrlPrefix) + host_ui::kRemoteControlPairingCodeCapacity]{};
        const int length = std::snprintf(url, sizeof(url), "%s%s", kPairUrlPrefix, setup_model_.code.data());
        lv_obj_t* qr = lv_qrcode_create(frame);
        lv_qrcode_set_size(qr, kQrSize);
        lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
        lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
        if (length <= 0 || lv_qrcode_update(qr, url, static_cast<uint32_t>(length)) != LV_RESULT_OK) {
            ESP_LOGW(kTag, "pairing QR code could not be generated");
        }
        lv_obj_center(qr);
    } else {
        lv_obj_center(Spinner(frame, 40));
        setup_animation_refresh_.Start(lv_obj_get_display(root_));
    }

    lv_obj_t* column = lv_obj_create(page);
    StyleTransparentContainer(column);
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_width(column, 0);
    lv_obj_set_flex_grow(column, 1);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(column, 6, 0);
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupLinkTitle), platform::lvgl::SystemFontRole::kLarge,
                       theme::kPrimaryText, LV_TEXT_ALIGN_LEFT);
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupLinkBody), platform::lvgl::SystemFontRole::kSmall,
                       theme::kSecondaryText, LV_TEXT_ALIGN_LEFT);
    if (!has_code) {
        (void)WrappedLabel(column, UiText(host_strings::Id::kSetupGettingCode), platform::lvgl::SystemFontRole::kMedium,
                           theme::kSecondaryText, LV_TEXT_ALIGN_LEFT);
        return;
    }
    // "123456" reads more easily as "123 456".
    char spaced[host_ui::kRemoteControlPairingCodeCapacity + 2U]{};
    const size_t code_length = ::strnlen(setup_model_.code.data(), setup_model_.code.size());
    if (code_length == 6U) {
        std::snprintf(spaced, sizeof(spaced), "%.3s %.3s", setup_model_.code.data(), setup_model_.code.data() + 3);
    } else {
        std::snprintf(spaced, sizeof(spaced), "%s", setup_model_.code.data());
    }
    lv_obj_t* code = Label(column, spaced, platform::lvgl::SystemFontRole::kTitle, theme::kPositive);
    lv_obj_set_style_text_font(code, platform::lvgl::SystemDisplayFont(), 0);
    setup_expiry_label_ = Label(column, "", platform::lvgl::SystemFontRole::kSmall, theme::kMutedText);
    SetSetupExpiryTextLocked();
}

void SystemDetailUi::SetSetupExpiryTextLocked() {
    if (setup_expiry_label_ == nullptr) {
        return;
    }
    char expiry[48]{};
    std::snprintf(expiry, sizeof(expiry), UiText(host_strings::Id::kSetupCodeExpires),
                  static_cast<unsigned>(setup_model_.code_expires_seconds / 60U),
                  static_cast<unsigned>(setup_model_.code_expires_seconds % 60U));
    lv_label_set_text(setup_expiry_label_, expiry);
}

void SystemDetailUi::DrawSetupDoneLocked(lv_obj_t* column) {
    lv_obj_t* badge = Badge(column, 56, theme::kPositive);
    lv_obj_t* check = Label(badge, LV_SYMBOL_OK, platform::lvgl::SystemFontRole::kTitle, 0xFFFFFF);
    lv_obj_center(check);
    (void)WrappedLabel(column, UiText(host_strings::Id::kSetupDoneTitle), platform::lvgl::SystemFontRole::kTitle,
                       theme::kPrimaryText);
    char linked[96]{};
    if (setup_model_.owner_name[0] != '\0') {
        std::snprintf(linked, sizeof(linked), UiText(host_strings::Id::kSetupLinkedTo), setup_model_.owner_name.data());
    } else {
        std::snprintf(linked, sizeof(linked), "%s", UiText(host_strings::Id::kSetupLinked));
    }
    (void)WrappedLabel(column, linked, platform::lvgl::SystemFontRole::kMedium, theme::kSecondaryText);
    lv_obj_t* play = SetupButton(layout_, column, UiText(host_strings::Id::kSetupStartPlaying), theme::kAccent, 70);
    lv_obj_add_event_cb(play, SetupActionEvent<host_ui::SystemUiActionType::kSetupFinish>, LV_EVENT_SHORT_CLICKED,
                        this);
}

}  // namespace micropixel::host_ui::lvgl::square_common
