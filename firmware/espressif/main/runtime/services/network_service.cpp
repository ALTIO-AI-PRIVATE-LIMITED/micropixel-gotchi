#include "runtime/services/network_service.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "runtime/services/timer_service.hpp"
#include "work/task_policy.hpp"

#if __has_include("mdns.h")
#include "mdns.h"
#define MICROPIXEL_NETWORK_HAS_MDNS 1
#else
#define MICROPIXEL_NETWORK_HAS_MDNS 0
#endif

namespace micropixel::runtime {
namespace {

constexpr char kTag[] = "micropixel_network";
constexpr uint32_t kSelectTimeoutMs = 20U;
constexpr uint32_t kStateCheckEvery = 25U;  // select rounds, ~0.5 s
constexpr uint32_t kTaskStackSize = 4096U;
constexpr int kListenBacklog = 2;

class ScopedLock final {
   public:
    explicit ScopedLock(SemaphoreHandle_t mutex) : mutex_(mutex) { (void)xSemaphoreTake(mutex_, portMAX_DELAY); }
    ~ScopedLock() { (void)xSemaphoreGive(mutex_); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;

   private:
    SemaphoreHandle_t mutex_;
};

bool WouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN; }

void MakeNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// NUL-terminated within `capacity`, non-empty.
bool TerminatedText(const char* text, uint32_t capacity) {
    const auto* end = static_cast<const char*>(std::memchr(text, '\0', capacity));
    return end != nullptr && end != text;
}

}  // namespace

NetworkService::NetworkService(device::Wifi& wifi, EventQueue& events, TimerService& clock)
    : wifi_(wifi), events_(events), clock_(clock), mutex_(xSemaphoreCreateMutex()) {}

NetworkService::~NetworkService() {
    Shutdown();
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

uint32_t NetworkService::Flags() const {
    const device::WifiLinkInfo link = wifi_.LinkInfo();
    uint32_t flags = MICROPIXEL_NETWORK_STATE_AVAILABLE;
    if (link.station_connected) {
        flags |= MICROPIXEL_NETWORK_STATE_STATION_CONNECTED;
    }
    if (link.station_connecting) {
        flags |= MICROPIXEL_NETWORK_STATE_STATION_CONNECTING;
    }
    if (link.access_point_active) {
        flags |= MICROPIXEL_NETWORK_STATE_ACCESS_POINT;
    }
    return flags;
}

micropixel_network_state_t NetworkService::State() const {
    const device::WifiLinkInfo link = wifi_.LinkInfo();
    micropixel_network_state_t state{};
    state.size = sizeof(state);
    state.flags = MICROPIXEL_NETWORK_STATE_AVAILABLE;
    if (link.station_connected) {
        state.flags |= MICROPIXEL_NETWORK_STATE_STATION_CONNECTED;
    }
    if (link.station_connecting) {
        state.flags |= MICROPIXEL_NETWORK_STATE_STATION_CONNECTING;
    }
    if (link.access_point_active) {
        state.flags |= MICROPIXEL_NETWORK_STATE_ACCESS_POINT;
    }
    state.channel = link.channel;
    state.rssi = link.rssi;
    state.station_address = link.station_address;
    state.access_point_address = link.access_point_address;
    state.access_point_stations = link.access_point_stations;
    std::memcpy(state.mac, link.mac.data(), sizeof(state.mac));
    std::memcpy(state.station_ssid, link.station_ssid.data(), sizeof(state.station_ssid) - 1U);
    return state;
}

ServiceResult<void> NetworkService::StartAccessPoint(const micropixel_network_access_point_request_t& request) {
    if (!TerminatedText(request.ssid, sizeof(request.ssid)) ||
        !TerminatedText(request.password, sizeof(request.password))) {
        return FailService<void>(MICROPIXEL_STATUS_INVALID_ARGUMENT);
    }
    const uint8_t channel = request.channel == 0U ? 6U : request.channel;
    const auto started = wifi_.StartAccessPoint(request.ssid, request.password, channel, request.max_stations);
    if (!started) {
        return FailService<void>(started.error() == device::WifiError::kInvalidArgument
                                     ? MICROPIXEL_STATUS_INVALID_ARGUMENT
                                     : MICROPIXEL_STATUS_UNSUPPORTED);
    }
    ScopedLock lock(mutex_);
    access_point_started_ = true;
    return {};
}

ServiceResult<void> NetworkService::StopAccessPoint() {
    {
        ScopedLock lock(mutex_);
        if (!access_point_started_) {
            return {};
        }
        access_point_started_ = false;
    }
    (void)wifi_.StopAccessPoint();
    return {};
}

ServiceResult<uint32_t> NetworkService::Advertise(const micropixel_network_advertise_request_t& request,
                                                  const uint8_t* txt, uint32_t txt_length) {
#if MICROPIXEL_NETWORK_HAS_MDNS
    if (!TerminatedText(request.service, sizeof(request.service)) || request.service[0] != '_' ||
        !TerminatedText(request.instance, sizeof(request.instance)) || request.protocol > 1U ||
        request.port == 0U) {
        return FailService<uint32_t>(MICROPIXEL_STATUS_INVALID_ARGUMENT);
    }
    static bool mdns_ready = false;
    if (!mdns_ready) {
        const esp_err_t status = mdns_init();
        if (status != ESP_OK && status != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(kTag, "mDNS unavailable: %s", esp_err_to_name(status));
            return FailService<uint32_t>(MICROPIXEL_STATUS_UNSUPPORTED);
        }
        const device::WifiLinkInfo link = wifi_.LinkInfo();
        char hostname[24];
        std::snprintf(hostname, sizeof(hostname), "micropixel-%02x%02x", link.mac[4], link.mac[5]);
        (void)mdns_hostname_set(hostname);
        mdns_ready = true;
    }
    // TXT record: length-prefixed "key=value" strings, split in place into a
    // copy so the items can point at NUL-terminated keys and values.
    constexpr uint32_t kMaxItems = 24U;
    char text[MICROPIXEL_NETWORK_MAX_TXT + kMaxItems]{};
    mdns_txt_item_t items[kMaxItems]{};
    uint32_t item_count = 0U;
    uint32_t out = 0U;
    for (uint32_t offset = 0U; offset < txt_length && item_count < kMaxItems;) {
        const uint32_t length = txt[offset++];
        if (length == 0U || offset + length > txt_length) {
            return FailService<uint32_t>(MICROPIXEL_STATUS_INVALID_ARGUMENT);
        }
        char* key = text + out;
        std::memcpy(key, txt + offset, length);
        key[length] = '\0';
        out += length + 1U;
        offset += length;
        char* equals = std::strchr(key, '=');
        if (equals != nullptr) {
            *equals = '\0';
        }
        items[item_count++] = mdns_txt_item_t{key, equals != nullptr ? equals + 1 : ""};
    }
    const char* protocol = request.protocol == 0U ? "_tcp" : "_udp";
    ScopedLock lock(mutex_);
    Advertisement* slot = nullptr;
    for (Advertisement& advertisement : advertisements_) {
        if (advertisement.handle == 0U) {
            slot = &advertisement;
            break;
        }
    }
    if (slot == nullptr) {
        return FailService<uint32_t>(MICROPIXEL_STATUS_RESOURCE_EXHAUSTED);
    }
    const esp_err_t status =
        mdns_service_add(request.instance, request.service, protocol, request.port, items, item_count);
    if (status != ESP_OK) {
        ESP_LOGW(kTag, "advert %s.%s failed: %s", request.service, protocol, esp_err_to_name(status));
        return FailService<uint32_t>(MICROPIXEL_STATUS_INTERNAL);
    }
    slot->handle = next_handle_++;
    std::memcpy(slot->service, request.service, sizeof(slot->service));
    std::memcpy(slot->protocol, protocol, sizeof(slot->protocol));
    ESP_LOGI(kTag, "advertising %s.%s \"%s\" on port %u", request.service, protocol, request.instance, request.port);
    return slot->handle;
#else
    (void)request;
    (void)txt;
    (void)txt_length;
    return FailService<uint32_t>(MICROPIXEL_STATUS_UNSUPPORTED);
#endif
}

ServiceResult<void> NetworkService::Withdraw(uint32_t handle) {
    ScopedLock lock(mutex_);
    for (Advertisement& advertisement : advertisements_) {
        if (advertisement.handle == handle && handle != 0U) {
#if MICROPIXEL_NETWORK_HAS_MDNS
            (void)mdns_service_remove(advertisement.service, advertisement.protocol);
#endif
            advertisement = {};
            return {};
        }
    }
    return FailService<void>(MICROPIXEL_STATUS_NOT_FOUND);
}

ServiceResult<uint32_t> NetworkService::Open(uint8_t kind, uint16_t port) {
    if ((kind != MICROPIXEL_NETWORK_SOCKET_TCP_LISTEN && kind != MICROPIXEL_NETWORK_SOCKET_UDP) || port == 0U) {
        return FailService<uint32_t>(MICROPIXEL_STATUS_INVALID_ARGUMENT);
    }
    if (!EnsureTask()) {
        return FailService<uint32_t>(MICROPIXEL_STATUS_RESOURCE_EXHAUSTED);
    }
    const bool stream = kind == MICROPIXEL_NETWORK_SOCKET_TCP_LISTEN;
    const int fd = socket(AF_INET, stream ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (fd < 0) {
        return FailService<uint32_t>(MICROPIXEL_STATUS_RESOURCE_EXHAUSTED);
    }
    const int one = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        (stream && listen(fd, kListenBacklog) != 0)) {
        ESP_LOGW(kTag, "port %u unavailable: errno %d", port, errno);
        close(fd);
        return FailService<uint32_t>(MICROPIXEL_STATUS_PERMISSION_DENIED);
    }
    MakeNonBlocking(fd);
    ScopedLock lock(mutex_);
    auto added = AddLocked(fd, stream ? Kind::kListener : Kind::kDatagram);
    if (!added) {
        close(fd);
    }
    return added;
}

ServiceResult<micropixel_network_accept_response_t> NetworkService::Accept(uint32_t handle) {
    int listener = -1;
    {
        ScopedLock lock(mutex_);
        Socket* socket = FindLocked(handle);
        if (socket == nullptr || socket->kind != Kind::kListener) {
            return FailService<micropixel_network_accept_response_t>(MICROPIXEL_STATUS_NOT_FOUND);
        }
        listener = socket->fd;
    }
    sockaddr_in remote{};
    socklen_t remote_length = sizeof(remote);
    const int fd = accept(listener, reinterpret_cast<sockaddr*>(&remote), &remote_length);
    Rearm(handle);
    if (fd < 0) {
        return FailService<micropixel_network_accept_response_t>(WouldBlock() ? MICROPIXEL_STATUS_WOULD_BLOCK
                                                                              : MICROPIXEL_STATUS_INTERNAL);
    }
    MakeNonBlocking(fd);
    const int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    sockaddr_in local{};
    socklen_t local_length = sizeof(local);
    (void)getsockname(fd, reinterpret_cast<sockaddr*>(&local), &local_length);
    ScopedLock lock(mutex_);
    auto added = AddLocked(fd, Kind::kStream);
    if (!added) {
        close(fd);
        return FailService<micropixel_network_accept_response_t>(added.error().status);
    }
    micropixel_network_accept_response_t response{};
    response.size = sizeof(response);
    response.handle = *added;
    response.remote_address = remote.sin_addr.s_addr;
    response.remote_port = ntohs(remote.sin_port);
    response.local_address = local.sin_addr.s_addr;
    return response;
}

ServiceResult<micropixel_network_receive_response_t> NetworkService::Receive(uint32_t handle, uint8_t* data_out,
                                                                             uint32_t capacity) {
    int fd = -1;
    Kind kind = Kind::kNone;
    {
        ScopedLock lock(mutex_);
        Socket* socket = FindLocked(handle);
        if (socket == nullptr || socket->kind == Kind::kListener) {
            return FailService<micropixel_network_receive_response_t>(MICROPIXEL_STATUS_NOT_FOUND);
        }
        fd = socket->fd;
        kind = socket->kind;
    }
    micropixel_network_receive_response_t response{};
    response.size = sizeof(response);
    sockaddr_in remote{};
    socklen_t remote_length = sizeof(remote);
    const ssize_t received =
        kind == Kind::kDatagram
            ? recvfrom(fd, data_out, capacity, 0, reinterpret_cast<sockaddr*>(&remote), &remote_length)
            : recv(fd, data_out, capacity, 0);
    if (received < 0 && WouldBlock()) {
        Rearm(handle);
        return FailService<micropixel_network_receive_response_t>(MICROPIXEL_STATUS_WOULD_BLOCK);
    }
    if (received <= 0) {
        // EOF or a reset: report it once; the Guest closes the handle.
        response.flags = MICROPIXEL_NETWORK_TRANSFER_CLOSED;
        return response;
    }
    Rearm(handle);
    response.length = static_cast<uint32_t>(received);
    if (kind == Kind::kDatagram) {
        response.remote_address = remote.sin_addr.s_addr;
        response.remote_port = ntohs(remote.sin_port);
    }
    return response;
}

ServiceResult<uint32_t> NetworkService::Send(const micropixel_network_send_request_t& request, const uint8_t* data) {
    int fd = -1;
    Kind kind = Kind::kNone;
    {
        ScopedLock lock(mutex_);
        Socket* socket = FindLocked(request.handle);
        if (socket == nullptr || socket->kind == Kind::kListener) {
            return FailService<uint32_t>(MICROPIXEL_STATUS_NOT_FOUND);
        }
        fd = socket->fd;
        kind = socket->kind;
    }
    ssize_t sent = 0;
    if (kind == Kind::kDatagram) {
        sockaddr_in remote{};
        remote.sin_family = AF_INET;
        remote.sin_port = htons(request.remote_port);
        remote.sin_addr.s_addr = request.remote_address;
        sent = sendto(fd, data, request.length, 0, reinterpret_cast<sockaddr*>(&remote), sizeof(remote));
    } else {
        sent = send(fd, data, request.length, MSG_DONTWAIT);
    }
    if (sent < 0) {
        return FailService<uint32_t>(WouldBlock() ? MICROPIXEL_STATUS_WOULD_BLOCK : MICROPIXEL_STATUS_CLOSED);
    }
    return static_cast<uint32_t>(sent);
}

ServiceResult<void> NetworkService::Close(uint32_t handle) {
    ScopedLock lock(mutex_);
    Socket* socket = FindLocked(handle);
    if (socket == nullptr) {
        return FailService<void>(MICROPIXEL_STATUS_NOT_FOUND);
    }
    socket->armed = false;
    socket->closing = true;  // the task closes it between select() rounds
    socket->handle = 0U;
    return {};
}

void NetworkService::Shutdown() {
    if (!valid()) {
        return;
    }
    for (Advertisement& advertisement : advertisements_) {
        if (advertisement.handle != 0U) {
            (void)Withdraw(advertisement.handle);
        }
    }
    (void)StopAccessPoint();
    running_ = false;
    for (uint32_t waited = 0U; !task_done_.load() && waited < 50U; ++waited) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ScopedLock lock(mutex_);
    for (Socket& socket : sockets_) {
        if (socket.fd >= 0) {
            close(socket.fd);
        }
        socket = {};
    }
    task_ = nullptr;
}

bool NetworkService::EnsureTask() {
    if (running_.load()) {
        return true;
    }
    if (!task_done_.load()) {
        return false;  // the previous task is still winding down
    }
    running_ = true;
    task_done_ = false;
    if (xTaskCreatePinnedToCoreWithCaps(TaskEntry, "guest_net", kTaskStackSize, this, task_policy::kGuestPriority,
                                        &task_, task_policy::kSystemCore,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        running_ = false;
        task_done_ = true;
        task_ = nullptr;
        return false;
    }
    return true;
}

void NetworkService::TaskEntry(void* context) { static_cast<NetworkService*>(context)->Task(); }

void NetworkService::Task() {
    uint32_t rounds = 0U;
    uint32_t last_flags = 0U;
    while (running_.load()) {
        fd_set readable;
        FD_ZERO(&readable);
        int max_fd = -1;
        {
            ScopedLock lock(mutex_);
            for (Socket& socket : sockets_) {
                if (socket.fd >= 0 && socket.closing) {
                    close(socket.fd);
                    socket = {};
                } else if (socket.fd >= 0 && socket.armed) {
                    FD_SET(socket.fd, &readable);
                    max_fd = std::max(max_fd, socket.fd);
                }
            }
        }
        if (max_fd >= 0) {
            timeval timeout{0, static_cast<suseconds_t>(kSelectTimeoutMs * 1000U)};
            if (select(max_fd + 1, &readable, nullptr, nullptr, &timeout) > 0) {
                uint32_t ready[kMaxSockets]{};
                uint32_t ready_count = 0U;
                {
                    ScopedLock lock(mutex_);
                    for (Socket& socket : sockets_) {
                        if (socket.fd >= 0 && socket.armed && !socket.closing && FD_ISSET(socket.fd, &readable)) {
                            socket.armed = false;
                            ready[ready_count++] = socket.handle;
                        }
                    }
                }
                for (uint32_t index = 0U; index < ready_count; ++index) {
                    micropixel_network_ready_event_payload_t payload{};
                    payload.handle = ready[index];
                    Post(MICROPIXEL_NETWORK_EVENT_READY, &payload, sizeof(payload));
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(kSelectTimeoutMs));
        }
        if (++rounds % kStateCheckEvery == 0U) {
            const uint32_t flags = Flags();
            if (flags != last_flags) {
                last_flags = flags;
                micropixel_network_state_event_payload_t payload{};
                payload.flags = flags;
                Post(MICROPIXEL_NETWORK_EVENT_STATE, &payload, sizeof(payload));
            }
        }
    }
    task_done_ = true;
    vTaskDeleteWithCaps(nullptr);
}

NetworkService::Socket* NetworkService::FindLocked(uint32_t handle) {
    if (handle == 0U) {
        return nullptr;
    }
    for (Socket& socket : sockets_) {
        if (socket.handle == handle && socket.fd >= 0 && !socket.closing) {
            return &socket;
        }
    }
    return nullptr;
}

ServiceResult<uint32_t> NetworkService::AddLocked(int fd, Kind kind) {
    for (Socket& socket : sockets_) {
        if (socket.fd < 0) {
            socket.fd = fd;
            socket.kind = kind;
            socket.handle = next_handle_++;
            socket.armed = true;
            socket.closing = false;
            return socket.handle;
        }
    }
    return FailService<uint32_t>(MICROPIXEL_STATUS_RESOURCE_EXHAUSTED);
}

void NetworkService::Rearm(uint32_t handle) {
    ScopedLock lock(mutex_);
    if (Socket* socket = FindLocked(handle)) {
        socket->armed = true;
    }
}

void NetworkService::Post(uint16_t event_id, const void* payload, uint32_t payload_size) {
    micropixel_event_t event{};
    event.size = sizeof(event);
    event.event_id = event_id;
    event.service_id = MICROPIXEL_SERVICE_NETWORK;
    event.source = 1U;
    event.timestamp_us = clock_.Now();
    event.sequence = event_sequence_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    event.status = MICROPIXEL_STATUS_OK;
    std::memcpy(event.payload, payload, payload_size);
    if (!events_.PushAdvisory(event) && event_id == MICROPIXEL_NETWORK_EVENT_READY) {
        // A full queue would lose this READY; let the next round post it again.
        micropixel_network_ready_event_payload_t ready{};
        std::memcpy(&ready, payload, sizeof(ready));
        Rearm(ready.handle);
    }
}

}  // namespace micropixel::runtime
