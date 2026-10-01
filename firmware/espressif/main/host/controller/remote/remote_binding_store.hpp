#pragma once

#include <array>
#include <cstdint>

#include "host/ui/system_ui.hpp"

namespace micropixel::firmware::remote_control {

// Whether the Gotchi is linked to an owner's account, as last reported by the
// control service. Kept in NVS so the Gotchi stays usable offline.
struct RemoteBinding final {
    std::array<char, host_ui::kRemoteControlOwnerNameCapacity> owner_name{};
    bool bound{};
};

class RemoteBindingStore final {
   public:
    // A missing or unreadable record loads as unbound.
    [[nodiscard]] bool Load(RemoteBinding& binding) const;
    [[nodiscard]] bool Save(const RemoteBinding& binding) const;
    [[nodiscard]] bool Clear() const;
};

}  // namespace micropixel::firmware::remote_control
