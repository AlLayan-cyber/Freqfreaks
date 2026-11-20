// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ff/SharedBus.h"

#include <chrono>
#include <cstring>
#include <random>

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

    template <typename Data>
    void seqWrite (std::atomic<std::uint32_t>& sequence, Data& target, const Data& source) noexcept
    {
        const auto seq = sequence.load (std::memory_order_relaxed);
        sequence.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        std::memcpy (static_cast<void*> (&target), &source, sizeof (Data));
        std::atomic_thread_fence (std::memory_order_release);
        sequence.store (seq + 2, std::memory_order_release);
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
                mySlot = i;
                myId = instanceId;
                bus::TrackData empty;
                empty.kind = bus::Kind::track;
                seqWrite (s.sequence, s.data, empty);
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
    bus::TrackData empty;
    seqWrite (s.sequence, s.data, empty);
    auto expected = myId;
    s.owner.compare_exchange_strong (expected, 0);
    mySlot = -1;
    myId = 0;
}

void SharedBus::publish (const bus::TrackData& data) noexcept
{
    if (mySlot < 0)
        return;
    auto& s = mem->slots[mySlot];
    seqWrite (s.sequence, s.data, data);
    s.heartbeatMs.store (nowMs(), std::memory_order_relaxed);
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
        myHubId = instanceId;
        return true;
    }
    return false;
}

void SharedBus::releaseHub()
{
    if (myHubId == 0)
        return;
    auto& h = mem->hub;
    seqWrite (h.sequence, h.data, bus::HubData {});
    auto expected = myHubId;
    h.owner.compare_exchange_strong (expected, 0);
    myHubId = 0;
}

void SharedBus::publishHub (const bus::HubData& data) noexcept
{
    if (myHubId == 0 || mem->hub.owner.load (std::memory_order_relaxed) != myHubId)
        return;
    seqWrite (mem->hub.sequence, mem->hub.data, data);
    mem->hub.heartbeatMs.store (nowMs(), std::memory_order_relaxed);
}

bool SharedBus::readHub (bus::HubData& out) const noexcept
{
    const auto& h = mem->hub;
    if (h.owner.load (std::memory_order_acquire) == 0
        || nowMs() - h.heartbeatMs.load (std::memory_order_relaxed) > bus::kStaleAfterMs
        || ! seqRead (h.sequence, h.data, out))
    {
        out = bus::HubData {};
        return false;
    }
    return true;
}

} // namespace ff
