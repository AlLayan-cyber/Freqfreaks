// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/SharedBus.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <random>
#include <thread>

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace ff
{

namespace
{
    bus::Memory& processLocalMemory()
    {
        // Static storage is zero-initialised, which is a valid empty bus.
        static bus::Memory memory;
        return memory;
    }

    /** Sequence-lock write. An odd sequence means "being written" and doubles as the writer
        lock: only the writer that turns it odd (compare-exchange) may copy. A second writer
        (the Hub publishes from the audio thread and from its timer) skips instead of racing:
        two writers storing seq+1 / seq+2 at once could leave the sequence odd for good, and
        every reader would then fail forever. Returns false when skipped. */
    template <typename Data>
    bool seqTryWrite (std::atomic<std::uint32_t>& sequence, Data& target, const Data& source) noexcept
    {
        auto seq = sequence.load (std::memory_order_relaxed);
        if ((seq & 1u) != 0 || ! sequence.compare_exchange_strong (seq, seq + 1, std::memory_order_acquire, std::memory_order_relaxed))
            return false;
        std::atomic_thread_fence (std::memory_order_release);
        std::memcpy (static_cast<void*> (&target), &source, sizeof (Data));
        std::atomic_thread_fence (std::memory_order_release);
        sequence.store (seq + 2, std::memory_order_release);
        return true;
    }

    /** For the message thread: a write that must not be dropped (claims, remote commands). */
    template <typename Data>
    void seqWrite (std::atomic<std::uint32_t>& sequence, Data& target, const Data& source) noexcept
    {
        for (int attempt = 0; attempt < 2000; ++attempt)
        {
            if (seqTryWrite (sequence, target, source))
                return;
            std::this_thread::yield();
        }
        // Still odd after that long: the writer died mid-write (crashed process). Recover.
        auto seq = sequence.load (std::memory_order_relaxed);
        sequence.store ((seq | 1u) + 1u, std::memory_order_release);
        seqTryWrite (sequence, target, source);
    }

    /** A new owner takes a slot over: a writer that died mid-write may have left it odd. */
    void unlockAbandoned (std::atomic<std::uint32_t>& sequence) noexcept
    {
        const auto seq = sequence.load (std::memory_order_relaxed);
        if ((seq & 1u) != 0)
            sequence.store (seq + 1u, std::memory_order_release);
    }

    template <typename Data>
    bool seqRead (const std::atomic<std::uint32_t>& sequence, const Data& source, Data& target) noexcept
    {
        for (int attempt = 0; attempt < 64; ++attempt)
        {
            const auto before = sequence.load (std::memory_order_acquire);
            if (before & 1u)
                continue;
            std::memcpy (static_cast<void*> (&target), &source, sizeof (Data));
            std::atomic_thread_fence (std::memory_order_acquire);
            if (sequence.load (std::memory_order_relaxed) == before)
                return true;
        }
        return false;
    }

    std::string defaultRegionName()
    {
       #if defined (_WIN32)
        return "Local\\FreqFreaksBus" + std::to_string (bus::kVersion);
       #else
        // macOS limits POSIX shm names to 31 characters.
        return "/ffbus" + std::to_string (bus::kVersion) + "-" + std::to_string ((unsigned) getuid());
       #endif
    }
}

std::string BusEntry::name() const
{
    return std::string (data.name, strnlen (data.name, sizeof (data.name)));
}

//==============================================================================
SharedBus::SharedBus (const std::string& regionName)
{
    const std::string name = regionName.empty() ? defaultRegionName() : regionName;
    const std::size_t size = sizeof (bus::Memory);
    void* address = nullptr;

   #if defined (_WIN32)
    std::wstring wide (name.begin(), name.end());
    HANDLE mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         0, (DWORD) size, wide.c_str());
    if (mapping != nullptr)
    {
        address = MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (address != nullptr)
            platformHandle = mapping;
        else
            CloseHandle (mapping);
    }
   #else
    const std::string posixName = name.front() == '/' ? name : "/" + name;
    const int fd = shm_open (posixName.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd >= 0)
    {
        struct stat info {};
        bool sized = fstat (fd, &info) == 0
                     && ((std::size_t) info.st_size >= size || ftruncate (fd, (off_t) size) == 0);
        if (sized)
        {
            void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (p != MAP_FAILED)
                address = p;
        }
        close (fd);
    }
   #endif

    if (address != nullptr)
    {
        auto* m = static_cast<bus::Memory*> (address);
        std::uint32_t expected = 0;
        if (m->magic.compare_exchange_strong (expected, bus::kMagic))
            m->version.store (bus::kVersion);

        if (m->magic.load() == bus::kMagic && m->version.load() == bus::kVersion)
        {
            mem = m;
            mappedSize = size;
            shared = true;
            return;
        }

        // Incompatible bus from another FreqFreaks version: stay out of its way.
       #if defined (_WIN32)
        UnmapViewOfFile (address);
        CloseHandle (static_cast<HANDLE> (platformHandle));
        platformHandle = nullptr;
       #else
        munmap (address, size);
       #endif
    }

    mem = &processLocalMemory();
    shared = false;
}

SharedBus::~SharedBus()
{
    release();
    releaseHub();

    if (shared && mem != nullptr)
    {
       #if defined (_WIN32)
        UnmapViewOfFile (mem);
        if (platformHandle != nullptr)
            CloseHandle (static_cast<HANDLE> (platformHandle));
       #else
        munmap (mem, mappedSize);
       #endif
    }
}

std::uint64_t SharedBus::nowMs() noexcept
{
    using namespace std::chrono;
    return (std::uint64_t) duration_cast<milliseconds> (steady_clock::now().time_since_epoch()).count();
}

std::uint64_t SharedBus::makeInstanceId()
{
    std::random_device rd;
    std::mt19937_64 gen (((std::uint64_t) rd() << 32) ^ rd() ^ (std::uint64_t) nowMs());
    std::uint64_t id = 0;
    while (id == 0)
        id = gen();
    return id;
}

//==============================================================================
bool SharedBus::claim (std::uint64_t instanceId)
{
    release();
    if (instanceId == 0)
        return false;

    const auto now = nowMs();

    // Refuse an id that another live instance is using (a duplicated track).
    for (int i = 0; i < bus::kMaxSlots; ++i)
    {
        auto& s = mem->slots[i];
        if (s.owner.load() == instanceId && now - s.heartbeatMs.load() < bus::kStaleAfterMs)
            return false;
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < bus::kMaxSlots; ++i)
        {
            auto& s = mem->slots[i];
            auto current = s.owner.load();
            const bool free = current == 0;
            const bool sameIdLeftover = current == instanceId;
            const bool abandoned = pass == 1 && current != 0
                                   && now - s.heartbeatMs.load() > bus::kReclaimAfterMs;

            if ((free || sameIdLeftover || abandoned) && s.owner.compare_exchange_strong (current, instanceId))
            {
                s.heartbeatMs.store (now);
                if (abandoned)
                {
                    unlockAbandoned (s.sequence);
                    unlockAbandoned (s.remoteSequence);
                }
                mySlot = i;
                myId = instanceId;
                bus::TrackData empty;
                empty.kind = bus::Kind::track;
                seqWrite (s.sequence, s.data, empty);

                s.audioWritten.store (0, std::memory_order_release);

                // Drop any command left for a previous owner of this slot.
                bus::RemoteCommand previous, cleared;
                seqRead (s.remoteSequence, s.remote, previous);
                cleared.serial = previous.serial;
                seqWrite (s.remoteSequence, s.remote, cleared);
                return true;
            }
        }
    }
    return false;
}

void SharedBus::release()
{
    if (mySlot < 0)
        return;

    auto& s = mem->slots[mySlot];
    if (s.owner.load() == myId)   // never wipe a slot someone else has taken over
    {
        bus::TrackData empty;
        seqWrite (s.sequence, s.data, empty);
        auto expected = myId;
        s.owner.compare_exchange_strong (expected, 0);
    }
    mySlot = -1;
    myId = 0;
}

void SharedBus::publish (const bus::TrackData& data) noexcept
{
    if (mySlot < 0)
        return;
    auto& s = mem->slots[mySlot];
    if (s.owner.load (std::memory_order_relaxed) != myId)
        return;   // the slot was taken over; ownsSlot() tells the owner to claim again
    seqTryWrite (s.sequence, s.data, data);
    s.heartbeatMs.store (nowMs(), std::memory_order_relaxed);
}

void SharedBus::publishAudio (const float* left, const float* right, int numFrames) noexcept
{
    if (mySlot < 0 || numFrames <= 0)
        return;
    auto& s = mem->slots[mySlot];
    if (s.owner.load (std::memory_order_relaxed) != myId)
        return;
    constexpr std::uint64_t mask = bus::kAudioFrames - 1;
    const auto written = s.audioWritten.load (std::memory_order_relaxed);
    for (int i = 0; i < numFrames; ++i)
    {
        const auto at = (std::size_t) ((written + (std::uint64_t) i) & mask) * 2;
        s.audio[at] = left[i];
        s.audio[at + 1] = right[i];
    }
    s.audioWritten.store (written + (std::uint64_t) numFrames, std::memory_order_release);
}

SharedBus::AudioRead SharedBus::readAudio (std::uint64_t id, std::uint64_t& readPos, float* left, float* right,
                                           int numFrames) const noexcept
{
    std::fill (left, left + numFrames, 0.0f);
    std::fill (right, right + numFrames, 0.0f);
    if (id == 0 || numFrames <= 0 || numFrames > bus::kAudioFrames / 4)
        return AudioRead::none;

    const auto now = nowMs();
    for (int i = 0; i < bus::kMaxSlots; ++i)
    {
        const auto& s = mem->slots[i];
        if (s.owner.load (std::memory_order_acquire) != id)
            continue;
        if (now - s.heartbeatMs.load (std::memory_order_relaxed) > bus::kStaleAfterMs)
            return AudioRead::none;

        const auto written = s.audioWritten.load (std::memory_order_acquire);
        const auto cushion = (std::uint64_t) std::max (2 * numFrames, 512);
        auto result = AudioRead::ok;
        if (written < cushion)
            return AudioRead::none;
        if (readPos == 0 || readPos + (std::uint64_t) numFrames > written
            || written - readPos > (std::uint64_t) (bus::kAudioFrames / 2))
        {
            readPos = written - cushion;
            result = AudioRead::resynced;
        }

        constexpr std::uint64_t mask = bus::kAudioFrames - 1;
        for (int k = 0; k < numFrames; ++k)
        {
            const auto at = (std::size_t) ((readPos + (std::uint64_t) k) & mask) * 2;
            left[k] = s.audio[at];
            right[k] = s.audio[at + 1];
        }
        readPos += (std::uint64_t) numFrames;
        return result;
    }
    return AudioRead::none;
}

bool SharedBus::ownsSlot() const noexcept
{
    return mySlot >= 0 && mem->slots[mySlot].owner.load (std::memory_order_relaxed) == myId;
}

void SharedBus::heartbeat() noexcept
{
    const auto now = nowMs();
    if (mySlot >= 0 && mem->slots[mySlot].owner.load (std::memory_order_relaxed) == myId)
        mem->slots[mySlot].heartbeatMs.store (now, std::memory_order_relaxed);
    if (myHubId != 0 && mem->hub.owner.load (std::memory_order_relaxed) == myHubId)
        mem->hub.heartbeatMs.store (now, std::memory_order_relaxed);
}

bool SharedBus::read (std::uint64_t id, bus::TrackData& out, bool* isStale) const noexcept
{
    if (id == 0)
        return false;

    const auto now = nowMs();
    for (int i = 0; i < bus::kMaxSlots; ++i)
    {
        const auto& s = mem->slots[i];
        if (s.owner.load (std::memory_order_acquire) != id)
            continue;
        if (! seqRead (s.sequence, s.data, out))
            return false;
        if (isStale != nullptr)
            *isStale = now - s.heartbeatMs.load (std::memory_order_relaxed) > bus::kStaleAfterMs;
        return out.kind == bus::Kind::track;
    }
    return false;
}

bool SharedBus::sendRemote (std::uint64_t trackId, const bus::RemoteCommand& command) noexcept
{
    if (trackId == 0)
        return false;
    for (int i = 0; i < bus::kMaxSlots; ++i)
    {
        auto& s = mem->slots[i];
        if (s.owner.load (std::memory_order_acquire) != trackId)
            continue;
        bus::RemoteCommand next = command;
        bus::RemoteCommand previous;
        seqRead (s.remoteSequence, s.remote, previous);
        next.serial = previous.serial + 1;
        seqWrite (s.remoteSequence, s.remote, next);
        return true;
    }
    return false;
}

bool SharedBus::readRemote (bus::RemoteCommand& out, std::uint32_t lastSerial) const noexcept
{
    if (mySlot < 0)
        return false;
    const auto& s = mem->slots[mySlot];
    return seqRead (s.remoteSequence, s.remote, out) && out.serial != lastSerial && out.mask != 0;
}

std::vector<BusEntry> SharedBus::list() const
{
    std::vector<BusEntry> entries;
    const auto now = nowMs();
    for (int i = 0; i < bus::kMaxSlots; ++i)
    {
        const auto& s = mem->slots[i];
        const auto owner = s.owner.load();
        if (owner == 0)
            continue;
        const auto age = now - s.heartbeatMs.load();
        if (age > bus::kReclaimAfterMs)
            continue;

        BusEntry e;
        e.slot = i;
        e.id = owner;
        e.stale = age > bus::kStaleAfterMs;
        if (seqRead (s.sequence, s.data, e.data) && e.data.kind == bus::Kind::track)
            entries.push_back (e);
    }
    return entries;
}

//==============================================================================
bool SharedBus::claimHub (std::uint64_t instanceId)
{
    auto& h = mem->hub;
    auto current = h.owner.load();
    const auto now = nowMs();
    const bool available = current == 0 || current == instanceId
                           || now - h.heartbeatMs.load() > bus::kStaleAfterMs;
    if (available && h.owner.compare_exchange_strong (current, instanceId))
    {
        h.heartbeatMs.store (now);
        if (current != 0 && current != instanceId)
            unlockAbandoned (h.sequence);
        myHubId = instanceId;
        return true;
    }
    return false;
}

bool SharedBus::ensureHub (std::uint64_t instanceId)
{
    if (mem->hub.owner.load() == instanceId && instanceId != 0)
    {
        myHubId = instanceId;
        return true;
    }
    // Lost it (another Hub instance took over while this one stalled, then went away),
    // or never had it: take it whenever it is free or its holder stopped beating.
    myHubId = 0;
    return claimHub (instanceId);
}

void SharedBus::releaseHub()
{
    if (myHubId == 0)
        return;
    auto& h = mem->hub;
    if (h.owner.load() == myHubId)
    {
        seqWrite (h.sequence, h.data, bus::HubData {});
        auto expected = myHubId;
        h.owner.compare_exchange_strong (expected, 0);
    }
    myHubId = 0;
}

void SharedBus::publishHub (const bus::HubData& data) noexcept
{
    if (myHubId == 0 || mem->hub.owner.load (std::memory_order_relaxed) != myHubId)
        return;
    seqTryWrite (mem->hub.sequence, mem->hub.data, data);
    mem->hub.heartbeatMs.store (nowMs(), std::memory_order_relaxed);
}

bool SharedBus::readHub (bus::HubData& out, bool* busy) const noexcept
{
    const auto& h = mem->hub;
    if (busy != nullptr)
        *busy = false;
    if (h.owner.load (std::memory_order_acquire) == 0
        || nowMs() - h.heartbeatMs.load (std::memory_order_relaxed) > bus::kStaleAfterMs)
    {
        out = bus::HubData {};
        return false;
    }
    if (! seqRead (h.sequence, h.data, out))
    {
        // A live Hub that was being written at that moment: the caller may keep its last copy.
        if (busy != nullptr)
            *busy = true;
        out = bus::HubData {};
        return false;
    }
    return true;
}

} // namespace ff
