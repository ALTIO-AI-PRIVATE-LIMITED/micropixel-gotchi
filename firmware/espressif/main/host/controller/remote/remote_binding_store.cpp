#include "host/controller/remote/remote_binding_store.hpp"

#include <algorithm>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

namespace micropixel::firmware::remote_control {
namespace {

constexpr char kTag[] = "remote_binding";
constexpr char kPartition[] = "sys_store";
constexpr char kNamespace[] = "control";
constexpr char kBindingKey[] = "binding";
constexpr uint8_t kBindingVersion = 1U;

struct Record final {
    uint8_t version{};
    uint8_t bound{};
    uint8_t reserved[2]{};
    char owner_name[host_ui::kRemoteControlOwnerNameCapacity]{};
};
static_assert(sizeof(Record) == 69U, "Remote Control binding record layout changed");

}  // namespace

bool RemoteBindingStore::Load(RemoteBinding& binding) const {
    binding = {};
    nvs_handle_t handle{};
    if (nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    Record record{};
    size_t size = sizeof(record);
    const esp_err_t error = nvs_get_blob(handle, kBindingKey, &record, &size);
    nvs_close(handle);
    const bool valid =
        error == ESP_OK && size == sizeof(record) && record.version == kBindingVersion && record.bound <= 1U &&
        ::strnlen(record.owner_name, sizeof(record.owner_name)) < sizeof(record.owner_name) &&
        std::all_of(std::begin(record.reserved), std::end(record.reserved), [](uint8_t byte) { return byte == 0U; });
    if (!valid) {
        return false;
    }
    binding.bound = record.bound != 0U;
    std::memcpy(binding.owner_name.data(), record.owner_name, sizeof(record.owner_name));
    return true;
}

bool RemoteBindingStore::Save(const RemoteBinding& binding) const {
    Record record{};
    record.version = kBindingVersion;
    record.bound = binding.bound ? 1U : 0U;
    const size_t length = ::strnlen(binding.owner_name.data(), sizeof(record.owner_name) - 1U);
    std::memcpy(record.owner_name, binding.owner_name.data(), length);
    nvs_handle_t handle{};
    esp_err_t error = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, kBindingKey, &record, sizeof(record));
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (handle != 0U) {
        nvs_close(handle);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "failed to save the account link: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

bool RemoteBindingStore::Clear() const {
    nvs_handle_t handle{};
    esp_err_t error = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_erase_key(handle, kBindingKey);
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            error = ESP_OK;
        }
    }
    if (error == ESP_OK) {
        error = nvs_commit(handle);
    }
    if (handle != 0U) {
        nvs_close(handle);
    }
    if (error != ESP_OK) {
        ESP_LOGE(kTag, "failed to clear the account link: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

}  // namespace micropixel::firmware::remote_control
