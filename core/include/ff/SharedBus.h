// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ff/Bands.h"
#include "ff/BellBank.h"
#include "ff/StereoProcessor.h"

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
    constexpr std::uint32_t kVersion = 5;   // bump on any layout change
    constexpr int kMaxSlots = 64;
    constexpr int kNameLength = 48;
    constexpr int kMaxReferences = 4;
    constexpr int kAudioFrames = 8192;               // per-track audio ring (stereo), power of two
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
        float eqBells[kNumBells] {};                    // exact filter state, for drawing the wave
        float spaceBells[kNumBells] {};
        float amount = 0.0f;
        float spaceAmount = 0.0f;
        float compensationDb = 0.0f;                    // automatic output gain right now
        std::uint32_t spaceMode = 0;                    // 0 Spread, 1 Pan Away, 2 Decorrelate
        float hubTrim = 1.0f;                           // the Hub's trim this track is applying
        std::uint32_t flags = 0;                        // see TrackFlags
        std::uint32_t sharpStep = 0;                    // bell layout of eqBells/spaceBells (Sharp step)
        float peakHz[kNumPeaks] {};                     // narrow Sharp mode: the moving bells
        float peakDb[kNumPeaks] {};
        float peakQ[kNumPeaks] {};
        float fineMidDb[kFineBands] {};                 // fine spectrum, for tracks that yield to this one
        std::uint32_t listen = 0;                       // monitoring (ff::Listen), 0 = normal
    };

    enum TrackFlags : std::uint32_t
    {
        flagBypassed      = 1u << 0,
        flagReferenceLost = 1u << 1,
        flagLinkOff       = 1u << 2,
        flagHubBypassed   = 1u << 3,   // bypassed by the Hub (all or this track)
    };

    /** The Hub's setting for one track, found by the track's instance id. */
    struct HubTrim
    {
        std::uint64_t id = 0;
        float trim = 1.0f;            // multiplies that track's Amount (0..1.5)
        std::uint32_t flags = 0;      // bit 0: bypass this track
    };

    enum HubFlags : std::uint32_t
    {
        hubCompareA = 1u << 0,        // compare A: all processing off (listening)
        hubBypassAll = 1u << 1,       // global bypass
    };

    /** What the Hub publishes. */
    struct HubData
    {
        float totalAmount = 1.0f;     // multiplies every track's Amount
        float spaceAmount = 1.0f;     // multiplies every track's Space Amount
        std::uint32_t flags = 0;      // see HubFlags
        HubTrim trims[kMaxSlots] {};

        /** This track's trim, or a neutral one. */
        HubTrim trimFor (std::uint64_t trackId) const noexcept
        {
            for (const auto& t : trims)
                if (t.id == trackId && trackId != 0)
                    return t;
            return {};
        }
    };

    /** A request from the Hub to change a track's own parameters (the remote panel). */
    struct RemoteCommand
    {
        std::uint32_t serial = 0;     // changes with every new command
        std::uint32_t mask = 0;       // see RemoteFields
        float amount = 0.0f;          // 0..1
        float spaceAmount = 0.0f;     // 0..2
        std::uint32_t link = 1;
        std::uint32_t spaceMode = 0;
        std::uint64_t referenceId = 0;
    };

    enum RemoteFields : std::uint32_t
    {
        remoteAmount = 1u << 0,
        remoteSpace = 1u << 1,
        remoteLink = 1u << 2,
        remoteSpaceMode = 1u << 3,
        remoteReference = 1u << 4,
    };

    struct alignas (64) Slot
    {
        std::atomic<std::uint64_t> owner;        // instance id of the writer, 0 = free
        std::atomic<std::uint64_t> heartbeatMs;
        std::atomic<std::uint32_t> sequence;
        TrackData data;
        std::atomic<std::uint32_t> remoteSequence;   // written by the Hub, read by the track
        RemoteCommand remote;

        // The track's own output, so another track can let you listen to its reference.
        std::atomic<std::uint64_t> audioWritten;    // frames written so far
        float audio[kAudioFrames * 2];               // interleaved L R
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

    /** False if this instance's slot was taken over (it stalled long enough to look crashed):
        call claim() again. */
    bool ownsSlot() const noexcept;
    std::uint64_t instanceId() const noexcept { return myId; }

    /** Real-time safe: copies data into this instance's slot and beats the heart. */
    void publish (const bus::TrackData& data) noexcept;

    /** Keeps this instance (track slot and Hub, if held) alive while the host
        is not calling the audio callback. */
    void heartbeat() noexcept;

    /** Real-time safe: latest data of the instance with the given id. */
    bool read (std::uint64_t id, bus::TrackData& out, bool* isStale = nullptr) const noexcept;

    /** Real-time safe: appends this track's output to its audio ring. */
    void publishAudio (const float* left, const float* right, int numFrames) noexcept;

    enum class AudioRead { none, ok, resynced };

    /** Real-time safe: the next numFrames of another track's audio. readPos is the caller's
        position (start with 0). The reader keeps a small cushion behind the writer, so the
        order in which the DAW runs the two tracks does not matter; when the cushion breaks
        (or at the start) it jumps back into place and returns resynced (fade in). none = no
        live track with that id: the output is silence. */
    AudioRead readAudio (std::uint64_t id, std::uint64_t& readPos, float* left, float* right, int numFrames) const noexcept;

    /** Hub side: asks the track with this id to change its own parameters. */
    bool sendRemote (std::uint64_t trackId, const bus::RemoteCommand& command) noexcept;

    /** Track side: the latest remote command for this instance, if its serial is newer than lastSerial. */
    bool readRemote (bus::RemoteCommand& out, std::uint32_t lastSerial) const noexcept;

    /** Not real-time safe (allocates): every non-empty slot. */
    std::vector<BusEntry> list() const;

    // Hub side.
    bool claimHub (std::uint64_t instanceId);
    /** Call regularly: true while this instance holds the Hub; takes it back if it was lost. */
    bool ensureHub (std::uint64_t instanceId);
    void releaseHub();
    void publishHub (const bus::HubData& data) noexcept;

    /** Latest Hub data; returns false (and defaults) if no live Hub. busy = there is a live Hub,
        but it was being written at that moment (keep the last copy). */
    bool readHub (bus::HubData& out, bool* busy = nullptr) const noexcept;

    static std::uint64_t nowMs() noexcept;
    static std::uint64_t makeInstanceId();

private:
    bus::Memory* mem = nullptr;
    [[maybe_unused]] void* platformHandle = nullptr;   // Windows file-mapping handle
    std::size_t mappedSize = 0;
    bool shared = false;
    int mySlot = -1;
    std::uint64_t myId = 0, myHubId = 0;
};

} // namespace ff
