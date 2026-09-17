// SPDX-License-Identifier: MIT
#include "boot_sequence.h"
#include "sof_test_image.h"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace phaser360::sof;
using sof_test_image::U32;
namespace {
size_t checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
std::vector<uint8_t> Ready() {
    std::vector<uint8_t> b(kIpc3ReadyBytes, 0);
    U32(b, 0, 108); U32(b, 4, 0x70000000);
    U32(b, 8, 0x1000); U32(b, 12, 0x2000);
    U32(b, 16, 0x1000); U32(b, 20, 0x1000);
    U32(b, 24, 60); U32(b, 28, 0x00090001); U32(b, 32, 0x00010000);
    U32(b, 64, 0x03013000); U32(b, 68, 0x12345678); U32(b, 84, 16);
    return b;
}
enum class Fault { None, Verify, Prepare, Tag, RomInit, RomState, RomHalted,
    Start, Entry, EntryState, EntryHalted, ReadyWait, ReadyHeader, ReadyLength };
struct Mock {
    Fault fault = Fault::None;
    bool stop_fails = false, release_fails = false, shutdown_fails = false;
    std::vector<std::string> events;
    std::vector<uint8_t> expected;
};
Mock& M(void* c) { return *static_cast<Mock*>(c); }
bool Verify(void* c, const uint8_t* data, size_t len) {
    auto& m = M(c); m.events.push_back("verify");
    Check(len == m.expected.size() && std::equal(data, data + len, m.expected.begin()), "verify exact whole image");
    return m.fault != Fault::Verify; // mock approval, not authentication!
}
bool Prepare(void* c, const uint8_t* data, size_t len, uint8_t* tag) {
    auto& m = M(c); m.events.push_back("prepare");
    Check(len == 0xa000 && std::equal(data, data + len, m.expected.begin() + 24), "XMan excluded from DMA payload");
    *tag = m.fault == Fault::Tag ? 0 : 3;
    return m.fault != Fault::Prepare;
}
bool Init(void* c, uint32_t command, uint32_t timeout, uint32_t* status) {
    auto& m = M(c); m.events.push_back("init");
    Check(command == 0x81004400 && timeout == 150, "ROM tag-minus-one and timeout");
    *status = m.fault == Fault::RomState ? 0 : (m.fault == Fault::RomHalted ? 0x80000001u : 1u);
    return m.fault != Fault::RomInit;
}
bool Start(void* c) { auto& m = M(c); m.events.push_back("start"); return m.fault != Fault::Start; }
bool Entry(void* c, uint32_t timeout, uint32_t* status) {
    auto& m = M(c); m.events.push_back("entry");
    Check(timeout == 3000, "firmware timeout forwarded");
    *status = m.fault == Fault::EntryState ? 4 : (m.fault == Fault::EntryHalted ? 0x80000005u : 0x20000005u);
    return m.fault != Fault::Entry;
}
bool Stop(void* c) { auto& m = M(c); m.events.push_back("stop"); return !m.stop_fails; }
bool WaitReady(void* c, uint32_t timeout, uint8_t* out, size_t capacity, size_t* received) {
    auto& m = M(c); m.events.push_back("ready");
    Check(timeout == 5000 && capacity == 108, "bounded ready buffer/deadline");
    auto b = Ready();
    if (m.fault == Fault::ReadyHeader) U32(b, 4, 0x10000000);
    std::copy(b.begin(), b.end(), out);
    *received = m.fault == Fault::ReadyLength ? 109 : 108;
    return m.fault != Fault::ReadyWait;
}
bool Release(void* c) { auto& m = M(c); m.events.push_back("release"); return !m.release_fails; }
bool Shutdown(void* c) { auto& m = M(c); m.events.push_back("shutdown"); return !m.shutdown_fails; }
BootBackend Backend(Mock& m) { return {&m, Verify, Prepare, Init, Start, Entry, Stop, WaitReady, Release, Shutdown}; }
constexpr BootPolicy policy{150, 3000, 5000, 19}; // test policy only
bool Has(const Mock& m, const char* e) { return std::find(m.events.begin(), m.events.end(), e) != m.events.end(); }
size_t Position(const Mock& m, const char* e) {
    return static_cast<size_t>(std::find(m.events.begin(), m.events.end(), e) - m.events.begin());
}
void ParserTests() {
    auto b = Ready(); ReadyInfo out{};
    Check(ParseIpc3Ready(b.data(), b.size(), 19, &out) == ReadyStatus::Ok, "valid fixed header");
    Check(out.major == 1 && out.minor == 9 && out.micro == 0 && out.build == 1 &&
          out.abi == 0x03013000 && out.source_hash == 0x12345678 && out.flags == 16 &&
          out.dspbox_offset == 0x1000 && out.hostbox_offset == 0x2000, "wire offsets");
    for (size_t size = 0; size < b.size(); ++size) {
        out.major = 99;
        Check(ParseIpc3Ready(b.data(), size, 19, &out) == ReadyStatus::Size && !out.major && !out.flags, "truncation rejects and clears");
    }
    Check(ParseIpc3Ready(b.data(), 109, 19, &out) == ReadyStatus::Size, "oversize");
    Check(ParseIpc3Ready(nullptr, 108, 19, &out) == ReadyStatus::InvalidArgument, "null input");
    Check(ParseIpc3Ready(b.data(), 108, 19, nullptr) == ReadyStatus::InvalidArgument, "null output");
    Check(ParseIpc3Ready(b.data(), 108, 4096, &out) == ReadyStatus::InvalidArgument, "ABI limit");
    struct Mutation { size_t offset; uint32_t value; ReadyStatus error; };
    for (const auto& m : {Mutation{0, 107, ReadyStatus::Size}, {4, 0x10000000, ReadyStatus::Command},
         {24, 59, ReadyStatus::VersionSize}, {64, 0x04000000, ReadyStatus::Abi},
         {64, 0x03014000, ReadyStatus::Abi}, {72, 1, ReadyStatus::Reserved}, {104, 1, ReadyStatus::Reserved}}) {
        b = Ready(); U32(b, m.offset, m.value); out.abi = 99;
        Check(ParseIpc3Ready(b.data(), b.size(), 19, &out) == m.error && !out.abi, "invalid fields cleared");
    }
    b = Ready(); U32(b, 8, 0xffffffff); U32(b, 88, 0x12345678);
    Check(ParseIpc3Ready(b.data(), b.size(), 19, &out) == ReadyStatus::Ok &&
          out.dspbox_offset == 0xffffffff && out.flags == 0x1234567800000010ull,
          "mailbox offsets remain untrusted data; never followed");
    b.insert(b.begin(), 0);
    Check(ParseIpc3Ready(b.data() + 1, 108, 19, &out) == ReadyStatus::Ok, "unaligned input");
    uint32_t seed = 0x12345678;
    for (size_t i = 0; i < 10000; ++i) {
        b = Ready(); seed = seed * 1664525u + 1013904223u;
        b[seed % b.size()] ^= static_cast<uint8_t>(seed >> 24);
        const auto status = ParseIpc3Ready(b.data(), b.size(), 19, &out);
        Check(status == ReadyStatus::Ok || (!out.major && !out.abi && !out.flags), "mutation output contract");
    }
}
}
int main() {
    ParserTests();
    uint32_t command = 99;
    Check(!BuildGlkRomControl(0, &command) && command == 0, "tag zero");
    Check(!BuildGlkRomControl(16, &command) && !command, "tag overflow");
    Check(!BuildGlkRomControl(1, nullptr), "null command");
    for (uint8_t tag = 1; tag <= 15; ++tag)
        Check(BuildGlkRomControl(tag, &command) && command == (0x81004000u | uint32_t(tag - 1) << 9), "tag encoding");
    const auto image = sof_test_image::Fixture();
    struct Scenario { Fault fault; BootStatus result; };
    const Scenario scenarios[] = {{Fault::None, BootStatus::ReadyHeaderValidated},
        {Fault::Verify, BootStatus::ImageRejected}, {Fault::Prepare, BootStatus::PrepareFailed},
        {Fault::Tag, BootStatus::InvalidStream}, {Fault::RomInit, BootStatus::RomInitFailed},
        {Fault::RomState, BootStatus::RomStateInvalid}, {Fault::RomHalted, BootStatus::RomStateInvalid},
        {Fault::Start, BootStatus::StartFailed}, {Fault::Entry, BootStatus::FirmwareEntryFailed},
        {Fault::EntryState, BootStatus::FirmwareStateInvalid}, {Fault::EntryHalted, BootStatus::FirmwareStateInvalid},
        {Fault::ReadyWait, BootStatus::ReadyWaitFailed}, {Fault::ReadyHeader, BootStatus::ReadyInvalid},
        {Fault::ReadyLength, BootStatus::ReadyInvalid}};
    for (const auto& s : scenarios) {
        Mock m; m.fault = s.fault; m.expected = image;
        const auto r = RunBootSequence(image.data(), image.size(), policy, Backend(m));
        Check(r.status == s.result && r.cleanup_confirmed && !r.resources_retained, "result with cleanup");
        if (s.fault == Fault::Verify) { Check(m.events.size() == 1, "no mutation before approval"); continue; }
        Check(Position(m, "stop") < Position(m, "release"), "quiescence precedes release");
        if (s.fault == Fault::None)
            Check(m.events == std::vector<std::string>({"verify","prepare","init","start","entry","stop","ready","release"}), "success ordering");
        else if (Has(m, "init"))
            Check(Position(m, "shutdown") < Position(m, "release"), "shutdown on failure");
    }
    for (auto fault : {Fault::None, Fault::Prepare, Fault::Start, Fault::Entry}) {
        Mock m; m.expected = image; m.fault = fault; m.stop_fails = true;
        const auto r = RunBootSequence(image.data(), image.size(), policy, Backend(m));
        Check(!r.cleanup_confirmed && r.resources_retained && !Has(m, "release") && !Has(m, "ready") && !Has(m, "shutdown"), "failed stop retains memory");
        Check(std::count(m.events.begin(), m.events.end(), "stop") == 1, "no repeated stop retry");
        Check(r.status == (fault == Fault::None ? BootStatus::StopFailed :
            fault == Fault::Prepare ? BootStatus::PrepareFailed :
            fault == Fault::Start ? BootStatus::StartFailed : BootStatus::FirmwareEntryFailed), "primary failure retained");
    }
    for (bool primary_failure : {false, true}) {
        Mock m; m.expected = image; m.release_fails = true; m.shutdown_fails = true;
        if (primary_failure) m.fault = Fault::Entry;
        const auto r = RunBootSequence(image.data(), image.size(), policy, Backend(m));
        Check(r.release_failed && r.shutdown_failed && !r.cleanup_confirmed && r.resources_retained, "cleanup failure explicit");
        Check(r.status == (primary_failure ? BootStatus::FirmwareEntryFailed : BootStatus::ReleaseFailed), "error precedence");
    }
    Mock m; m.expected = image; auto backend = Backend(m);
    auto r = RunBootSequence(nullptr, image.size(), policy, backend);
    Check(r.status == BootStatus::InvalidArgument && m.events.empty(), "null image no callbacks");
    r = RunBootSequence(image.data(), 20, policy, backend);
    Check(r.status == BootStatus::InvalidImage && m.events.empty(), "malformed image no callbacks");
    backend.start = nullptr;
    r = RunBootSequence(image.data(), image.size(), policy, backend);
    Check(r.status == BootStatus::InvalidArgument && m.events.empty(), "missing backend rejected");
    auto bad_policy = policy; bad_policy.ready_timeout_ms = 0;
    r = RunBootSequence(image.data(), image.size(), bad_policy, Backend(m));
    Check(r.status == BootStatus::InvalidArgument && m.events.empty(), "unbounded wait rejected");
    std::cout << "SOF_BOOT_TESTS=PASS checks=" << checks << " scenarios=20 hardware_access=NONE\n";
}
