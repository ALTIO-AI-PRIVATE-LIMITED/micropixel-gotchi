#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "host/controller/remote/firmware_release_notes.hpp"
#include "host/controller/remote/remote_binding_policy.hpp"
#include "host/controller/remote/remote_control_defaults.hpp"
#include "host/controller/remote/remote_pairing_policy.hpp"
#include "host/controller/remote/remote_reconnect_policy.hpp"
#include "host/controller/remote/runtime_snapshot_policy.hpp"

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void TestRemoteControlDefaults() {
    using micropixel::firmware::remote_control::EnabledByDefault;

    Check(!EnabledByDefault(""), "Remote Control should default to disabled without a service host");
    Check(EnabledByDefault("control.local"), "Remote Control should default to enabled for a DNS host");
    Check(EnabledByDefault("192.0.2.1"), "Remote Control should default to enabled for an IP host");
}

using micropixel::firmware::remote_control::ReconnectBackoff;
using micropixel::firmware::remote_control::ShouldResetIdentityForControlStatus;

void TestRemoteControlReconnectPolicy() {
    assert(ShouldResetIdentityForControlStatus(401));
    assert(!ShouldResetIdentityForControlStatus(400));
    assert(!ShouldResetIdentityForControlStatus(403));
    assert(!ShouldResetIdentityForControlStatus(500));

    ReconnectBackoff backoff;
    constexpr uint32_t kCaps[] = {10000U, 20000U, 40000U, 80000U, 160000U, 300000U, 300000U};
    for (const uint32_t cap : kCaps) {
        const uint32_t minimum = cap / 2U;
        assert(backoff.NextDelayMs(0U) == minimum);
    }

    backoff.Reset();
    assert(backoff.NextDelayMs(UINT32_MAX) >= 5000U);
    assert(backoff.NextDelayMs(0U) == 10000U);
}

void TestPairingConsumedPolicy() {
    using micropixel::firmware::remote_control::MatchesPairingConsumed;
    constexpr auto kSession = "11111111-1111-4111-8111-111111111111";
    constexpr auto kPairing = "22222222-2222-4222-8222-222222222222";
    constexpr auto kOther = "33333333-3333-4333-8333-333333333333";
    Check(MatchesPairingConsumed(1, kSession, kPairing, kSession, kPairing), "current pairing is consumed");
    Check(!MatchesPairingConsumed(1, kSession, kPairing, kSession, kOther), "late event preserves new pairing");
    Check(!MatchesPairingConsumed(1, kOther, kPairing, kSession, kPairing), "stale session is ignored");
    Check(!MatchesPairingConsumed(2, kSession, kPairing, kSession, kPairing), "wrong protocol is ignored");
    Check(!MatchesPairingConsumed(1.5, kSession, kPairing, kSession, kPairing), "fractional version is ignored");
    Check(!MatchesPairingConsumed(1, "", kPairing, "", kPairing), "no active session cannot consume");
    Check(!MatchesPairingConsumed(1, kSession, "", kSession, ""), "duplicate after clearing is ignored");
    Check(!MatchesPairingConsumed(1, "", "", kSession, kPairing), "missing IDs cannot consume");
}

void TestBindingFramePolicy() {
    using micropixel::firmware::remote_control::MatchesSessionFrame;
    constexpr auto kSession = "11111111-1111-4111-8111-111111111111";
    constexpr auto kOther = "33333333-3333-4333-8333-333333333333";
    Check(MatchesSessionFrame(1, kSession, kSession), "binding for the open session applies");
    Check(!MatchesSessionFrame(1, kOther, kSession), "binding from a stale session is ignored");
    Check(!MatchesSessionFrame(2, kSession, kSession), "binding with another protocol is ignored");
    Check(!MatchesSessionFrame(1, "", ""), "binding without an open session is ignored");
}

void TestOwnerNameCopy() {
    using micropixel::firmware::remote_control::CopyOwnerName;
    std::array<char, 8U> name{};
    Check(CopyOwnerName("Abilash", name.data(), name.size()) == 7U && std::string(name.data()) == "Abilash",
          "an ASCII name that fits is copied");
    Check(CopyOwnerName("Abilash S", name.data(), name.size()) == 7U && std::string(name.data()) == "Abilash",
          "a long name is truncated to capacity");
    Check(CopyOwnerName("Ab\ncd\x7f", name.data(), name.size()) == 4U && std::string(name.data()) == "Abcd",
          "control characters are dropped");
    // "Zoë" (o-diaeresis is two bytes) then a three-byte character that does not fit.
    Check(CopyOwnerName("Zo\xc3\xab\xe4\xb8\x80\xe4\xb8\x80", name.data(), name.size()) == 7U,
          "truncation keeps whole characters");
    Check(CopyOwnerName("Bad\xc3", name.data(), name.size()) == 0U && name[0] == '\0',
          "malformed UTF-8 yields an empty name");
    Check(CopyOwnerName("\xed\xa0\x80", name.data(), name.size()) == 0U, "UTF-16 surrogates are rejected");
    Check(CopyOwnerName("\xc0\xaf", name.data(), name.size()) == 0U, "overlong encodings are rejected");
    Check(CopyOwnerName("", name.data(), name.size()) == 0U && name[0] == '\0', "an empty name stays empty");
}

void TestRuntimeSnapshotPolicy() {
    using micropixel::firmware::remote_control::RuntimeSnapshotPolicy;
    RuntimeSnapshotPolicy policy;
    constexpr int64_t kSecond = 1000000;
    constexpr int64_t kConnected = 7 * kSecond;
    Check(policy.ShouldPublish(kConnected, 0U), "new connection needs an initial snapshot");
    policy.RecordPublished(kConnected, 0U);
    Check(!policy.ShouldPublish(kConnected, 0U), "initial snapshot must not be sent twice");
    for (int64_t elapsed = 20 * kSecond; elapsed < 300 * kSecond; elapsed += 20 * kSecond) {
        Check(!policy.ShouldPublish(kConnected + elapsed, 0U), "heartbeat must not upload unchanged status");
    }
    Check(!policy.ShouldPublish(kConnected + 300 * kSecond - 1, 0U), "five-minute boundary is not early");
    Check(policy.ShouldPublish(kConnected + 300 * kSecond, 0U), "idle status refreshes after five minutes");
    Check(policy.ShouldPublish(kConnected + 320 * kSecond, 0U), "failed publication remains due");

    // A console read (or reconnect) bypasses the interval and starts a new one.
    Check(policy.ShouldPublish(kConnected + kSecond, 0U, true), "explicit reads and reconnects refresh immediately");
    policy.RecordPublished(kConnected + kSecond, 0U);
    Check(!policy.ShouldPublish(kConnected + 300 * kSecond, 0U), "explicit refresh postpones periodic duplicate");
    Check(policy.ShouldPublish(kConnected + 301 * kSecond, 0U), "periodic refresh follows last accepted snapshot");

    Check(policy.ShouldPublish(kConnected + 2 * kSecond, 1U), "lifecycle change refreshes immediately");
    policy.RecordPublished(kConnected + 2 * kSecond, 1U);
    Check(!policy.ShouldPublish(kConnected + 3 * kSecond, 1U), "published lifecycle change is not repeated");
    Check(policy.ShouldPublish(kConnected + 3 * kSecond, 2U), "change during publication is not lost");
}

}  // namespace

void TestFirmwareReleaseNotes() {
    using micropixel::firmware::remote_control::FirmwareReleaseNotesBuilder;
    std::array<char, 16> text{};
    FirmwareReleaseNotesBuilder notes(text);
    notes.Append("");
    notes.Append("Fix one");
    notes.Append("Fix two");
    assert(std::string(text.data()) == "Fix one\nFix two");
    notes.Append("extra");
    assert(std::string(text.data()) == "Fix one\nFix ...");
    notes.Append("ignored");
    assert(std::string(text.data()) == "Fix one\nFix ...");
    FirmwareReleaseNotesBuilder utf8(text);
    utf8.Append("修复显示错误与问题");
    assert(std::string(text.data()) == "修复显示...");
    FirmwareReleaseNotesBuilder reset(text);
    assert(text[0] == '\0');
    reset.Append("012345678901234");
    assert(std::string(text.data()) == "012345678901234");
    std::array<char, 1> tiny{'x'};
    FirmwareReleaseNotesBuilder small(tiny);
    small.Append("修复");
    assert(tiny[0] == '\0');
    FirmwareReleaseNotesBuilder empty({});
    empty.Append("ignored");
}

int main() {
    TestFirmwareReleaseNotes();
    TestRemoteControlDefaults();
    TestRemoteControlReconnectPolicy();
    TestPairingConsumedPolicy();
    TestBindingFramePolicy();
    TestOwnerNameCopy();
    TestRuntimeSnapshotPolicy();
    return 0;
}
