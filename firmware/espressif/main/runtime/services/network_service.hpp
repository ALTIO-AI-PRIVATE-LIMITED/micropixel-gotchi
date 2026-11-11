#ifndef MICROPIXEL_RUNTIME_SERVICES_NETWORK_SERVICE_HPP
#define MICROPIXEL_RUNTIME_SERVICES_NETWORK_SERVICE_HPP

#include <atomic>
#include <cstdint>

#include "abi/micropixel_abi.h"
#include "device/contracts/wifi.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "runtime/event_queue.hpp"
#include "runtime/services/service_result.hpp"

namespace micropixel::runtime {

class TimerService;

// Network 1.0: non-blocking IPv4 sockets, a Guest hotspot and DNS-SD
// adverts for one session. A Host task select()s on the sockets the Guest has
// asked about since their last READY and posts READY when one turns readable,
// so the Guest never polls. Closing is deferred to that task so a socket is
// never closed under select(). Everything is torn down at session end.
class NetworkService final {
   public:
    static constexpr uint32_t kMaxSockets = 10U;
    static constexpr uint32_t kMaxAdvertisements = 4U;

    NetworkService(device::Wifi& wifi, EventQueue& events, TimerService& clock);
    NetworkService(const NetworkService&) = delete;
    NetworkService& operator=(const NetworkService&) = delete;
    ~NetworkService();

    [[nodiscard]] bool valid() const { return mutex_ != nullptr; }  // NOLINT(readability-identifier-naming)
    [[nodiscard]] micropixel_network_state_t State() const;
    [[nodiscard]] ServiceResult<void> StartAccessPoint(const micropixel_network_access_point_request_t& request);
    [[nodiscard]] ServiceResult<void> StopAccessPoint();
    [[nodiscard]] ServiceResult<uint32_t> Advertise(const micropixel_network_advertise_request_t& request,
                                                    const uint8_t* txt, uint32_t txt_length);
    [[nodiscard]] ServiceResult<void> Withdraw(uint32_t handle);
    [[nodiscard]] ServiceResult<uint32_t> Open(uint8_t kind, uint16_t port);
    [[nodiscard]] ServiceResult<micropixel_network_accept_response_t> Accept(uint32_t handle);
    // Reads into `data_out` (Guest memory, `capacity`).
    [[nodiscard]] ServiceResult<micropixel_network_receive_response_t> Receive(uint32_t handle, uint8_t* data_out,
                                                                               uint32_t capacity);
    [[nodiscard]] ServiceResult<uint32_t> Send(const micropixel_network_send_request_t& request,
                                               const uint8_t* data);
    [[nodiscard]] ServiceResult<void> Close(uint32_t handle);
    void Shutdown();

   private:
    enum class Kind : uint8_t {
        kNone,
        kListener,
        kStream,
        kDatagram,
    };

    struct Socket final {
        int fd{-1};
        uint32_t handle{};
        Kind kind{Kind::kNone};
        bool armed{};
        bool closing{};
    };

    struct Advertisement final {
        uint32_t handle{};
        char service[16]{};
        char protocol[5]{};
    };

    static void TaskEntry(void* context);
    void Task();
    [[nodiscard]] bool EnsureTask();
    [[nodiscard]] Socket* FindLocked(uint32_t handle);
    [[nodiscard]] ServiceResult<uint32_t> AddLocked(int fd, Kind kind);
    void Rearm(uint32_t handle);
    [[nodiscard]] uint32_t Flags() const;
    void Post(uint16_t event_id, const void* payload, uint32_t payload_size);

    device::Wifi& wifi_;
    EventQueue& events_;
    TimerService& clock_;
    SemaphoreHandle_t mutex_{};
    // Guarded by mutex_.
    Socket sockets_[kMaxSockets]{};
    Advertisement advertisements_[kMaxAdvertisements]{};
    uint32_t next_handle_{1U};
    bool access_point_started_{};
    TaskHandle_t task_{};
    std::atomic<bool> running_{};
    std::atomic<bool> task_done_{true};
    std::atomic<uint32_t> event_sequence_{0U};
};

}  // namespace micropixel::runtime

#endif
