#include "sdk/devices.hpp"

#include "runtime/service_binding.hpp"
#include "sdk/ble_link.hpp"
#include "sdk/gpio.hpp"
#include "sdk/haptics.hpp"
#include "sdk/hid_keyboard.hpp"
#include "sdk/microphone.hpp"
#include "sdk/network.hpp"
#include "sdk/peer_radio.hpp"
#include "sdk/power_info.hpp"
#include "sdk/sensors.hpp"

using micropixel::runtime::CallService;
using micropixel::runtime::CallVoid;
using micropixel::runtime::CopyBytes;
using micropixel::runtime::ErrorFromStatus;
using micropixel::runtime::OpenService;
using micropixel::runtime::ServiceCache;

namespace {

ServiceCache devices_service;
ServiceCache sensors_service;
ServiceCache gpio_service;
ServiceCache haptics_service;
ServiceCache power_info_service;
ServiceCache hid_keyboard_service;

int32_t OpenHidKeyboardService() {
    return OpenService(hid_keyboard_service, MICROPIXEL_SERVICE_HID_KEYBOARD, MICROPIXEL_HID_KEYBOARD_INTERFACE_MAJOR,
                       MICROPIXEL_HID_KEYBOARD_INTERFACE_MINOR);
}

ServiceCache microphone_service;

int32_t OpenMicrophoneService() {
    return OpenService(microphone_service, MICROPIXEL_SERVICE_MICROPHONE, MICROPIXEL_MICROPHONE_INTERFACE_MAJOR,
                       MICROPIXEL_MICROPHONE_INTERFACE_MINOR);
}

ServiceCache peer_radio_service;

int32_t OpenPeerRadioService() {
    return OpenService(peer_radio_service, MICROPIXEL_SERVICE_PEER_RADIO, MICROPIXEL_PEER_RADIO_INTERFACE_MAJOR,
                       MICROPIXEL_PEER_RADIO_INTERFACE_MINOR);
}

micropixel::Result<void> PeerRadioVoid(uint32_t method_id, const void* request, uint32_t request_size) {
    int32_t status = OpenPeerRadioService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(peer_radio_service, method_id, request, request_size);
    }
    return status == MICROPIXEL_STATUS_OK ? micropixel::Result<void>{}
                                          : micropixel::Result<void>{micropixel::unexpected(ErrorFromStatus(status))};
}

micropixel::Result<void> VoidResult(int32_t status) {
    return status == MICROPIXEL_STATUS_OK ? micropixel::Result<void>{}
                                          : micropixel::Result<void>{micropixel::unexpected(ErrorFromStatus(status))};
}

ServiceCache ble_link_service;

int32_t OpenBleLinkService() {
    return OpenService(ble_link_service, MICROPIXEL_SERVICE_BLE_LINK, MICROPIXEL_BLE_LINK_INTERFACE_MAJOR,
                       MICROPIXEL_BLE_LINK_INTERFACE_MINOR);
}

ServiceCache network_service;

int32_t OpenNetworkService() {
    return OpenService(network_service, MICROPIXEL_SERVICE_NETWORK, MICROPIXEL_NETWORK_INTERFACE_MAJOR,
                       MICROPIXEL_NETWORK_INTERFACE_MINOR);
}

// Copies a NUL-terminated string into a fixed field; false when it does not fit.
bool CopyText(char* destination, uint32_t capacity, const char* source) {
    if (source == nullptr) {
        return false;
    }
    uint32_t length = 0U;
    while (source[length] != '\0') {
        if (++length >= capacity) {
            return false;
        }
    }
    CopyBytes(destination, source, length + 1U);
    return true;
}

micropixel::Result<uint32_t> NetworkHandleCall(uint32_t method_id, const void* request, uint32_t request_size) {
    int32_t status = OpenNetworkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return micropixel::unexpected(ErrorFromStatus(status));
    }
    micropixel_network_handle_message_t response{};
    uint32_t response_size = 0U;
    status = CallService(network_service, method_id, request, request_size, &response, sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return micropixel::unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size != sizeof(response) || response.handle == 0U) {
        micropixel::runtime::Panic("network.handle.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return response.handle;
}

micropixel::Result<void> NetworkHandleVoid(uint32_t method_id, uint32_t handle) {
    int32_t status = OpenNetworkService();
    if (status == MICROPIXEL_STATUS_OK) {
        micropixel_network_handle_message_t request{};
        request.size = sizeof(request);
        request.handle = handle;
        status = CallVoid(network_service, method_id, &request, sizeof(request));
    }
    return VoidResult(status);
}

micropixel::Result<uint32_t> NetworkSend(uint32_t socket, const void* data, uint32_t length, uint32_t address,
                                         uint16_t port) {
    if (data == nullptr || length == 0U || socket == 0U) {
        return micropixel::unexpected(micropixel::Error{micropixel::ErrorCode::kInvalidArgument});
    }
    if (length > micropixel::Network::kMaxTransfer) {
        length = micropixel::Network::kMaxTransfer;
    }
    int32_t status = OpenNetworkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return micropixel::unexpected(ErrorFromStatus(status));
    }
    alignas(4) static uint8_t request[sizeof(micropixel_network_send_request_t) + micropixel::Network::kMaxTransfer];
    micropixel_network_send_request_t header{};
    header.size = sizeof(header);
    header.handle = socket;
    header.length = length;
    header.remote_address = address;
    header.remote_port = port;
    CopyBytes(request, &header, sizeof(header));
    CopyBytes(request + sizeof(header), data, length);
    micropixel_network_send_response_t response{};
    uint32_t response_size = 0U;
    status = CallService(network_service, MICROPIXEL_NETWORK_METHOD_SEND, request, sizeof(header) + length, &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return micropixel::unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size != sizeof(response) || response.sent > length) {
        micropixel::runtime::Panic("network.send.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return response.sent;
}

}  // namespace

namespace micropixel {

Result<DeviceList> Devices::List(DeviceKind kind) const {
    int32_t status = OpenService(devices_service, MICROPIXEL_SERVICE_DEVICES, MICROPIXEL_DEVICES_INTERFACE_MAJOR,
                                 MICROPIXEL_DEVICES_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    // Walk the pages; a generation change mid-walk means the registry
    // changed under us, so start over a bounded number of times.
    for (uint32_t attempt = 0U; attempt < 4U; ++attempt) {
        DeviceList list{};
        uint32_t first_index = 0U;
        bool restart = false;
        do {
            micropixel_devices_list_request_t request{};
            request.size = sizeof(request);
            request.kind = static_cast<uint16_t>(kind);
            request.first_index = static_cast<uint16_t>(first_index);
            micropixel_devices_list_response_t response{};
            uint32_t response_size = 0U;
            status = CallService(devices_service, MICROPIXEL_DEVICES_METHOD_LIST, &request, sizeof(request), &response,
                                 sizeof(response), response_size);
            if (status != MICROPIXEL_STATUS_OK) {
                return unexpected(ErrorFromStatus(status));
            }
            if (response_size != sizeof(response) || response.size != sizeof(response) ||
                response.count > MICROPIXEL_DEVICES_LIST_PAGE_SIZE || response.reserved0 != 0U ||
                first_index + response.count > response.total_count ||
                (response.count == 0U && first_index < response.total_count)) {
                runtime::Panic("devices.list.response", MICROPIXEL_STATUS_INTERNAL);
            }
            if (first_index == 0U) {
                list.generation_ = response.generation;
            } else if (response.generation != list.generation_) {
                restart = true;
                break;
            }
            if (response.total_count > DeviceList::kCapacity) {
                return unexpected(Error{ErrorCode::kResourceExhausted});
            }
            for (uint32_t index = 0U; index < response.count; ++index) {
                const uint32_t slot = first_index + index;
                if (response.devices[index] == 0U) {
                    runtime::Panic("devices.list.id", MICROPIXEL_STATUS_INTERNAL);
                }
                for (uint32_t previous = 0U; previous < slot; ++previous) {
                    if (list.devices_[previous].value() == response.devices[index]) {
                        runtime::Panic("devices.list.duplicate", MICROPIXEL_STATUS_INTERNAL);
                    }
                }
                list.devices_[slot] = DeviceId{response.devices[index]};
            }
            first_index += response.count;
            list.count_ = first_index;
            if (first_index >= response.total_count) {
                return list;
            }
        } while (true);
        if (!restart) {
            break;
        }
    }
    return unexpected(Error{ErrorCode::kInvalidState});
}

Result<DeviceInfo> Devices::GetInfo(DeviceId device) const {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(devices_service, MICROPIXEL_SERVICE_DEVICES, MICROPIXEL_DEVICES_INTERFACE_MAJOR,
                                 MICROPIXEL_DEVICES_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_device_request_t request{sizeof(request), 0U, device.value()};
    micropixel_device_info_t response{};
    uint32_t response_size = 0U;
    status = CallService(devices_service, MICROPIXEL_DEVICES_METHOD_GET_INFO, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.device != device.value() ||
        response.kind == MICROPIXEL_DEVICE_KIND_ANY || response.kind > MICROPIXEL_DEVICE_KIND_NETWORK ||
        response.reserved0 != 0U || response.reserved1 != 0U ||
        response.name_length > MICROPIXEL_DEVICE_NAME_MAX_BYTES || response.name[response.name_length] != '\0') {
        runtime::Panic("devices.info.response", MICROPIXEL_STATUS_INTERNAL);
    }
    DeviceInfo info{};
    info.id = device;
    info.parent = DeviceId{response.parent};
    info.kind = static_cast<DeviceKind>(response.kind);
    info.capabilities = response.capabilities;
    if (!info.name.Append(response.name)) {
        runtime::Panic("devices.info.name", MICROPIXEL_STATUS_INTERNAL);
    }
    return info;
}

Result<SensorInfo> Sensors::GetInfo(DeviceId device) const {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(sensors_service, MICROPIXEL_SERVICE_SENSORS, MICROPIXEL_SENSORS_INTERFACE_MAJOR,
                                 MICROPIXEL_SENSORS_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_device_request_t request{sizeof(request), 0U, device.value()};
    micropixel_sensor_info_t response{};
    uint32_t response_size = 0U;
    status = CallService(sensors_service, MICROPIXEL_SENSORS_METHOD_GET_INFO, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.device != device.value() ||
        response.kind < MICROPIXEL_SENSOR_ACCELERATION || response.kind > MICROPIXEL_SENSOR_ORIENTATION ||
        response.placement > MICROPIXEL_SENSOR_PLACEMENT_RIGHT || response.value_count == 0U ||
        response.value_count > 4U || response.min_interval_us == 0U ||
        response.max_interval_us < response.min_interval_us || response.reserved0[0] != 0U ||
        response.reserved0[1] != 0U) {
        runtime::Panic("sensors.info.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return SensorInfo{device,
                      DeviceId{response.parent},
                      static_cast<SensorKind>(response.kind),
                      static_cast<SensorPlacement>(response.placement),
                      response.value_count,
                      Duration::Microseconds(response.min_interval_us),
                      Duration::Microseconds(response.max_interval_us)};
}

namespace detail {

Result<SensorOpenResult> OpenSensor(DeviceId device, SensorKind expected_kind) {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(sensors_service, MICROPIXEL_SERVICE_SENSORS, MICROPIXEL_SENSORS_INTERFACE_MAJOR,
                                 MICROPIXEL_SENSORS_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_sensor_open_request_t request{};
    request.size = sizeof(request);
    request.expected_kind = static_cast<uint16_t>(expected_kind);
    request.device = device.value();
    micropixel_sensor_open_response_t response{};
    uint32_t response_size = 0U;
    status = CallService(sensors_service, MICROPIXEL_SENSORS_METHOD_OPEN, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.sensor_handle == 0U ||
        response.device != device.value() || response.kind != static_cast<uint16_t>(expected_kind) ||
        response.reserved0 != 0U) {
        runtime::Panic("sensors.open.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return SensorOpenResult{response.sensor_handle, device, expected_kind};
}

Result<SensorReadResult> ReadSensor(uint32_t handle, DeviceId device, SensorKind expected_kind) {
    // Sensor<Reading> owns the handle, device and kind; the Host validates
    // handle ownership and generation, so no Guest-side mirror is kept.
    if (handle == 0U) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    micropixel_handle_request_t request{sizeof(request), 0U, handle};
    micropixel_sensor_reading_t response{};
    uint32_t response_size = 0U;
    const int32_t status = CallService(sensors_service, MICROPIXEL_SENSORS_METHOD_READ, &request, sizeof(request),
                                       &response, sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.sensor_handle != handle ||
        response.device != device.value() || response.kind != static_cast<uint16_t>(expected_kind) ||
        response.reserved0 != 0U) {
        runtime::Panic("sensors.read.response", MICROPIXEL_STATUS_INTERNAL);
    }
    SensorReadResult result{};
    result.timestamp_us = response.timestamp_us;
    CopyBytes(result.values, response.values, sizeof(result.values));
    return result;
}

Result<Duration> SetSensorSampleInterval(uint32_t handle, Duration interval) {
    if (handle == 0U || interval.count_microseconds() == 0U) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    micropixel_sensor_sample_interval_request_t request{};
    request.size = sizeof(request);
    request.sensor_handle = handle;
    request.interval_us = interval.count_microseconds();
    const int32_t status =
        CallVoid(sensors_service, MICROPIXEL_SENSORS_METHOD_SET_SAMPLE_INTERVAL, &request, sizeof(request));
    return status == MICROPIXEL_STATUS_OK ? Result<Duration>{interval} : unexpected(ErrorFromStatus(status));
}

void ReleaseSensor(uint32_t handle) {
    if (handle == 0U) {
        return;
    }
    micropixel_handle_request_t request{sizeof(request), 0U, handle};
    if (OpenService(sensors_service, MICROPIXEL_SERVICE_SENSORS, MICROPIXEL_SENSORS_INTERFACE_MAJOR,
                    MICROPIXEL_SENSORS_INTERFACE_MINOR) == MICROPIXEL_STATUS_OK) {
        (void)CallVoid(sensors_service, MICROPIXEL_SENSORS_METHOD_CLOSE, &request, sizeof(request));
    }
}

Result<GpioOpenResult> OpenGpio(DeviceId device, uint16_t mode, uint16_t pull, uint16_t edge, uint32_t initial_value,
                                uint32_t pwm_frequency_hz) {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(gpio_service, MICROPIXEL_SERVICE_GPIO, MICROPIXEL_GPIO_INTERFACE_MAJOR,
                                 MICROPIXEL_GPIO_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_gpio_open_request_t request{};
    request.size = sizeof(request);
    request.mode = mode;
    request.device = device.value();
    request.pull = pull;
    request.edge = edge;
    request.initial_value = initial_value;
    request.pwm_frequency_hz = pwm_frequency_hz;
    micropixel_gpio_open_response_t response{};
    uint32_t response_size = 0U;
    status = CallService(gpio_service, MICROPIXEL_GPIO_METHOD_OPEN, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.gpio_handle == 0U ||
        response.device != device.value() || response.mode != mode || response.reserved0 != 0U) {
        runtime::Panic("gpio.open.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return GpioOpenResult{response.gpio_handle, device};
}

Result<bool> ReadGpio(uint32_t handle) {
    if (handle == 0U) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    micropixel_handle_request_t request{sizeof(request), 0U, handle};
    micropixel_gpio_value_response_t response{};
    uint32_t response_size = 0U;
    const int32_t status = CallService(gpio_service, MICROPIXEL_GPIO_METHOD_READ, &request, sizeof(request), &response,
                                       sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.gpio_handle != handle ||
        response.reserved0 != 0U || response.value > 1U) {
        runtime::Panic("gpio.read.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return response.value != 0U;
}

Result<void> WriteGpio(uint32_t handle, bool value) {
    micropixel_gpio_value_request_t request{sizeof(request), 0U, handle, value ? 1U : 0U};
    const int32_t status = CallVoid(gpio_service, MICROPIXEL_GPIO_METHOD_WRITE, &request, sizeof(request));
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<void> SetGpioPwmDuty(uint32_t handle, uint16_t duty_per_mille) {
    micropixel_gpio_value_request_t request{sizeof(request), 0U, handle, duty_per_mille};
    const int32_t status = CallVoid(gpio_service, MICROPIXEL_GPIO_METHOD_SET_PWM_DUTY, &request, sizeof(request));
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

void ReleaseGpio(uint32_t handle) {
    if (handle == 0U) {
        return;
    }
    micropixel_handle_request_t request{sizeof(request), 0U, handle};
    if (OpenService(gpio_service, MICROPIXEL_SERVICE_GPIO, MICROPIXEL_GPIO_INTERFACE_MAJOR,
                    MICROPIXEL_GPIO_INTERFACE_MINOR) == MICROPIXEL_STATUS_OK) {
        (void)CallVoid(gpio_service, MICROPIXEL_GPIO_METHOD_CLOSE, &request, sizeof(request));
    }
}

Result<uint32_t> OpenHaptic(DeviceId device) {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(haptics_service, MICROPIXEL_SERVICE_HAPTICS, MICROPIXEL_HAPTICS_INTERFACE_MAJOR,
                                 MICROPIXEL_HAPTICS_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_device_request_t request{sizeof(request), 0U, device.value()};
    micropixel_handle_response_t response{};
    uint32_t response_size = 0U;
    status = CallService(haptics_service, MICROPIXEL_HAPTICS_METHOD_OPEN, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.reserved0 != 0U ||
        response.handle == 0U) {
        runtime::Panic("haptics.open.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return response.handle;
}

Result<void> PlayHaptic(uint32_t handle, Duration duration, uint16_t strength_per_mille) {
    const uint64_t duration_us = duration.count_microseconds();
    if (handle == 0U || strength_per_mille == 0U || strength_per_mille > 1000U || duration_us == 0U ||
        duration_us > static_cast<uint64_t>(UINT32_MAX) * 1000U) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    micropixel_haptics_play_request_t request{};
    request.size = sizeof(request);
    request.strength_per_mille = strength_per_mille;
    request.haptics_handle = handle;
    request.duration_ms = static_cast<uint32_t>((duration_us + 999U) / 1000U);
    const int32_t status = CallVoid(haptics_service, MICROPIXEL_HAPTICS_METHOD_PLAY, &request, sizeof(request));
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<void> StopHaptic(uint32_t handle) {
    micropixel_handle_request_t request{sizeof(request), 0U, handle};
    const int32_t status = CallVoid(haptics_service, MICROPIXEL_HAPTICS_METHOD_STOP, &request, sizeof(request));
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

void ReleaseHaptic(uint32_t handle) {
    if (handle == 0U) {
        return;
    }
    micropixel_handle_request_t request{sizeof(request), 0U, handle};
    if (OpenService(haptics_service, MICROPIXEL_SERVICE_HAPTICS, MICROPIXEL_HAPTICS_INTERFACE_MAJOR,
                    MICROPIXEL_HAPTICS_INTERFACE_MINOR) == MICROPIXEL_STATUS_OK) {
        (void)CallVoid(haptics_service, MICROPIXEL_HAPTICS_METHOD_CLOSE, &request, sizeof(request));
    }
}

}  // namespace detail

Result<GpioInfo> Gpio::GetInfo(DeviceId device) const {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(gpio_service, MICROPIXEL_SERVICE_GPIO, MICROPIXEL_GPIO_INTERFACE_MAJOR,
                                 MICROPIXEL_GPIO_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_device_request_t request{sizeof(request), 0U, device.value()};
    micropixel_gpio_info_t response{};
    uint32_t response_size = 0U;
    status = CallService(gpio_service, MICROPIXEL_GPIO_METHOD_GET_INFO, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.device != device.value() ||
        response.line_number == 0U || response.reserved0[0] != 0U || response.reserved0[1] != 0U ||
        response.reserved0[2] != 0U) {
        runtime::Panic("gpio.info.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return GpioInfo{device, response.line_number, response.capabilities, response.max_pwm_frequency_hz};
}

Result<GpioInput> Gpio::OpenInput(DeviceId device, GpioInputOptions options) const {
    auto result = detail::OpenGpio(device, MICROPIXEL_GPIO_MODE_INPUT, static_cast<uint16_t>(options.pull),
                                   static_cast<uint16_t>(options.edge), 0U, 0U);
    return result ? Result<GpioInput>{GpioInput{result->handle, result->device}}
                  : Result<GpioInput>{unexpected(result.error())};
}

Result<GpioOutput> Gpio::OpenOutput(DeviceId device, bool initial_value) const {
    auto result = detail::OpenGpio(device, MICROPIXEL_GPIO_MODE_OUTPUT, MICROPIXEL_GPIO_PULL_NONE,
                                   MICROPIXEL_GPIO_EDGE_NONE, initial_value ? 1U : 0U, 0U);
    return result ? Result<GpioOutput>{GpioOutput{result->handle, result->device}}
                  : Result<GpioOutput>{unexpected(result.error())};
}

Result<GpioPwm> Gpio::OpenPwm(DeviceId device, uint32_t frequency_hz, uint16_t initial_duty_per_mille) const {
    auto result = detail::OpenGpio(device, MICROPIXEL_GPIO_MODE_PWM, MICROPIXEL_GPIO_PULL_NONE,
                                   MICROPIXEL_GPIO_EDGE_NONE, initial_duty_per_mille, frequency_hz);
    return result ? Result<GpioPwm>{GpioPwm{result->handle, result->device}}
                  : Result<GpioPwm>{unexpected(result.error())};
}

Result<HapticsInfo> Haptics::GetInfo(DeviceId device) const {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(haptics_service, MICROPIXEL_SERVICE_HAPTICS, MICROPIXEL_HAPTICS_INTERFACE_MAJOR,
                                 MICROPIXEL_HAPTICS_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_device_request_t request{sizeof(request), 0U, device.value()};
    micropixel_haptics_info_t response{};
    uint32_t response_size = 0U;
    status = CallService(haptics_service, MICROPIXEL_HAPTICS_METHOD_GET_INFO, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.device != device.value() ||
        response.max_duration_ms == 0U || response.reserved0 != 0U || response.reserved1 != 0U) {
        runtime::Panic("haptics.info.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return HapticsInfo{device, Duration::Milliseconds(response.max_duration_ms),
                       (response.capabilities & MICROPIXEL_HAPTICS_CAP_VARIABLE_STRENGTH) != 0U};
}

Result<Haptic> Haptics::Open(DeviceId device) const {
    auto result = detail::OpenHaptic(device);
    return result ? Result<Haptic>{Haptic{*result, device}} : Result<Haptic>{unexpected(result.error())};
}

Result<PowerState> PowerInfo::Get(DeviceId device) const {
    if (!device.valid()) {
        return unexpected(ErrorFromStatus(MICROPIXEL_STATUS_INVALID_ARGUMENT));
    }
    int32_t status = OpenService(power_info_service, MICROPIXEL_SERVICE_POWER, MICROPIXEL_POWER_INTERFACE_MAJOR,
                                 MICROPIXEL_POWER_INTERFACE_MINOR);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_device_request_t request{sizeof(request), 0U, device.value()};
    micropixel_power_info_t response{};
    uint32_t response_size = 0U;
    status = CallService(power_info_service, MICROPIXEL_POWER_METHOD_GET_INFO, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    constexpr uint32_t kKnownFlags = MICROPIXEL_POWER_STATE_HAS_BATTERY | MICROPIXEL_POWER_STATE_CHARGING |
                                     MICROPIXEL_POWER_STATE_DISCHARGING | MICROPIXEL_POWER_STATE_EXTERNAL_CONNECTED;
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.device != device.value() ||
        response.source > MICROPIXEL_POWER_SOURCE_EXTERNAL || response.battery_percent > 100U ||
        (response.flags & ~kKnownFlags) != 0U || response.reserved0[0] != 0U || response.reserved0[1] != 0U ||
        response.reserved0[2] != 0U || response.reserved1[0] != 0U || response.reserved1[1] != 0U) {
        runtime::Panic("power.info.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return PowerState{device,
                      static_cast<PowerSource>(response.source),
                      response.battery_percent,
                      (response.flags & MICROPIXEL_POWER_STATE_HAS_BATTERY) != 0U,
                      (response.flags & MICROPIXEL_POWER_STATE_CHARGING) != 0U,
                      (response.flags & MICROPIXEL_POWER_STATE_DISCHARGING) != 0U,
                      (response.flags & MICROPIXEL_POWER_STATE_EXTERNAL_CONNECTED) != 0U};
}

Result<HidKeyboardState> HidKeyboard::State() const {
    int32_t status = OpenHidKeyboardService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_hid_keyboard_state_t response{};
    uint32_t response_size = 0U;
    status = CallService(hid_keyboard_service, MICROPIXEL_HID_KEYBOARD_METHOD_GET_STATE, nullptr, 0U, &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    constexpr uint32_t kKnownFlags = MICROPIXEL_HID_KEYBOARD_STATE_AVAILABLE | MICROPIXEL_HID_KEYBOARD_STATE_STARTED |
                                     MICROPIXEL_HID_KEYBOARD_STATE_ADVERTISING |
                                     MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED;
    if (response_size < sizeof(response) || response.size < sizeof(response) || response.reserved0 != 0U ||
        (response.flags & ~kKnownFlags) != 0U) {
        runtime::Panic("hid_keyboard.state.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return HidKeyboardState{(response.flags & MICROPIXEL_HID_KEYBOARD_STATE_AVAILABLE) != 0U,
                            (response.flags & MICROPIXEL_HID_KEYBOARD_STATE_STARTED) != 0U,
                            (response.flags & MICROPIXEL_HID_KEYBOARD_STATE_ADVERTISING) != 0U,
                            (response.flags & MICROPIXEL_HID_KEYBOARD_STATE_CONNECTED) != 0U};
}

Result<void> HidKeyboard::Start() const {
    int32_t status = OpenHidKeyboardService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(hid_keyboard_service, MICROPIXEL_HID_KEYBOARD_METHOD_START, nullptr, 0U);
    }
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<void> HidKeyboard::Key(HidKeyUsage usage, bool pressed, bool apple_fn) const {
    int32_t status = OpenHidKeyboardService();
    if (status == MICROPIXEL_STATUS_OK) {
        micropixel_hid_keyboard_key_request_t request{};
        request.size = sizeof(request);
        request.usage = static_cast<uint8_t>(usage);
        request.pressed = pressed ? 1U : 0U;
        request.flags = apple_fn ? static_cast<uint32_t>(MICROPIXEL_HID_KEYBOARD_KEY_APPLE_FN) : 0U;
        status = CallVoid(hid_keyboard_service, MICROPIXEL_HID_KEYBOARD_METHOD_KEY, &request, sizeof(request));
    }
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<void> HidKeyboard::Consumer(HidConsumerUsage usage, bool pressed) const {
    int32_t status = OpenHidKeyboardService();
    if (status == MICROPIXEL_STATUS_OK) {
        micropixel_hid_keyboard_consumer_request_t request{};
        request.size = sizeof(request);
        request.usage = static_cast<uint16_t>(usage);
        request.pressed = pressed ? 1U : 0U;
        status = CallVoid(hid_keyboard_service, MICROPIXEL_HID_KEYBOARD_METHOD_CONSUMER, &request, sizeof(request));
    }
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<void> HidKeyboard::Mouse(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel, int8_t pan) const {
    int32_t status = OpenHidKeyboardService();
    if (status == MICROPIXEL_STATUS_OK) {
        micropixel_hid_keyboard_mouse_request_t request{};
        request.size = sizeof(request);
        request.buttons = buttons;
        request.dx = dx;
        request.dy = dy;
        request.wheel = wheel;
        request.pan = pan;
        status = CallVoid(hid_keyboard_service, MICROPIXEL_HID_KEYBOARD_METHOD_MOUSE, &request, sizeof(request));
    }
    return VoidResult(status);
}

Result<void> HidKeyboard::ForgetBonds() const {
    int32_t status = OpenHidKeyboardService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(hid_keyboard_service, MICROPIXEL_HID_KEYBOARD_METHOD_FORGET_BONDS, nullptr, 0U);
    }
    return VoidResult(status);
}

Result<BleLinkState> BleLink::State() const {
    int32_t status = OpenBleLinkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_ble_link_state_t response{};
    uint32_t response_size = 0U;
    status = CallService(ble_link_service, MICROPIXEL_BLE_LINK_METHOD_GET_STATE, nullptr, 0U, &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response)) {
        runtime::Panic("ble_link.state.response", MICROPIXEL_STATUS_INTERNAL);
    }
    BleLinkState state{};
    state.available = (response.flags & MICROPIXEL_BLE_LINK_STATE_AVAILABLE) != 0U;
    state.started = (response.flags & MICROPIXEL_BLE_LINK_STATE_STARTED) != 0U;
    state.connected = (response.flags & MICROPIXEL_BLE_LINK_STATE_CONNECTED) != 0U;
    state.encrypted = (response.flags & MICROPIXEL_BLE_LINK_STATE_ENCRYPTED) != 0U;
    state.reply_subscribed = (response.flags & MICROPIXEL_BLE_LINK_STATE_SUBSCRIBED_REPLY) != 0U;
    state.status_subscribed = (response.flags & MICROPIXEL_BLE_LINK_STATE_SUBSCRIBED_STATUS) != 0U;
    state.bulk_subscribed = (response.flags & MICROPIXEL_BLE_LINK_STATE_SUBSCRIBED_BULK) != 0U;
    state.mtu = response.mtu;
    state.bulk_payload = response.bulk_payload;
    state.phy = response.phy;
    state.peers = response.peers;
    state.bulk_drops = response.bulk_drops;
    state.reply_drops = response.reply_drops;
    state.bulk_kbps = response.bulk_kbps;
    return state;
}

Result<void> BleLink::Start() const {
    int32_t status = OpenBleLinkService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(ble_link_service, MICROPIXEL_BLE_LINK_METHOD_START, nullptr, 0U);
    }
    return VoidResult(status);
}

Result<void> BleLink::Stop() const {
    int32_t status = OpenBleLinkService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(ble_link_service, MICROPIXEL_BLE_LINK_METHOD_STOP, nullptr, 0U);
    }
    return VoidResult(status);
}

Result<BleLinkMessage> BleLink::Read(uint8_t (&data)[kMaxMessage]) const {
    int32_t status = OpenBleLinkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    using Header = micropixel_ble_link_read_response_t;
    alignas(4) static uint8_t response[sizeof(Header) + kMaxMessage];
    uint32_t response_size = 0U;
    status = CallService(ble_link_service, MICROPIXEL_BLE_LINK_METHOD_READ, nullptr, 0U, response, sizeof(response),
                         response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    Header header{};
    if (response_size < sizeof(header)) {
        runtime::Panic("ble_link.read.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(&header, response, sizeof(header));
    if (header.size != sizeof(header) || header.length > kMaxMessage || response_size != sizeof(header) + header.length) {
        runtime::Panic("ble_link.read.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(data, response + sizeof(header), header.length);
    return BleLinkMessage{header.length, header.source == MICROPIXEL_BLE_LINK_SOURCE_VALUE, header.pending,
                          header.dropped};
}

Result<void> BleLink::Notify(BleLinkChannel channel, const void* data, uint32_t length) const {
    if (data == nullptr || length == 0U || length > kMaxMessage) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    int32_t status = OpenBleLinkService();
    if (status == MICROPIXEL_STATUS_OK) {
        alignas(4) static uint8_t request[sizeof(micropixel_ble_link_notify_request_t) + kMaxMessage];
        micropixel_ble_link_notify_request_t header{};
        header.size = sizeof(header);
        header.channel = static_cast<uint8_t>(channel);
        header.length = static_cast<uint16_t>(length);
        CopyBytes(request, &header, sizeof(header));
        CopyBytes(request + sizeof(header), data, length);
        status = CallVoid(ble_link_service, MICROPIXEL_BLE_LINK_METHOD_NOTIFY, request, sizeof(header) + length);
    }
    return VoidResult(status);
}

Result<void> BleLink::SetValue(const void* data, uint32_t length) const {
    if ((data == nullptr && length != 0U) || length > kMaxValue) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    int32_t status = OpenBleLinkService();
    if (status == MICROPIXEL_STATUS_OK) {
        alignas(4) uint8_t request[sizeof(micropixel_ble_link_value_request_t) + kMaxValue];
        micropixel_ble_link_value_request_t header{};
        header.size = sizeof(header);
        header.length = static_cast<uint16_t>(length);
        CopyBytes(request, &header, sizeof(header));
        if (length != 0U) {
            CopyBytes(request + sizeof(header), data, length);
        }
        status = CallVoid(ble_link_service, MICROPIXEL_BLE_LINK_METHOD_SET_VALUE, request, sizeof(header) + length);
    }
    return VoidResult(status);
}

Result<NetworkState> Network::State() const {
    int32_t status = OpenNetworkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_network_state_t response{};
    uint32_t response_size = 0U;
    status = CallService(network_service, MICROPIXEL_NETWORK_METHOD_GET_STATE, nullptr, 0U, &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response)) {
        runtime::Panic("network.state.response", MICROPIXEL_STATUS_INTERNAL);
    }
    NetworkState state{};
    state.available = (response.flags & MICROPIXEL_NETWORK_STATE_AVAILABLE) != 0U;
    state.station_connected = (response.flags & MICROPIXEL_NETWORK_STATE_STATION_CONNECTED) != 0U;
    state.station_connecting = (response.flags & MICROPIXEL_NETWORK_STATE_STATION_CONNECTING) != 0U;
    state.access_point = (response.flags & MICROPIXEL_NETWORK_STATE_ACCESS_POINT) != 0U;
    state.channel = response.channel;
    state.rssi = response.rssi;
    state.access_point_stations = response.access_point_stations;
    state.station_address = response.station_address;
    state.access_point_address = response.access_point_address;
    CopyBytes(state.mac, response.mac, sizeof(state.mac));
    CopyBytes(state.station_ssid, response.station_ssid, sizeof(state.station_ssid) - 1U);
    return state;
}

Result<void> Network::StartAccessPoint(const char* ssid, const char* password, uint8_t channel,
                                       uint8_t max_stations) const {
    micropixel_network_access_point_request_t request{};
    request.size = sizeof(request);
    request.channel = channel;
    request.max_stations = max_stations;
    if (!CopyText(request.ssid, sizeof(request.ssid), ssid) ||
        !CopyText(request.password, sizeof(request.password), password)) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    int32_t status = OpenNetworkService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(network_service, MICROPIXEL_NETWORK_METHOD_ACCESS_POINT_START, &request, sizeof(request));
    }
    return VoidResult(status);
}

Result<void> Network::StopAccessPoint() const {
    int32_t status = OpenNetworkService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(network_service, MICROPIXEL_NETWORK_METHOD_ACCESS_POINT_STOP, nullptr, 0U);
    }
    return VoidResult(status);
}

Result<uint32_t> Network::Advertise(const char* service, const char* instance, uint16_t port, const uint8_t* txt,
                                    uint32_t txt_length, bool udp) const {
    if (txt_length > kMaxTxt || (txt == nullptr && txt_length != 0U)) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    alignas(4) uint8_t request[sizeof(micropixel_network_advertise_request_t) + kMaxTxt];
    micropixel_network_advertise_request_t header{};
    header.size = sizeof(header);
    header.port = port;
    header.protocol = udp ? 1U : 0U;
    header.txt_length = static_cast<uint16_t>(txt_length);
    if (!CopyText(header.service, sizeof(header.service), service) ||
        !CopyText(header.instance, sizeof(header.instance), instance)) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    CopyBytes(request, &header, sizeof(header));
    if (txt_length != 0U) {
        CopyBytes(request + sizeof(header), txt, txt_length);
    }
    return NetworkHandleCall(MICROPIXEL_NETWORK_METHOD_ADVERTISE, request, sizeof(header) + txt_length);
}

Result<void> Network::Withdraw(uint32_t advert) const {
    return NetworkHandleVoid(MICROPIXEL_NETWORK_METHOD_WITHDRAW, advert);
}

Result<uint32_t> Network::Listen(uint16_t port) const {
    micropixel_network_open_request_t request{};
    request.size = sizeof(request);
    request.kind = MICROPIXEL_NETWORK_SOCKET_TCP_LISTEN;
    request.port = port;
    return NetworkHandleCall(MICROPIXEL_NETWORK_METHOD_OPEN, &request, sizeof(request));
}

Result<uint32_t> Network::OpenUdp(uint16_t port) const {
    micropixel_network_open_request_t request{};
    request.size = sizeof(request);
    request.kind = MICROPIXEL_NETWORK_SOCKET_UDP;
    request.port = port;
    return NetworkHandleCall(MICROPIXEL_NETWORK_METHOD_OPEN, &request, sizeof(request));
}

Result<NetworkAccepted> Network::Accept(uint32_t listener) const {
    int32_t status = OpenNetworkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_network_handle_message_t request{};
    request.size = sizeof(request);
    request.handle = listener;
    micropixel_network_accept_response_t response{};
    uint32_t response_size = 0U;
    status = CallService(network_service, MICROPIXEL_NETWORK_METHOD_ACCEPT, &request, sizeof(request), &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size != sizeof(response) || response.handle == 0U) {
        runtime::Panic("network.accept.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return NetworkAccepted{response.handle, response.remote_address, response.remote_port, response.local_address};
}

Result<NetworkReceived> Network::Receive(uint32_t socket, void* data, uint32_t capacity) const {
    if (data == nullptr || capacity == 0U || socket == 0U) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    if (capacity > kMaxTransfer) {
        capacity = kMaxTransfer;
    }
    int32_t status = OpenNetworkService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    using Header = micropixel_network_receive_response_t;
    micropixel_network_receive_request_t request{};
    request.size = sizeof(request);
    request.handle = socket;
    request.max_length = capacity;
    alignas(4) static uint8_t response[sizeof(Header) + kMaxTransfer];
    uint32_t response_size = 0U;
    status = CallService(network_service, MICROPIXEL_NETWORK_METHOD_RECEIVE, &request, sizeof(request), response,
                         sizeof(Header) + capacity, response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    Header header{};
    if (response_size < sizeof(header)) {
        runtime::Panic("network.receive.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(&header, response, sizeof(header));
    if (header.size != sizeof(header) || header.length > capacity || response_size != sizeof(header) + header.length) {
        runtime::Panic("network.receive.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(data, response + sizeof(header), header.length);
    return NetworkReceived{header.length, (header.flags & MICROPIXEL_NETWORK_TRANSFER_CLOSED) != 0U,
                           header.remote_address, header.remote_port};
}

Result<uint32_t> Network::Send(uint32_t socket, const void* data, uint32_t length) const {
    return NetworkSend(socket, data, length, 0U, 0U);
}

Result<uint32_t> Network::SendTo(uint32_t socket, const void* data, uint32_t length, uint32_t address,
                                 uint16_t port) const {
    return NetworkSend(socket, data, length, address, port);
}

Result<void> Network::Close(uint32_t socket) const { return NetworkHandleVoid(MICROPIXEL_NETWORK_METHOD_CLOSE, socket); }

Result<MicrophoneInfo> Microphone::Info() const {
    int32_t status = OpenMicrophoneService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_microphone_info_t response{};
    uint32_t response_size = 0U;
    status = CallService(microphone_service, MICROPIXEL_MICROPHONE_METHOD_GET_INFO, nullptr, 0U, &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response)) {
        runtime::Panic("microphone.info.response", MICROPIXEL_STATUS_INTERNAL);
    }
    return MicrophoneInfo{(response.flags & MICROPIXEL_MICROPHONE_FLAG_AVAILABLE) != 0U,
                          (response.flags & MICROPIXEL_MICROPHONE_FLAG_STARTED) != 0U, response.sample_rate,
                          response.frame_samples};
}

Result<void> Microphone::Start(uint8_t gain_db) const {
    int32_t status = OpenMicrophoneService();
    if (status == MICROPIXEL_STATUS_OK) {
        micropixel_microphone_start_request_t request{};
        request.size = sizeof(request);
        request.gain_db = gain_db;
        status = CallVoid(microphone_service, MICROPIXEL_MICROPHONE_METHOD_START, &request, sizeof(request));
    }
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<void> Microphone::Stop() const {
    int32_t status = OpenMicrophoneService();
    if (status == MICROPIXEL_STATUS_OK) {
        status = CallVoid(microphone_service, MICROPIXEL_MICROPHONE_METHOD_STOP, nullptr, 0U);
    }
    return status == MICROPIXEL_STATUS_OK ? Result<void>{} : Result<void>{unexpected(ErrorFromStatus(status))};
}

Result<uint32_t> Microphone::Read(int16_t* samples, uint32_t max_samples) const {
    if (samples == nullptr || max_samples == 0U || max_samples > kMaxReadSamples) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    int32_t status = OpenMicrophoneService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_microphone_read_request_t request{};
    request.size = sizeof(request);
    request.max_samples = max_samples;
    // Header plus samples in one response; not zero-initialised.
    alignas(4) uint8_t response[sizeof(micropixel_microphone_read_response_t) + kMaxReadSamples * sizeof(int16_t)];
    uint32_t response_size = 0U;
    status = CallService(microphone_service, MICROPIXEL_MICROPHONE_METHOD_READ, &request, sizeof(request), response,
                         sizeof(micropixel_microphone_read_response_t) + max_samples * sizeof(int16_t),
                         response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_microphone_read_response_t header{};
    if (response_size < sizeof(header)) {
        runtime::Panic("microphone.read.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(&header, response, sizeof(header));
    if (header.size != sizeof(header) || header.sample_count > max_samples ||
        response_size != sizeof(header) + header.sample_count * sizeof(int16_t)) {
        runtime::Panic("microphone.read.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(samples, response + sizeof(header), header.sample_count * sizeof(int16_t));
    return header.sample_count;
}

Result<PeerRadioState> PeerRadio::State() const {
    int32_t status = OpenPeerRadioService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    micropixel_peer_radio_state_t response{};
    uint32_t response_size = 0U;
    status = CallService(peer_radio_service, MICROPIXEL_PEER_RADIO_METHOD_GET_STATE, nullptr, 0U, &response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    if (response_size < sizeof(response) || response.size < sizeof(response)) {
        runtime::Panic("peer_radio.state.response", MICROPIXEL_STATUS_INTERNAL);
    }
    PeerRadioState state{};
    state.available = (response.flags & MICROPIXEL_PEER_RADIO_STATE_AVAILABLE) != 0U;
    state.started = (response.flags & MICROPIXEL_PEER_RADIO_STATE_STARTED) != 0U;
    state.paired = (response.flags & MICROPIXEL_PEER_RADIO_STATE_PAIRED) != 0U;
    state.peer_present = (response.flags & MICROPIXEL_PEER_RADIO_STATE_PEER_PRESENT) != 0U;
    state.pairing = (response.flags & MICROPIXEL_PEER_RADIO_STATE_PAIRING) != 0U;
    state.on_wifi_channel = (response.flags & MICROPIXEL_PEER_RADIO_STATE_WIFI_CHANNEL) != 0U;
    state.channel = response.channel;
    state.peer_rssi = response.peer_rssi;
    CopyBytes(state.self_address, response.self_address, sizeof(state.self_address));
    CopyBytes(state.peer_address, response.peer_address, sizeof(state.peer_address));
    state.packets_sent = response.packets_sent;
    state.packets_received = response.packets_received;
    state.send_failures = response.send_failures;
    return state;
}

Result<void> PeerRadio::Start() const { return PeerRadioVoid(MICROPIXEL_PEER_RADIO_METHOD_START, nullptr, 0U); }

Result<void> PeerRadio::Stop() const { return PeerRadioVoid(MICROPIXEL_PEER_RADIO_METHOD_STOP, nullptr, 0U); }

Result<void> PeerRadio::BeginPairing() const {
    return PeerRadioVoid(MICROPIXEL_PEER_RADIO_METHOD_PAIR_BEGIN, nullptr, 0U);
}

Result<void> PeerRadio::Accept(const uint8_t (&address)[6]) const {
    micropixel_peer_radio_accept_request_t request{};
    request.size = sizeof(request);
    CopyBytes(request.address, address, sizeof(request.address));
    return PeerRadioVoid(MICROPIXEL_PEER_RADIO_METHOD_PAIR_ACCEPT, &request, sizeof(request));
}

Result<void> PeerRadio::Forget() const { return PeerRadioVoid(MICROPIXEL_PEER_RADIO_METHOD_FORGET, nullptr, 0U); }

Result<void> PeerRadio::Send(const void* data, uint32_t length) const {
    if (data == nullptr || length == 0U || length > kMaxPayload) {
        return unexpected(Error{ErrorCode::kInvalidArgument});
    }
    alignas(4) uint8_t request[sizeof(micropixel_peer_radio_send_request_t) + kMaxPayload];
    micropixel_peer_radio_send_request_t header{};
    header.size = sizeof(header);
    header.length = static_cast<uint16_t>(length);
    CopyBytes(request, &header, sizeof(header));
    CopyBytes(request + sizeof(header), data, length);
    return PeerRadioVoid(MICROPIXEL_PEER_RADIO_METHOD_SEND, request, sizeof(header) + length);
}

Result<PeerPacket> PeerRadio::Receive(uint8_t (&data)[kMaxPayload]) const {
    int32_t status = OpenPeerRadioService();
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    using Header = micropixel_peer_radio_receive_response_t;
    alignas(4) uint8_t response[sizeof(Header) + kMaxPayload];
    uint32_t response_size = 0U;
    status = CallService(peer_radio_service, MICROPIXEL_PEER_RADIO_METHOD_RECEIVE, nullptr, 0U, response,
                         sizeof(response), response_size);
    if (status != MICROPIXEL_STATUS_OK) {
        return unexpected(ErrorFromStatus(status));
    }
    Header header{};
    if (response_size < sizeof(header)) {
        runtime::Panic("peer_radio.receive.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(&header, response, sizeof(header));
    if (header.size != sizeof(header) || header.length > kMaxPayload ||
        response_size != sizeof(header) + header.length) {
        runtime::Panic("peer_radio.receive.response", MICROPIXEL_STATUS_INTERNAL);
    }
    CopyBytes(data, response + sizeof(header), header.length);
    return PeerPacket{header.length, header.rssi, header.pending, header.dropped};
}

}  // namespace micropixel
