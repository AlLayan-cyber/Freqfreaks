// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Bands.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace ff
{

//==============================================================================
/** Layout of the shared-memory region all FreqFreaks instances meet in.

    Plain data plus lock-free atomics only, so it works across processes
    (DAWs that sandbox plugins). Each slot is written by exactly one instance and
    protected by a sequence lock; readers retry on a torn read. */
namespace bus
{
    constexpr std::uint32_t kMagic   = 0x46524551;   // "FREQ"
    constexpr std::uint32_t kVersion = 1;
    constexpr int kMaxSlots = 64;
    constexpr int kNameLength = 48;
    constexpr int kMaxReferences = 4;
    constexpr std::uint64_t kStaleAfterMs = 1500;    // no heartbeat for this long -> treat as gone
    constexpr std::uint64_t kReclaimAfterMs = 10000; // a crashed instance's slot may be reused

    enum class Kind : std::uint32_t { empty = 0, track = 1, hub = 2 };

    /** What a track publishes (copied in and out as a whole under the seqlock). */
    struct TrackData
    {
        Kind kind = Kind::empty;
        std::uint32_t colourArgb = 0;
        char name[kNameLength] {};
        std::uint64_t references[kMaxReferences] {};   // instance ids this track yields to
        float levelMidDb[kNumBands] {};
        float levelSideDb[kNumBands] {};
        float totalDb[kNumBands] {};
        float spaceDb[kNumBands] {};
        float eqDb[kNumBands] {};
        float restDb[kNumBands] {};
        float amount = 0.0f;
        float spaceAmount = 0.0f;
        std::uint32_t flags = 0;                        // see TrackFlags
    };

    enum TrackFlags : std::uint32_t
    {
        flagBypassed      = 1u << 0,
        flagReferenceLost = 1u << 1,
        flagLinkOff       = 1u << 2,
    };

    /** What the Hub publishes. */
    struct HubData
    {
        float totalAmount = 1.0f;     // multiplies every track's Amount
        float spaceAmount = 1.0f;     // multiplies every track's Space Amount
        std::uint32_t flags = 0;      // bit 0: compare A (all FreqFreaks processing bypassed)
    };

    struct alignas (64) Slot
    {
        std::atomic<std::uint64_t> owner;        // instance id of the writer, 0 = free
        std::atomic<std::uint64_t> heartbeatMs;
        std::atomic<std::uint32_t> sequence;
        TrackData data;
    };

    struct HubBlock
    {
        std::atomic<std::uint64_t> owner;
        std::atomic<std::uint64_t> heartbeatMs;
        std::atomic<std::uint32_t> sequence;
        HubData data;
    };

    struct Memory
    {
        std::atomic<std::uint32_t> magic;
        std::atomic<std::uint32_t> version;
        Slot slots[kMaxSlots];
        HubBlock hub;
    };

    static_assert (std::atomic<std::uint64_t>::is_always_lock_free, "need address-free 64-bit atomics");
    static_assert (std::atomic<std::uint32_t>::is_always_lock_free, "need address-free 32-bit atomics");
}

//==============================================================================
/** A live instance as seen by readers. */
struct BusEntry
{
    int slot = -1;
    std::uint64_t id = 0;
    bool stale = false;
    bus::TrackData data;

    std::string name() const;
};

/** Connection to the shared bus. One per plugin instance; cheap to create.

    Opens (or creates) a named shared-memory region. If that fails, falls back to
    a per-process region, so instances in the same process still see each other. */
class SharedBus
{
public:
    /** name: region name; tests pass a unique one. Empty = the default per-user bus. */
    explicit SharedBus (const std::string& regionName = {});
    ~SharedBus();

    SharedBus (const SharedBus&) = delete;
    SharedBus& operator= (const SharedBus&) = delete;

    bool isShared() const noexcept      { return shared; }

    /** Claims a slot for this id. If another live instance already uses the id
        (a duplicated track), returns false so the caller can pick a fresh id. */
    bool claim (std::uint64_t instanceId);
    void release();
    int slotIndex() const noexcept      { return mySlot; }
    std::uint64_t instanceId() const noexcept { return myId; }

    /** Real-time safe: copies data into this instance's slot and beats the heart. */
    void publish (const bus::TrackData& data) noexcept;

    /** Keeps this instance (track slot and Hub, if held) alive while the host
        is not calling the audio callback. */
    void heartbeat() noexcept;

    /** Real-time safe: latest data of the instance with the given id. */
    bool read (std::uint64_t id, bus::TrackData& out, bool* isStale = nullptr) const noexcept;

    /** Not real-time safe (allocates): every non-empty slot. */
    std::vector<BusEntry> list() const;

    // Hub side.
    bool claimHub (std::uint64_t instanceId);
    void releaseHub();
    void publishHub (const bus::HubData& data) noexcept;

    /** Latest Hub data; returns false (and defaults) if no live Hub. */
    bool readHub (bus::HubData& out) const noexcept;

    static std::uint64_t nowMs() noexcept;
    static std::uint64_t makeInstanceId();

private:
    bus::Memory* mem = nullptr;
    void* platformHandle = nullptr;
    std::size_t mappedSize = 0;
    bool shared = false;
    int mySlot = -1;
    std::uint64_t myId = 0, myHubId = 0;
};

} // namespace ff
