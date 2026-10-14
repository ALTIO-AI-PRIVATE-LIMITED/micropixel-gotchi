#include "platform/bluetooth/ble_hid_keyboard.hpp"

#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_NIMBLE_HID_SERVICE

#include <algorithm>
#include <cstring>

#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"

extern "C" void ble_store_config_init(void);

namespace micropixel::platform::bluetooth {
namespace {

constexpr char kTag[] = "micropixel_ble_hid";

constexpr uint8_t kKeyboardReportId = 1U;
constexpr uint8_t kAppleFnReportId = 2U;
constexpr uint8_t kConsumerReportId = 3U;
constexpr uint8_t kFirstModifier = 0xE0U;  // Left Control.
constexpr uint8_t kLastModifier = 0xE7U;   // Right GUI.
constexpr uint8_t kHighestUsage = 0x73U;   // F24, the descriptor's usage maximum.
constexpr uint16_t kHidServiceUuid = 0x1812U;

// clang-format off
constexpr uint8_t kReportMap[] = {
    0x05, 0x01,               // Usage Page (Generic Desktop)
    0x09, 0x06,               // Usage (Keyboard)
    0xA1, 0x01,               // Collection (Application)
    0x85, kKeyboardReportId,  //   Report ID
    0x05, 0x07,               //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,               //   Usage Minimum (Left Control)
    0x29, 0xE7,               //   Usage Maximum (Right GUI)
    0x15, 0x00,               //   Logical Minimum (0)
    0x25, 0x01,               //   Logical Maximum (1)
    0x75, 0x01,               //   Report Size (1)
    0x95, 0x08,               //   Report Count (8)
    0x81, 0x02,               //   Input (Data, Variable, Absolute)
    0x95, 0x01,               //   Report Count (1)
    0x75, 0x08,               //   Report Size (8)
    0x81, 0x03,               //   Input (Constant)
    0x95, 0x05,               //   Report Count (5)
    0x75, 0x01,               //   Report Size (1)
    0x05, 0x08,               //   Usage Page (LEDs)
    0x19, 0x01,               //   Usage Minimum (Num Lock)
    0x29, 0x05,               //   Usage Maximum (Kana)
    0x91, 0x02,               //   Output (Data, Variable, Absolute)
    0x95, 0x01,               //   Report Count (1)
    0x75, 0x03,               //   Report Size (3)
    0x91, 0x03,               //   Output (Constant)
    0x95, 0x06,               //   Report Count (6)
    0x75, 0x08,               //   Report Size (8)
    0x15, 0x00,               //   Logical Minimum (0)
    0x25, kHighestUsage,      //   Logical Maximum (F24)
    0x05, 0x07,               //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,               //   Usage Minimum (Reserved)
    0x29, kHighestUsage,      //   Usage Maximum (F24)
    0x81, 0x00,               //   Input (Data, Array, Absolute)
    0xC0,                     // End Collection
    0x06, 0x00, 0xFF,         // Usage Page (Vendor Defined)
    0x09, 0x03,               // Usage (Apple Fn/Globe-style modifier)
    0xA1, 0x01,               // Collection (Application)
    0x85, kAppleFnReportId,   //   Report ID
    0x15, 0x00,               //   Logical Minimum (0)
    0x25, 0x01,               //   Logical Maximum (1)
    0x75, 0x01,               //   Report Size (1)
    0x95, 0x01,               //   Report Count (1)
    0x09, 0x03,               //   Usage (Fn)
    0x81, 0x02,               //   Input (Data, Variable, Absolute)
    0x75, 0x07,               //   Report Size (7)
    0x95, 0x01,               //   Report Count (1)
    0x81, 0x03,               //   Input (Constant)
    0xC0,                     // End Collection
    0x05, 0x0C,               // Usage Page (Consumer)
    0x09, 0x01,               // Usage (Consumer Control)
    0xA1, 0x01,               // Collection (Application)
    0x85, kConsumerReportId,  //   Report ID
    0x15, 0x00,               //   Logical Minimum (0)
    0x26, 0xFF, 0x03,         //   Logical Maximum (0x3FF)
    0x19, 0x00,               //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,         //   Usage Maximum (0x3FF)
    0x75, 0x10,               //   Report Size (16)
    0x95, 0x01,               //   Report Count (1)
    0x81, 0x00,               //   Input (Data, Array, Absolute)
    0xC0,                     // End Collection
};
// clang-format on

esp_hid_raw_report_map_t report_maps[] = {{.data = kReportMap, .len = sizeof(kReportMap)}};

// NimBLE and esp_hidd callbacks carry no usable context; the board owns one
// keyboard for the whole boot.
BleHidKeyboard* active_keyboard = nullptr;

void HidEvent(void*, esp_event_base_t, int32_t id, void* event_data) {
    if (active_keyboard == nullptr) {
        return;
    }
    const auto* param = static_cast<esp_hidd_event_data_t*>(event_data);
    switch (static_cast<esp_hidd_event_t>(id)) {
        case ESP_HIDD_START_EVENT:
            active_keyboard->OnHidStarted();
            break;
        case ESP_HIDD_CONNECT_EVENT:
            if (param->connect.status == ESP_OK) {
                active_keyboard->OnConnected(param->connect.dev);
            }
            break;
        case ESP_HIDD_DISCONNECT_EVENT:
            active_keyboard->OnDisconnected();
            break;
        default:
            break;
    }
}

int GapEvent(ble_gap_event* event, void*) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(kTag, "link %s: status=%d", event->connect.status == 0 ? "up" : "failed", event->connect.status);
            if (event->connect.status == 0) {
                // macOS only hands an LE keyboard to its HID driver once the
                // link is bonded, and it does not always start pairing itself.
                const int security = ble_gap_security_initiate(event->connect.conn_handle);
                if (security != 0 && security != BLE_HS_EALREADY) {
                    ESP_LOGW(kTag, "security request failed: %d", security);
                }
            } else if (active_keyboard != nullptr) {
                active_keyboard->OnAdvertisingStopped();
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(kTag, "link down: reason=0x%x", event->disconnect.reason);
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(kTag, "encryption: status=%d", event->enc_change.status);
            return 0;
        case BLE_GAP_EVENT_IDENTITY_RESOLVED:
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(kTag, "subscribe: attr=%u notify=%u", event->subscribe.attr_handle, event->subscribe.cur_notify);
            return 0;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (active_keyboard != nullptr && event->adv_complete.reason != 0) {
                active_keyboard->OnAdvertisingStopped();
            }
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // The computer forgot us and is pairing again; replace the old bond.
            ble_gap_conn_desc desc{};
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                (void)ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        case BLE_GAP_EVENT_PASSKEY_ACTION:
            if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
                ble_sm_io io{};
                io.action = BLE_SM_IOACT_NUMCMP;
                io.numcmp_accept = 1U;
                (void)ble_sm_inject_io(event->passkey.conn_handle, &io);
            }
            return 0;
        default:
            return 0;
    }
}

// Readable only over an encrypted link: a computer app that reads it makes
// the OS pair, which is how a Mac can bond when its Bluetooth settings page
// offers no Connect button. UUID 6c0e1d7a-5b7f-4f2a-9f3e-2c9e8f6b7a01.
const ble_uuid128_t kPairingServiceUuid =
    BLE_UUID128_INIT(0x01, 0x7a, 0x6b, 0x8f, 0x9e, 0x2c, 0x3e, 0x9f, 0x2a, 0x4f, 0x7f, 0x5b, 0x7a, 0x1d, 0x0e, 0x6c);
const ble_uuid128_t kPairingCharacteristicUuid =
    BLE_UUID128_INIT(0x02, 0x7a, 0x6b, 0x8f, 0x9e, 0x2c, 0x3e, 0x9f, 0x2a, 0x4f, 0x7f, 0x5b, 0x7a, 0x1d, 0x0e, 0x6c);

int ReadPairingCharacteristic(uint16_t, uint16_t, ble_gatt_access_ctxt* context, void*) {
    const uint8_t paired = 1U;
    return os_mbuf_append(context->om, &paired, sizeof(paired)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// NimBLE keeps pointers to these tables; they are filled once in Start().
ble_gatt_chr_def pairing_characteristics[2] = {};
ble_gatt_svc_def pairing_services[2] = {};

void HostTask(void*) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

uint32_t BleHidKeyboard::StateFlags() const {
    return MICROPIXEL_HID_KEYBOARD_STATE_AVAILABLE | flags_.load(std::memory_order_acquire);
}

int32_t BleHidKeyboard::Start() {
    if ((flags_.load(std::memory_order_acquire) & MICROPIXEL_HID_KEYBOARD_STATE_STARTED) != 0U ||
        starting_.exchange(true)) {
        return MICROPIXEL_STATUS_OK;
    }
    active_keyboard = this;
    esp_err_t error = nimble_port_init();
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "NimBLE initialization failed: %s", esp_err_to_name(error));
        starting_.store(false);
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }

    // Just Works pairing with a persistent bond: the Gotchi has no way to
    // show or confirm a passkey from inside the Host.
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1U;
    ble_hs_cfg.sm_mitm = 0U;
    ble_hs_cfg.sm_sc = 1U;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ID | BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    (void)ble_svc_gap_device_name_set(device_name_);
    (void)ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_KEYBOARD);

    static esp_hid_device_config_t config{};
    config.vendor_id = 0x303AU;
    config.product_id = 0x4005U;
    config.version = 0x0100U;
    config.device_name = device_name_;
    config.manufacturer_name = "Cheeko";
    config.serial_number = "0001";
    config.report_maps = report_maps;
    config.report_maps_len = 1U;
    esp_hidd_dev_t* device = nullptr;
    error = esp_hidd_dev_init(&config, ESP_HID_TRANSPORT_BLE, HidEvent, &device);
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "HID device initialization failed: %s", esp_err_to_name(error));
        (void)nimble_port_deinit();
        starting_.store(false);
        return MICROPIXEL_STATUS_INTERNAL;
    }
    hid_device_ = device;
    pairing_characteristics[0].uuid = &kPairingCharacteristicUuid.u;
    pairing_characteristics[0].access_cb = ReadPairingCharacteristic;
    pairing_characteristics[0].flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC;
    pairing_services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    pairing_services[0].uuid = &kPairingServiceUuid.u;
    pairing_services[0].characteristics = pairing_characteristics;
    if (ble_gatts_count_cfg(pairing_services) != 0 || ble_gatts_add_svcs(pairing_services) != 0) {
        ESP_LOGW(kTag, "pairing service unavailable");
    }
    ble_store_config_init();
    nimble_port_freertos_init(HostTask);
    flags_.fetch_or(MICROPIXEL_HID_KEYBOARD_STATE_STARTED, std::memory_order_release);
    ESP_LOGI(kTag, "Bluetooth keyboard \"%s\" started", device_name_);
    return MICROPIXEL_STATUS_OK;
}

int32_t BleHidKeyboard::Key(uint8_t usage, bool pressed, uint32_t flags) {
    const bool modifier = usage >= kFirstModifier && usage <= kLastModifier;
    if (usage == 0U || (usage > kHighestUsage && !modifier)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (modifier) {
        const auto bit = static_cast<uint8_t>(1U << (usage - kFirstModifier));
        modifiers_ = pressed ? static_cast<uint8_t>(modifiers_ | bit) : static_cast<uint8_t>(modifiers_ & ~bit);
    } else if (pressed) {
        if (std::find(keys_.begin(), keys_.end(), usage) == keys_.end()) {
            auto free_slot = std::find(keys_.begin(), keys_.end(), uint8_t{0U});
            if (free_slot == keys_.end()) {
                return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
            }
            *free_slot = usage;
        }
    } else {
        // Keep the remaining keys contiguous, as HID array reports expect.
        auto end = std::remove(keys_.begin(), keys_.end(), usage);
        std::fill(end, keys_.end(), uint8_t{0U});
    }

    auto* device = static_cast<esp_hidd_dev_t*>(hid_device_);
    if ((flags_.load(std::memory_order_acquire) & MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED) == 0U || device == nullptr) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    if ((flags & MICROPIXEL_HID_KEYBOARD_KEY_APPLE_FN) != 0U) {
        uint8_t fn_report[1] = {static_cast<uint8_t>(pressed ? 1U : 0U)};
        const esp_err_t error = esp_hidd_dev_input_set(device, 0U, kAppleFnReportId, fn_report, sizeof(fn_report));
        if (error != ESP_OK) {
            ESP_LOGW(kTag, "Fn report failed: %s", esp_err_to_name(error));
        }
    }
    uint8_t report[8] = {modifiers_};
    std::copy(keys_.begin(), keys_.end(), report + 2);
    const esp_err_t error = esp_hidd_dev_input_set(device, 0U, kKeyboardReportId, report, sizeof(report));
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "keyboard report failed: %s", esp_err_to_name(error));
        return MICROPIXEL_STATUS_INTERNAL;
    }
    return MICROPIXEL_STATUS_OK;
}

int32_t BleHidKeyboard::Consumer(uint16_t usage, bool pressed) {
    if (usage == 0U || usage > 0x3FFU) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    auto* device = static_cast<esp_hidd_dev_t*>(hid_device_);
    if ((flags_.load(std::memory_order_acquire) & MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED) == 0U || device == nullptr) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    const uint16_t value = pressed ? usage : 0U;
    uint8_t report[2] = {static_cast<uint8_t>(value & 0xFFU), static_cast<uint8_t>(value >> 8U)};
    const esp_err_t error = esp_hidd_dev_input_set(device, 0U, kConsumerReportId, report, sizeof(report));
    if (error != ESP_OK) {
        ESP_LOGW(kTag, "consumer report failed: %s", esp_err_to_name(error));
        return MICROPIXEL_STATUS_INTERNAL;
    }
    return MICROPIXEL_STATUS_OK;
}

void BleHidKeyboard::OnHidStarted() { Advertise(); }

void BleHidKeyboard::OnConnected(void* device) {
    hid_device_ = device;
    flags_.fetch_and(~static_cast<uint32_t>(MICROPIXEL_HID_KEYBOARD_STATE_ADVERTISING), std::memory_order_acq_rel);
    flags_.fetch_or(MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED, std::memory_order_acq_rel);
    ESP_LOGI(kTag, "connected");
}

void BleHidKeyboard::OnDisconnected() {
    flags_.fetch_and(~static_cast<uint32_t>(MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED), std::memory_order_acq_rel);
    ESP_LOGI(kTag, "disconnected; advertising again");
    Advertise();
}

void BleHidKeyboard::OnAdvertisingStopped() {
    flags_.fetch_and(~static_cast<uint32_t>(MICROPIXEL_HID_KEYBOARD_STATE_ADVERTISING), std::memory_order_acq_rel);
    if ((flags_.load(std::memory_order_acquire) & MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED) == 0U) {
        Advertise();
    }
}

void BleHidKeyboard::Advertise() {
    static ble_uuid16_t hid_uuid = BLE_UUID16_INIT(kHidServiceUuid);
    ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
    fields.appearance_is_present = 1U;
    fields.name = reinterpret_cast<const uint8_t*>(device_name_);
    fields.name_len = static_cast<uint8_t>(std::strlen(device_name_));
    fields.name_is_complete = 1U;
    fields.uuids16 = &hid_uuid;
    fields.num_uuids16 = 1U;
    fields.uuids16_is_complete = 1U;
    int status = ble_gap_adv_set_fields(&fields);
    if (status != 0) {
        ESP_LOGE(kTag, "advertising data rejected: %d", status);
        return;
    }
    ble_gap_adv_params params{};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
    params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
    status = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &params, GapEvent, nullptr);
    if (status != 0 && status != BLE_HS_EALREADY) {
        ESP_LOGE(kTag, "advertising failed to start: %d", status);
        return;
    }
    flags_.fetch_or(MICROPIXEL_HID_KEYBOARD_STATE_ADVERTISING, std::memory_order_acq_rel);
    ESP_LOGI(kTag, "advertising as \"%s\" (status=%d)", device_name_, status);
}

}  // namespace micropixel::platform::bluetooth

#else

namespace micropixel::platform::bluetooth {

uint32_t BleHidKeyboard::StateFlags() const { return 0U; }
int32_t BleHidKeyboard::Start() { return MICROPIXEL_STATUS_UNSUPPORTED; }
int32_t BleHidKeyboard::Key(uint8_t, bool, uint32_t) { return MICROPIXEL_STATUS_UNSUPPORTED; }
int32_t BleHidKeyboard::Consumer(uint16_t, bool) { return MICROPIXEL_STATUS_UNSUPPORTED; }
void BleHidKeyboard::OnHidStarted() {}
void BleHidKeyboard::OnConnected(void*) {}
void BleHidKeyboard::OnDisconnected() {}
void BleHidKeyboard::OnAdvertisingStopped() {}
void BleHidKeyboard::Advertise() {}

}  // namespace micropixel::platform::bluetooth

#endif
