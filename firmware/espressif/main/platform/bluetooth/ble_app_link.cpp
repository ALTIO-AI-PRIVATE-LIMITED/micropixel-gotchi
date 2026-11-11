#include "platform/bluetooth/ble_app_link.hpp"

#include "sdkconfig.h"

#if CONFIG_BT_NIMBLE_ENABLED

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"
#include "work/task_policy.hpp"

namespace micropixel::platform::bluetooth {
namespace {

constexpr char kTag[] = "micropixel_ble_link";
constexpr uint32_t kReplyTimeoutMs = 3000U;   // replies retry this long (BULK pauses meanwhile)
constexpr uint32_t kStatusTimeoutMs = 500U;   // a newer status replaces it a second later anyway
constexpr uint32_t kMessageActiveMs = 1000U;  // after an INBOX write, replies are expected
constexpr int kMbufReserve = 16;              // msys blocks BULK never takes
constexpr int kBulkBlocksWhileMessages = 8;   // queued BULK blocks while messages flow (~2 KB)
constexpr int kBulkBlocks = 40;               // ... otherwise
constexpr uint32_t kReplyQueueDepth = 8U;
constexpr uint32_t kTaskStackSize = 4096U;
constexpr uint16_t kNoConnection = BLE_HS_CONN_HANDLE_NONE;
constexpr int kMbufData = MYNEWT_VAL(MSYS_1_BLOCK_SIZE) - 16;  // payload per block (16 B mbuf header)

// 6e400001-a9e8-4b1b-9c39-6d6f8a3cXXXX, little-endian as BLE_UUID128_INIT takes it.
#define MICROPIXEL_LINK_UUID(suffix)                                                                              \
    BLE_UUID128_INIT((suffix) & 0xFFU, (suffix) >> 8U, 0x3c, 0x8a, 0x6f, 0x6d, 0x39, 0x9c, 0x1b, 0x4b, 0xe8, 0xa9, \
                     0x01, 0x00, 0x40, 0x6e)

const ble_uuid128_t kServiceUuid = MICROPIXEL_LINK_UUID(0x0000U);
const ble_uuid128_t kInboxUuid = MICROPIXEL_LINK_UUID(0x0001U);
const ble_uuid128_t kChannelUuids[3] = {MICROPIXEL_LINK_UUID(0x0002U), MICROPIXEL_LINK_UUID(0x0003U),
                                        MICROPIXEL_LINK_UUID(0x0004U)};
const ble_uuid128_t kValueUuid = MICROPIXEL_LINK_UUID(0x0005U);

// NimBLE keeps pointers to these tables; they are filled once in RegisterServices().
uint16_t inbox_handle = 0U;
uint16_t channel_handles[3] = {};
uint16_t value_handle = 0U;
ble_gatt_chr_def characteristics[6] = {};
ble_gatt_svc_def services[2] = {};
ble_gap_event_listener gap_listener = {};

// NimBLE callbacks carry no usable context; the board owns one link.
BleAppLink* active_link = nullptr;

uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// msys blocks one queued notify of n bytes holds: ATT/L2CAP/HCI headers and the
// packet header in the first block, plus one for the ACL fragment split off.
int BlocksFor(uint32_t length) { return static_cast<int>((length + 3U + 4U + 4U + 8U + kMbufData - 1U) / kMbufData) + 1; }

int AccessEntry(uint16_t connection, uint16_t attribute, ble_gatt_access_ctxt* context, void*) {
    return active_link != nullptr ? active_link->Access(connection, attribute, context) : BLE_ATT_ERR_UNLIKELY;
}

int GapEntry(ble_gap_event* event, void*) { return active_link != nullptr ? active_link->OnGapEvent(event) : 0; }

}  // namespace

bool BleAppLink::RegisterServices() {
    if (registered_) {
        return true;
    }
    callback_mutex_ = xSemaphoreCreateMutex();
    status_mutex_ = xSemaphoreCreateMutex();
    reply_queue_ = xQueueCreateWithCaps(kReplyQueueDepth, sizeof(Text), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    status_text_ = static_cast<Text*>(heap_caps_calloc(4U, sizeof(Text), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (callback_mutex_ == nullptr || status_mutex_ == nullptr || reply_queue_ == nullptr || status_text_ == nullptr) {
        ESP_LOGE(kTag, "out of memory");
        return false;
    }
    status_value_ = status_text_ + 1;
    scratch_ = status_text_ + 2;
    inbox_ = status_text_ + 3;
    active_link = this;

    characteristics[0].uuid = &kInboxUuid.u;
    characteristics[0].access_cb = AccessEntry;
    characteristics[0].flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;
    characteristics[0].val_handle = &inbox_handle;
    for (uint32_t index = 0U; index < 3U; ++index) {
        ble_gatt_chr_def& channel = characteristics[1U + index];
        channel.uuid = &kChannelUuids[index].u;
        channel.access_cb = AccessEntry;
        channel.flags = BLE_GATT_CHR_F_NOTIFY;
        if (index == MICROPIXEL_BLE_LINK_CHANNEL_STATUS) {
            channel.flags |= BLE_GATT_CHR_F_READ;
        }
        channel.val_handle = &channel_handles[index];
    }
    characteristics[4].uuid = &kValueUuid.u;
    characteristics[4].access_cb = AccessEntry;
    characteristics[4].flags =
        BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_WRITE_ENC;
    characteristics[4].val_handle = &value_handle;
    services[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid = &kServiceUuid.u;
    services[0].characteristics = characteristics;
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0) {
        ESP_LOGE(kTag, "GATT service rejected");
        return false;
    }
    if (ble_gap_event_listener_register(&gap_listener, GapEntry, nullptr) != 0) {
        ESP_LOGW(kTag, "GAP listener rejected");
    }
    // PSRAM stack: internal RAM is scarce with Wi-Fi and BLE up.
    if (xTaskCreatePinnedToCoreWithCaps(TaskEntry, "ble_link", kTaskStackSize, this, task_policy::kHostPriority,
                                        nullptr, task_policy::kSystemCore,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGE(kTag, "TX task failed to start");
        return false;
    }
    registered_ = true;
    return true;
}

micropixel_ble_link_state_t BleAppLink::State() const {
    micropixel_ble_link_state_t state{};
    state.size = sizeof(state);
    state.flags = Flags();
    state.mtu = Mtu();
    state.bulk_payload = static_cast<uint16_t>(state.mtu - 3U);
    const uint16_t connection = connection_.load();
    state.peers = connection != kNoConnection ? 1U : 0U;
    state.phy = 1U;
    if (connection != kNoConnection) {
        uint8_t tx = 1U;
        uint8_t rx = 1U;
        if (ble_gap_read_le_phy(connection, &tx, &rx) == 0 && tx == BLE_GAP_LE_PHY_2M) {
            state.phy = 2U;
        }
    }
    state.bulk_drops = bulk_drops_.load();
    state.reply_drops = reply_drops_.load();
    state.bulk_kbps = NowMs() - window_start_ms_ > 2000U ? 0U : bulk_kbps_.load();
    return state;
}

int32_t BleAppLink::Start(const device::BleLinkCallbacks& callbacks) {
    if (!registered_) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    (void)xSemaphoreTake(callback_mutex_, portMAX_DELAY);
    callbacks_ = callbacks;
    started_ = true;
    (void)xSemaphoreGive(callback_mutex_);
    return MICROPIXEL_STATUS_OK;
}

int32_t BleAppLink::Stop() {
    if (!registered_) {
        return MICROPIXEL_STATUS_OK;
    }
    (void)xSemaphoreTake(callback_mutex_, portMAX_DELAY);
    started_ = false;
    callbacks_ = {};
    (void)xSemaphoreGive(callback_mutex_);
    room_armed_ = false;
    return MICROPIXEL_STATUS_OK;
}

int32_t BleAppLink::Queue(uint8_t channel, const uint8_t* data, uint32_t length) {
    if (!registered_ || length > MICROPIXEL_BLE_LINK_MAX_MESSAGE ||
        (channel != MICROPIXEL_BLE_LINK_CHANNEL_REPLY && channel != MICROPIXEL_BLE_LINK_CHANNEL_STATUS)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (channel == MICROPIXEL_BLE_LINK_CHANNEL_STATUS) {
        (void)xSemaphoreTake(status_mutex_, portMAX_DELAY);
        status_value_->length = static_cast<uint16_t>(length);  // also served on read
        std::memcpy(status_value_->data, data, length);
        const bool subscribed = (subscribed_.load() & (1U << channel)) != 0U;
        if (subscribed) {
            *status_text_ = *status_value_;
            if (!status_pending_) {
                ++control_pending_;
            }
            status_pending_ = true;
        }
        (void)xSemaphoreGive(status_mutex_);
        return subscribed ? MICROPIXEL_STATUS_OK : MICROPIXEL_STATUS_CLOSED;
    }
    if ((subscribed_.load() & (1U << channel)) == 0U) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    Text text{};
    text.length = static_cast<uint16_t>(length);
    std::memcpy(text.data, data, length);
    ++control_pending_;
    if (xQueueSend(reply_queue_, &text, pdMS_TO_TICKS(200)) != pdTRUE) {  // 8 replies stuck: the link is gone
        --control_pending_;
        ++reply_drops_;
        return MICROPIXEL_STATUS_RESOURCE_EXHAUSTED;
    }
    return MICROPIXEL_STATUS_OK;
}

int32_t BleAppLink::SendBulk(const uint8_t* data, uint32_t length) {
    const uint16_t connection = connection_.load();
    if (!registered_ || connection == kNoConnection ||
        (subscribed_.load() & (1U << MICROPIXEL_BLE_LINK_CHANNEL_BULK)) == 0U) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    if (length == 0U || length > static_cast<uint32_t>(Mtu() - 3U)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    if (control_pending_.load() > 0 || !BulkRoom(length)) {
        room_armed_ = true;
        return MICROPIXEL_STATUS_RATE_LIMITED;
    }
    os_mbuf* om = ble_hs_mbuf_from_flat(data, static_cast<uint16_t>(length));
    const int status = om != nullptr ? ble_gatts_notify_custom(connection, channel_handles[2], om) : BLE_HS_ENOMEM;
    if (status == BLE_HS_ENOMEM) {
        room_armed_ = true;
        return MICROPIXEL_STATUS_RATE_LIMITED;
    }
    if (status != 0) {
        ++bulk_drops_;
        return MICROPIXEL_STATUS_INTERNAL;
    }
    const uint32_t now = NowMs();
    window_bytes_ += length;
    if (now - window_start_ms_ >= 1000U) {
        bulk_kbps_ = window_bytes_ * 8U / std::max<uint32_t>(1U, now - window_start_ms_);
        window_bytes_ = 0U;
        window_start_ms_ = now;
    }
    return MICROPIXEL_STATUS_OK;
}

int32_t BleAppLink::SetValue(const uint8_t* data, uint32_t length) {
    if (!registered_ || length > sizeof(value_)) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    (void)xSemaphoreTake(status_mutex_, portMAX_DELAY);
    std::memcpy(value_, data, length);
    value_length_ = static_cast<uint16_t>(length);
    (void)xSemaphoreGive(status_mutex_);
    return MICROPIXEL_STATUS_OK;
}

int BleAppLink::Access(uint16_t, uint16_t attribute, void* raw_context) {
    auto* context = static_cast<ble_gatt_access_ctxt*>(raw_context);
    if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (attribute != inbox_handle && attribute != value_handle) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (OS_MBUF_PKTLEN(context->om) > sizeof(inbox_->data)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint16_t length = 0U;
        if (ble_hs_mbuf_to_flat(context->om, inbox_->data, sizeof(inbox_->data), &length) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (attribute == inbox_handle) {
            last_message_ms_ = NowMs();
        }
        Deliver(attribute == value_handle ? MICROPIXEL_BLE_LINK_SOURCE_VALUE : MICROPIXEL_BLE_LINK_SOURCE_INBOX,
                inbox_->data, length);
        return 0;
    }
    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int status = 0;
        (void)xSemaphoreTake(status_mutex_, portMAX_DELAY);
        if (attribute == value_handle) {
            status = os_mbuf_append(context->om, value_, value_length_);
        } else if (attribute == channel_handles[MICROPIXEL_BLE_LINK_CHANNEL_STATUS]) {
            status = os_mbuf_append(context->om, status_value_->data, status_value_->length);
        }
        (void)xSemaphoreGive(status_mutex_);
        return status == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// A bonded peer's restored encryption and subscriptions can be reported before
// its connect event, so the connection is claimed on whichever comes first
// and its state is only reset for a different connection.
void BleAppLink::Claim(uint16_t connection) {
    if (connection_.load() != connection) {
        connection_ = connection;
        subscribed_ = 0U;
        encrypted_ = false;
    }
}

int BleAppLink::OnGapEvent(void* raw_event) {
    auto* event = static_cast<ble_gap_event*>(raw_event);
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                Claim(event->connect.conn_handle);
                TuneLink(event->connect.conn_handle);
                PublishState();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            if (event->disconnect.conn.conn_handle == connection_.load()) {
                connection_ = kNoConnection;
                subscribed_ = 0U;
                encrypted_ = false;
                PublishState();
            }
            break;
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status == 0) {
                Claim(event->enc_change.conn_handle);
                encrypted_ = true;
                PublishState();
            }
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            for (uint32_t index = 0U; index < 3U; ++index) {
                if (event->subscribe.attr_handle == channel_handles[index]) {
                    Claim(event->subscribe.conn_handle);
                    if (event->subscribe.cur_notify != 0U) {
                        subscribed_.fetch_or(1U << index);
                    } else {
                        subscribed_.fetch_and(~(1U << index));
                    }
                    PublishState();
                }
            }
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(kTag, "mtu %u", event->mtu.value);
            PublishState();
            break;
        default:
            break;
    }
    return 0;
}

void BleAppLink::TaskEntry(void* context) { static_cast<BleAppLink*>(context)->Task(); }

void BleAppLink::Task() {
    for (;;) {
        if (xQueueReceive(reply_queue_, scratch_, pdMS_TO_TICKS(room_armed_.load() ? 5 : 20)) == pdTRUE) {
            SendText(MICROPIXEL_BLE_LINK_CHANNEL_REPLY, *scratch_, kReplyTimeoutMs);
            --control_pending_;
        }
        (void)xSemaphoreTake(status_mutex_, portMAX_DELAY);
        const bool due = status_pending_;
        if (due) {
            *scratch_ = *status_text_;
        }
        status_pending_ = false;
        (void)xSemaphoreGive(status_mutex_);
        if (due) {
            SendText(MICROPIXEL_BLE_LINK_CHANNEL_STATUS, *scratch_, kStatusTimeoutMs);
            --control_pending_;
        }
        if (room_armed_.load() && control_pending_.load() <= 0 && BulkRoom(Mtu() - 3U)) {
            room_armed_ = false;
            (void)xSemaphoreTake(callback_mutex_, portMAX_DELAY);
            if (started_ && callbacks_.room != nullptr) {
                callbacks_.room(callbacks_.context);
            }
            (void)xSemaphoreGive(callback_mutex_);
        }
    }
}

void BleAppLink::SendText(uint8_t channel, const Text& text, uint32_t timeout_ms) {
    const uint32_t length = std::min<uint32_t>(text.length, Mtu() - 3U);
    if (!NotifyOne(channel, text.data, length, timeout_ms) && channel == MICROPIXEL_BLE_LINK_CHANNEL_REPLY &&
        (subscribed_.load() & (1U << channel)) != 0U) {
        ++reply_drops_;
    }
}

// One notify through the NimBLE host; unlike a plain notify it reports "out
// of buffers", so the message waits instead of being lost.
bool BleAppLink::NotifyOne(uint8_t channel, const uint8_t* data, uint32_t length, uint32_t timeout_ms) {
    const uint32_t start = NowMs();
    for (;;) {
        const uint16_t connection = connection_.load();
        if (connection == kNoConnection || (subscribed_.load() & (1U << channel)) == 0U) {
            return false;
        }
        if (os_msys_num_free() >= BlocksFor(length)) {
            os_mbuf* om = ble_hs_mbuf_from_flat(data, static_cast<uint16_t>(length));
            const int status =
                om != nullptr ? ble_gatts_notify_custom(connection, channel_handles[channel], om) : BLE_HS_ENOMEM;
            if (status == 0) {
                return true;
            }
            if (status != BLE_HS_ENOMEM) {
                ESP_LOGW(kTag, "notify channel %u failed: %d", channel, status);
                return false;
            }
        }
        if (NowMs() - start > timeout_ms) {
            ESP_LOGW(kTag, "notify channel %u gave up after %lu ms", channel,
                     static_cast<unsigned long>(NowMs() - start));
            return false;
        }
        vTaskDelay(1);
    }
}

bool BleAppLink::BulkRoom(uint32_t length) const {
    const int need = BlocksFor(length);
    const int queued = os_msys_count() - os_msys_num_free();
    const bool messages = NowMs() - last_message_ms_.load() < kMessageActiveMs;
    const int limit = messages ? kBulkBlocksWhileMessages : kBulkBlocks;
    return queued + need <= std::max(need, limit) && os_msys_num_free() >= need + kMbufReserve;
}

uint16_t BleAppLink::Mtu() const {
    const uint16_t connection = connection_.load();
    const uint16_t mtu = connection != kNoConnection ? ble_att_mtu(connection) : 0U;
    return mtu != 0U ? mtu : BLE_ATT_MTU_DFLT;
}

uint32_t BleAppLink::Flags() const {
    uint32_t flags = registered_ ? MICROPIXEL_BLE_LINK_STATE_AVAILABLE : 0U;
    if (started_) {
        flags |= MICROPIXEL_BLE_LINK_STATE_STARTED;
    }
    if (connection_.load() != kNoConnection) {
        flags |= MICROPIXEL_BLE_LINK_STATE_CONNECTED;
    }
    if (encrypted_) {
        flags |= MICROPIXEL_BLE_LINK_STATE_ENCRYPTED;
    }
    const uint32_t subscribed = subscribed_.load();
    if ((subscribed & 1U) != 0U) {
        flags |= MICROPIXEL_BLE_LINK_STATE_SUBSCRIBED_REPLY;
    }
    if ((subscribed & 2U) != 0U) {
        flags |= MICROPIXEL_BLE_LINK_STATE_SUBSCRIBED_STATUS;
    }
    if ((subscribed & 4U) != 0U) {
        flags |= MICROPIXEL_BLE_LINK_STATE_SUBSCRIBED_BULK;
    }
    return flags;
}

void BleAppLink::Deliver(uint8_t source, const uint8_t* data, uint32_t length) {
    (void)xSemaphoreTake(callback_mutex_, portMAX_DELAY);
    if (started_ && callbacks_.message != nullptr) {
        callbacks_.message(callbacks_.context, source, data, length);
    }
    (void)xSemaphoreGive(callback_mutex_);
}

void BleAppLink::PublishState() {
    const uint32_t flags = Flags();
    (void)xSemaphoreTake(callback_mutex_, portMAX_DELAY);
    if (started_ && callbacks_.state != nullptr) {
        callbacks_.state(callbacks_.context, flags);
    }
    (void)xSemaphoreGive(callback_mutex_);
}

// 15 ms interval, no latency, 4 s supervision timeout; data length 251.
void BleAppLink::TuneLink(uint16_t connection) {
    (void)ble_gap_set_data_len(connection, 251U, 2120U);
    ble_gap_upd_params params{};
    params.itvl_min = 12U;
    params.itvl_max = 12U;
    params.latency = 0U;
    params.supervision_timeout = 400U;
    (void)ble_gap_update_params(connection, &params);
}

}  // namespace micropixel::platform::bluetooth

#else

namespace micropixel::platform::bluetooth {

micropixel_ble_link_state_t BleAppLink::State() const {
    micropixel_ble_link_state_t state{};
    state.size = sizeof(state);
    return state;
}
int32_t BleAppLink::Start(const device::BleLinkCallbacks&) { return MICROPIXEL_STATUS_UNSUPPORTED; }
int32_t BleAppLink::Stop() { return MICROPIXEL_STATUS_OK; }
int32_t BleAppLink::Queue(uint8_t, const uint8_t*, uint32_t) { return MICROPIXEL_STATUS_UNSUPPORTED; }
int32_t BleAppLink::SendBulk(const uint8_t*, uint32_t) { return MICROPIXEL_STATUS_UNSUPPORTED; }
int32_t BleAppLink::SetValue(const uint8_t*, uint32_t) { return MICROPIXEL_STATUS_UNSUPPORTED; }
bool BleAppLink::RegisterServices() { return false; }
int BleAppLink::Access(uint16_t, uint16_t, void*) { return 0; }
int BleAppLink::OnGapEvent(void*) { return 0; }

}  // namespace micropixel::platform::bluetooth

#endif
