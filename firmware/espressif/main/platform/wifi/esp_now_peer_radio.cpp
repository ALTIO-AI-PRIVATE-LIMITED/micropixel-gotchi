#include "platform/wifi/esp_now_peer_radio.hpp"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"
#include "nvs.h"
#include "work/task_policy.hpp"

namespace micropixel::platform::wifi {
namespace {

constexpr char kTag[] = "peer_radio";
constexpr char kNvsNamespace[] = "peer_radio";
constexpr char kNvsPeerKey[] = "peer";
constexpr uint32_t kTaskStackSize = 4096U;
constexpr uint32_t kRxQueueDepth = 16U;
constexpr uint8_t kMagic0 = 'C';
constexpr uint8_t kMagic1 = 'W';
constexpr uint8_t kFrameHello = 1U;
constexpr uint8_t kFrameBeacon = 2U;
constexpr uint8_t kFrameData = 3U;
constexpr uint32_t kHeaderBytes = 3U;
constexpr uint8_t kRendezvousChannel = 1U;
constexpr uint8_t kMaxChannel = 13U;
constexpr int64_t kBeaconIntervalUs = 250000;
constexpr int64_t kPresenceTimeoutUs = 2000000;
// A weak link loses packets in bursts; leaving the channel then breaks it for
// good. Only search after the peer has been silent this long.
constexpr int64_t kSearchAfterUs = 15000000;
// 20 dBm, the S3's maximum (units of 0.25 dBm).
constexpr int8_t kMaxTxPowerQuarterDbm = 80;
constexpr int64_t kRendezvousDwellUs = 600000;
constexpr int64_t kSweepDwellUs = 300000;
constexpr int64_t kPairingWindowUs = 60000000;
constexpr uint8_t kBroadcast[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
// Shared by every CGotchi; the per-link key below is what pairs two units.
constexpr uint8_t kPrimaryMasterKey[ESP_NOW_KEY_LEN] = {'C', 'G', 'o', 't', 'c', 'h', 'i', '-',
                                                        'w', 'a', 'l', 'k', 'i', 'e', '-', '1'};

EspNowPeerRadio* instance = nullptr;

uint32_t Fnv1a(const uint8_t* bytes, uint32_t length, uint32_t hash) {
    for (uint32_t index = 0U; index < length; ++index) {
        hash = (hash ^ bytes[index]) * 16777619U;
    }
    return hash;
}

// Both addresses in a fixed order, so both ends derive the same values.
void SortedPair(const uint8_t* first, const uint8_t* second, uint8_t (&out)[12]) {
    const bool first_low = std::memcmp(first, second, 6U) < 0;
    std::memcpy(out, first_low ? first : second, 6U);
    std::memcpy(out + 6, first_low ? second : first, 6U);
}

}  // namespace

esp_err_t EspNowPeerRadio::Initialize() {
    if (task_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    mutex_ = xSemaphoreCreateMutex();
    rx_queue_ = xQueueCreateWithCaps(kRxQueueDepth, sizeof(RxItem), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mutex_ == nullptr || rx_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    // PSRAM stack: internal RAM is scarce with Wi-Fi and BLE up. The task only
    // sends frames and switches channels; NVS writes stay on the caller.
    if (xTaskCreatePinnedToCoreWithCaps(TaskEntry, "peer_radio", kTaskStackSize, this, task_policy::kHostPriority,
                                        &task_, task_policy::kSystemCore,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        task_ = nullptr;
        return ESP_ERR_NO_MEM;
    }
    instance = this;
    return ESP_OK;
}

uint16_t EspNowPeerRadio::PairCode(const Address& first, const Address& second) {
    uint8_t pair[12];
    SortedPair(first.data(), second.data(), pair);
    return static_cast<uint16_t>(Fnv1a(pair, sizeof(pair), 2166136261U) % 10000U);
}

uint32_t EspNowPeerRadio::FlagsLocked(int64_t now_us) const {
    uint32_t flags = task_ != nullptr ? static_cast<uint32_t>(MICROPIXEL_PEER_RADIO_STATE_AVAILABLE) : 0U;
    if (running_.load(std::memory_order_acquire)) {
        flags |= MICROPIXEL_PEER_RADIO_STATE_STARTED;
    }
    if (paired_) {
        flags |= MICROPIXEL_PEER_RADIO_STATE_PAIRED;
        if (peer_heard_us_ != 0 && now_us - peer_heard_us_ < kPresenceTimeoutUs) {
            flags |= MICROPIXEL_PEER_RADIO_STATE_PEER_PRESENT;
        }
    }
    if (pairing_until_us_ > now_us) {
        flags |= MICROPIXEL_PEER_RADIO_STATE_PAIRING;
    }
    if (wifi_channel_) {
        flags |= MICROPIXEL_PEER_RADIO_STATE_WIFI_CHANNEL;
    }
    return flags;
}

micropixel_peer_radio_state_t EspNowPeerRadio::State() const {
    micropixel_peer_radio_state_t state{};
    state.size = sizeof(state);
    if (mutex_ == nullptr) {
        return state;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    state.flags = FlagsLocked(esp_timer_get_time());
    state.channel = channel_;
    state.peer_rssi = peer_rssi_;
    std::memcpy(state.self_address, self_.data(), 6U);
    if (paired_) {
        std::memcpy(state.peer_address, peer_.data(), 6U);
    }
    (void)xSemaphoreGive(mutex_);
    state.packets_sent = packets_sent_.load(std::memory_order_relaxed);
    state.packets_received = packets_received_.load(std::memory_order_relaxed);
    state.send_failures = send_failures_.load(std::memory_order_relaxed);
    return state;
}

esp_err_t EspNowPeerRadio::AddPeer(const Address& address) {
    esp_now_peer_info_t info{};
    std::memcpy(info.peer_addr, address.data(), 6U);
    info.channel = 0U;  // Whatever channel the STA is on.
    info.ifidx = WIFI_IF_STA;
    info.encrypt = true;
    uint8_t pair[12];
    SortedPair(self_.data(), address.data(), pair);
    for (uint32_t word = 0U; word < ESP_NOW_KEY_LEN / 4U; ++word) {
        const uint32_t value = Fnv1a(pair, sizeof(pair), 2166136261U ^ (0x9E3779B9U * (word + 1U)));
        std::memcpy(info.lmk + word * 4U, &value, 4U);
    }
    return esp_now_is_peer_exist(address.data()) ? esp_now_mod_peer(&info) : esp_now_add_peer(&info);
}

void EspNowPeerRadio::LoadPeer() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    Address stored{};
    size_t length = stored.size();
    if (nvs_get_blob(handle, kNvsPeerKey, stored.data(), &length) == ESP_OK && length == stored.size()) {
        paired_ = AddPeer(stored) == ESP_OK;
        if (paired_) {
            peer_ = stored;
            ESP_LOGI(kTag, "paired with %02x:%02x:%02x:%02x:%02x:%02x", peer_[0], peer_[1], peer_[2], peer_[3],
                     peer_[4], peer_[5]);
        }
    }
    nvs_close(handle);
}

esp_err_t EspNowPeerRadio::EnsureEspNow() {
    // Wi-Fi off releases the driver, and ESP-NOW with it; start it again.
    wifi_mode_t mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        ESP_LOGI(kTag, "turning Wi-Fi on for the peer link");
        if (!wifi_.SetEnabled(true)) {
            return ESP_ERR_INVALID_STATE;
        }
        esp_now_ready_ = false;
    }
    esp_now_peer_num_t count{};
    if (esp_now_ready_ && esp_now_get_peer_num(&count) == ESP_OK) {
        return ESP_OK;
    }
    esp_err_t status = esp_now_init();
    if (status == ESP_OK) {
        status = esp_now_register_recv_cb(OnReceive);
    }
    if (status == ESP_OK) {
        status = esp_now_register_send_cb(OnSent);
    }
    if (status == ESP_OK) {
        status = esp_now_set_pmk(kPrimaryMasterKey);
    }
    if (status == ESP_OK) {
        status = esp_wifi_get_mac(WIFI_IF_STA, self_.data());
    }
    if (status == ESP_OK) {
        esp_now_peer_info_t broadcast{};
        std::memcpy(broadcast.peer_addr, kBroadcast, 6U);
        broadcast.ifidx = WIFI_IF_STA;
        status = esp_now_add_peer(&broadcast);
    }
    if (status != ESP_OK) {
        ESP_LOGE(kTag, "ESP-NOW start failed: %s", esp_err_to_name(status));
        (void)esp_now_deinit();
        return status;
    }
    esp_now_ready_ = true;
    if (wifi_events_ == nullptr) {
        (void)esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, OnWifiEvent, this, &wifi_events_);
    }
    // Once, for the state before the events started; polling this call logs
    // a driver warning each time while unassociated.
    wifi_ap_record_t access_point{};
    const bool associated = esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
    ap_channel_.store(associated ? access_point.primary : 0U, std::memory_order_relaxed);
    associated_.store(associated, std::memory_order_release);
    LoadPeer();
    ESP_LOGI(kTag, "ESP-NOW ready on %02x:%02x:%02x:%02x:%02x:%02x", self_[0], self_[1], self_[2], self_[3],
             self_[4], self_[5]);
    return ESP_OK;
}

int32_t EspNowPeerRadio::Start(const device::PeerRadioCallbacks& callbacks) {
    if (task_ == nullptr) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    if (running_.load(std::memory_order_acquire)) {
        return MICROPIXEL_STATUS_STALE_STATE;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    const esp_err_t status = EnsureEspNow();
    if (status != ESP_OK) {
        (void)xSemaphoreGive(mutex_);
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    uint8_t primary = kRendezvousChannel;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &secondary) == ESP_OK) {
        channel_ = primary;
    }
    callbacks_ = callbacks;
    peer_heard_us_ = 0;
    pairing_until_us_ = 0;
    candidate_reported_ = false;
    published_flags_ = 0xFFFFFFFFU;
    next_beacon_us_ = 0;
    dwell_until_us_ = 0;
    (void)xQueueReset(rx_queue_);
    running_.store(true, std::memory_order_release);
    (void)xSemaphoreGive(mutex_);
    // Modem sleep would drop ESP-NOW frames between beacons while associated.
    const esp_err_t ps_status = esp_wifi_set_ps(WIFI_PS_NONE);
    (void)esp_wifi_set_max_tx_power(kMaxTxPowerQuarterDbm);
    ESP_LOGI(kTag, "started on channel %u (power save off: %s), internal free %u, DMA largest %u", channel_,
             esp_err_to_name(ps_status), static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)));
    xTaskNotifyGive(task_);
    return MICROPIXEL_STATUS_OK;
}

int32_t EspNowPeerRadio::Stop() {
    if (task_ == nullptr || !running_.load(std::memory_order_acquire)) {
        return MICROPIXEL_STATUS_OK;
    }
    // Callbacks run under the mutex, so none is in flight once it is ours.
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    running_.store(false, std::memory_order_release);
    callbacks_ = {};
    pairing_until_us_ = 0;
    (void)xSemaphoreGive(mutex_);
    (void)esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ESP_LOGI(kTag, "stopped");
    return MICROPIXEL_STATUS_OK;
}

int32_t EspNowPeerRadio::BeginPairing() {
    if (!running_.load(std::memory_order_acquire)) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    pairing_until_us_ = esp_timer_get_time() + kPairingWindowUs;
    candidate_reported_ = false;
    next_beacon_us_ = 0;
    (void)xSemaphoreGive(mutex_);
    ESP_LOGI(kTag, "pairing for %lld s", static_cast<long long>(kPairingWindowUs / 1000000));
    xTaskNotifyGive(task_);
    return MICROPIXEL_STATUS_OK;
}

int32_t EspNowPeerRadio::AcceptPeer(const uint8_t (&address)[6]) {
    if (!running_.load(std::memory_order_acquire)) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    Address accepted{};
    std::memcpy(accepted.data(), address, 6U);
    if (accepted == self_ || std::memcmp(address, kBroadcast, 6U) == 0 || accepted == Address{}) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    if (paired_ && peer_ != accepted) {
        (void)esp_now_del_peer(peer_.data());
    }
    const esp_err_t add_status = AddPeer(accepted);
    if (add_status != ESP_OK) {
        (void)xSemaphoreGive(mutex_);
        ESP_LOGE(kTag, "add peer failed: %s", esp_err_to_name(add_status));
        return MICROPIXEL_STATUS_INTERNAL;
    }
    peer_ = accepted;
    paired_ = true;
    pairing_until_us_ = 0;
    // It was just heard pairing, so it is in range.
    peer_heard_us_ = esp_timer_get_time();
    next_beacon_us_ = 0;
    (void)xSemaphoreGive(mutex_);
    nvs_handle_t handle = 0;
    esp_err_t status = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (status == ESP_OK) {
        status = nvs_set_blob(handle, kNvsPeerKey, accepted.data(), accepted.size());
        if (status == ESP_OK) {
            status = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    ESP_LOGI(kTag, "paired with %02x:%02x:%02x:%02x:%02x:%02x (saved: %s)", accepted[0], accepted[1], accepted[2],
             accepted[3], accepted[4], accepted[5], esp_err_to_name(status));
    xTaskNotifyGive(task_);
    return MICROPIXEL_STATUS_OK;
}

int32_t EspNowPeerRadio::Forget() {
    if (task_ == nullptr) {
        return MICROPIXEL_STATUS_UNSUPPORTED;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    if (paired_ && esp_now_ready_) {
        (void)esp_now_del_peer(peer_.data());
    }
    paired_ = false;
    peer_ = {};
    peer_heard_us_ = 0;
    (void)xSemaphoreGive(mutex_);
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        (void)nvs_erase_key(handle, kNvsPeerKey);
        (void)nvs_commit(handle);
        nvs_close(handle);
    }
    ESP_LOGI(kTag, "pairing forgotten");
    return MICROPIXEL_STATUS_OK;
}

int32_t EspNowPeerRadio::Send(const uint8_t* data, uint32_t length) {
    if (!running_.load(std::memory_order_acquire)) {
        return MICROPIXEL_STATUS_CLOSED;
    }
    if (data == nullptr || length == 0U || length > MICROPIXEL_PEER_RADIO_MAX_PAYLOAD) {
        return MICROPIXEL_STATUS_INVALID_ARGUMENT;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    const bool paired = paired_;
    const Address peer = peer_;
    (void)xSemaphoreGive(mutex_);
    if (!paired) {
        return MICROPIXEL_STATUS_NOT_FOUND;
    }
    uint8_t frame[kHeaderBytes + MICROPIXEL_PEER_RADIO_MAX_PAYLOAD];
    frame[0] = kMagic0;
    frame[1] = kMagic1;
    frame[2] = kFrameData;
    std::memcpy(frame + kHeaderBytes, data, length);
    const esp_err_t status = esp_now_send(peer.data(), frame, kHeaderBytes + length);
    if (status == ESP_OK) {
        packets_sent_.fetch_add(1U, std::memory_order_relaxed);
        return MICROPIXEL_STATUS_OK;
    }
    return status == ESP_ERR_ESPNOW_NO_MEM ? MICROPIXEL_STATUS_RATE_LIMITED : MICROPIXEL_STATUS_INTERNAL;
}

void EspNowPeerRadio::OnReceive(const esp_now_recv_info_t* info, const uint8_t* data, int length) {
    EspNowPeerRadio* self = instance;
    if (self == nullptr || info == nullptr || data == nullptr || length < static_cast<int>(kHeaderBytes) ||
        length > ESP_NOW_MAX_DATA_LEN || data[0] != kMagic0 || data[1] != kMagic1 ||
        !self->running_.load(std::memory_order_acquire)) {
        return;
    }
    RxItem item{};
    std::memcpy(item.source.data(), info->src_addr, 6U);
    item.rssi = info->rx_ctrl != nullptr ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
    item.length = static_cast<uint8_t>(length);
    std::memcpy(item.data, data, static_cast<size_t>(length));
    (void)xQueueSend(self->rx_queue_, &item, 0U);
}

void EspNowPeerRadio::OnSent(const esp_now_send_info_t* info, esp_now_send_status_t status) {
    EspNowPeerRadio* self = instance;
    if (self != nullptr && status != ESP_NOW_SEND_SUCCESS && info != nullptr &&
        std::memcmp(info->des_addr, kBroadcast, 6U) != 0) {
        self->send_failures_.fetch_add(1U, std::memory_order_relaxed);
    }
}

void EspNowPeerRadio::OnWifiEvent(void* context, esp_event_base_t, int32_t id, void* data) {
    auto* self = static_cast<EspNowPeerRadio*>(context);
    if (id == WIFI_EVENT_STA_CONNECTED && data != nullptr) {
        self->ap_channel_.store(static_cast<const wifi_event_sta_connected_t*>(data)->channel,
                                std::memory_order_relaxed);
        self->associated_.store(true, std::memory_order_release);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP) {
        self->associated_.store(false, std::memory_order_release);
    }
}

void EspNowPeerRadio::TaskEntry(void* argument) { static_cast<EspNowPeerRadio*>(argument)->Run(); }

void EspNowPeerRadio::Run() {
    RxItem item{};
    for (;;) {
        if (!running_.load(std::memory_order_acquire)) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        if (xQueueReceive(rx_queue_, &item, pdMS_TO_TICKS(20)) == pdTRUE) {
            HandleFrame(item, esp_timer_get_time());
        }
        Tick(esp_timer_get_time());
    }
}

void EspNowPeerRadio::HandleFrame(const RxItem& item, int64_t now_us) {
    if (item.source == self_) {
        return;
    }
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!running_.load(std::memory_order_acquire)) {
        (void)xSemaphoreGive(mutex_);
        return;
    }
    const uint8_t type = item.data[2];
    const bool from_peer = paired_ && item.source == peer_;
    if (from_peer) {
        peer_heard_us_ = now_us;
        peer_rssi_ = item.rssi;
    }
    if (type == kFrameHello && pairing_until_us_ > now_us &&
        (!candidate_reported_ || candidate_ != item.source)) {
        candidate_ = item.source;
        candidate_reported_ = true;
        const uint16_t code = PairCode(self_, item.source);
        ESP_LOGI(kTag, "pairing candidate %02x:%02x:%02x:%02x:%02x:%02x code %04u rssi %d", item.source[0],
                 item.source[1], item.source[2], item.source[3], item.source[4], item.source[5], code, item.rssi);
        if (callbacks_.candidate != nullptr) {
            uint8_t address[6];
            std::memcpy(address, item.source.data(), 6U);
            callbacks_.candidate(callbacks_.context, address, code, item.rssi);
        }
    } else if (type == kFrameData && from_peer) {
        packets_received_.fetch_add(1U, std::memory_order_relaxed);
        if (callbacks_.packet != nullptr) {
            callbacks_.packet(callbacks_.context, item.data + kHeaderBytes, item.length - kHeaderBytes, item.rssi);
        }
    }
    (void)xSemaphoreGive(mutex_);
}

void EspNowPeerRadio::UpdateChannel(int64_t now_us) {
    uint8_t desired = channel_;
    const bool associated = associated_.load(std::memory_order_acquire);
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    wifi_channel_ = associated;
    const bool recently_heard = paired_ && peer_heard_us_ != 0 && now_us - peer_heard_us_ < kSearchAfterUs;
    // Never leave a peer heard recently, even while pairing again.
    const bool searching = !recently_heard && (paired_ || pairing_until_us_ > now_us);
    (void)xSemaphoreGive(mutex_);
    if (associated) {
        desired = ap_channel_.load(std::memory_order_relaxed);
    } else if (searching && now_us >= dwell_until_us_) {
        if (on_rendezvous_) {
            desired = sweep_channel_;
            sweep_channel_ = sweep_channel_ >= kMaxChannel ? 2U : static_cast<uint8_t>(sweep_channel_ + 1U);
            dwell_until_us_ = now_us + kSweepDwellUs;
        } else {
            desired = kRendezvousChannel;
            dwell_until_us_ = now_us + kRendezvousDwellUs;
        }
        on_rendezvous_ = !on_rendezvous_;
    }
    if (desired != channel_ && !associated) {
        if (esp_wifi_set_channel(desired, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            return;  // The Wi-Fi manager is scanning or connecting; retry next tick.
        }
        next_beacon_us_ = 0;  // Announce on arrival.
    }
    if (desired != channel_) {
        (void)xSemaphoreTake(mutex_, portMAX_DELAY);
        channel_ = desired;
        (void)xSemaphoreGive(mutex_);
    }
}

void EspNowPeerRadio::SendControl(const uint8_t* destination, uint8_t type) {
    const uint8_t frame[kHeaderBytes + 1U] = {kMagic0, kMagic1, type, channel_};
    (void)esp_now_send(destination, frame, sizeof(frame));
}

void EspNowPeerRadio::Tick(int64_t now_us) {
    UpdateChannel(now_us);
    if (now_us >= next_beacon_us_) {
        next_beacon_us_ = now_us + kBeaconIntervalUs;
        (void)xSemaphoreTake(mutex_, portMAX_DELAY);
        const bool pairing = pairing_until_us_ > now_us;
        const bool paired = paired_;
        const Address peer = peer_;
        (void)xSemaphoreGive(mutex_);
        if (pairing) {
            SendControl(kBroadcast, kFrameHello);
        }
        if (paired) {
            SendControl(peer.data(), kFrameBeacon);
        }
    }
    PublishState(false);
}

void EspNowPeerRadio::PublishState(bool force) {
    (void)xSemaphoreTake(mutex_, portMAX_DELAY);
    const uint32_t flags = FlagsLocked(esp_timer_get_time());
    if (running_.load(std::memory_order_acquire) &&
        (force || flags != published_flags_ || channel_ != published_channel_)) {
        if (flags != published_flags_) {
            ESP_LOGI(kTag, "state 0x%02lx channel %u rssi %d", static_cast<unsigned long>(flags), channel_,
                     peer_rssi_);
        }
        published_flags_ = flags;
        published_channel_ = channel_;
        if (callbacks_.state != nullptr) {
            callbacks_.state(callbacks_.context, flags, channel_, peer_rssi_);
        }
    }
    (void)xSemaphoreGive(mutex_);
}

}  // namespace micropixel::platform::wifi
