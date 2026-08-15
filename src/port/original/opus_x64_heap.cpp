#include "opus_x64_compat.h"
#include "opus_x64_heap.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <limits>

namespace {

struct NativeHandle {
    void* data;
    std::size_t size;
};

struct NativeSegment {
    void* data;
    std::size_t size;
};

static_assert(offsetof(NativeHandle, data) == 0);

[[nodiscard]] HANDLE process_heap() noexcept { return GetProcessHeap(); }

constexpr int kPrcTokenNil = 0x3fff;
constexpr int kPrcTokenMac = kPrcTokenNil - 1;
constexpr int kCompactHandleMac = 0x3fff;
constexpr std::size_t kNativeHandleSlots = 131071;
constexpr std::size_t kSegmentSlots = 65536;
constexpr std::size_t kGuardBytes = 64;
constexpr std::size_t kMaxAllocationBytes = 256u * 1024u * 1024u;
constexpr unsigned char kGuardValue = 0xa5;
SRWLOCK prc_registry_lock = SRWLOCK_INIT;
SRWLOCK compact_handle_lock = SRWLOCK_INIT;
SRWLOCK native_handle_lock = SRWLOCK_INIT;
SRWLOCK segment_registry_lock = SRWLOCK_INIT;
void** prc_handles[kPrcTokenNil]{};
void** compact_handles[kCompactHandleMac + 1]{};
void** native_handles[kNativeHandleSlots]{};
std::array<NativeSegment, kSegmentSlots> native_segments{};
std::atomic_size_t heap_bytes_used{0};

[[nodiscard]] constexpr std::size_t payload_size(
    const std::size_t logical_size) noexcept {
    return logical_size == 0 ? 1 : logical_size;
}

[[nodiscard]] constexpr bool guarded_size(
    const std::size_t logical_size, std::size_t* result) noexcept {
    if (logical_size > kMaxAllocationBytes) return false;
    const std::size_t payload = payload_size(logical_size);
    if (payload > (std::numeric_limits<std::size_t>::max)() - kGuardBytes) {
        return false;
    }
    *result = payload + kGuardBytes;
    return true;
}

[[nodiscard]] bool valid_native_handle(void** opaque_handle) noexcept {
    if (opaque_handle == nullptr) return false;
    void** const tombstone = reinterpret_cast<void**>(static_cast<uintptr_t>(1));
    const std::size_t start =
        (reinterpret_cast<uintptr_t>(opaque_handle) >> 4) % kNativeHandleSlots;
    AcquireSRWLockShared(&native_handle_lock);
    bool valid = false;
    for (std::size_t probe = 0; probe < kNativeHandleSlots; ++probe) {
        void** const candidate =
            native_handles[(start + probe) % kNativeHandleSlots];
        if (candidate == opaque_handle) {
            valid = true;
            break;
        }
        if (candidate == nullptr) break;
        if (candidate == tombstone) continue;
    }
    ReleaseSRWLockShared(&native_handle_lock);
    return valid;
}

[[nodiscard]] bool register_native_handle(void** opaque_handle) noexcept {
    void** const tombstone = reinterpret_cast<void**>(static_cast<uintptr_t>(1));
    const std::size_t start =
        (reinterpret_cast<uintptr_t>(opaque_handle) >> 4) % kNativeHandleSlots;
    AcquireSRWLockExclusive(&native_handle_lock);
    std::size_t first_tombstone = kNativeHandleSlots;
    for (std::size_t probe = 0; probe < kNativeHandleSlots; ++probe) {
        const std::size_t index = (start + probe) % kNativeHandleSlots;
        if (native_handles[index] == opaque_handle) {
            ReleaseSRWLockExclusive(&native_handle_lock);
            return true;
        }
        if (native_handles[index] == tombstone &&
            first_tombstone == kNativeHandleSlots) first_tombstone = index;
        if (native_handles[index] == nullptr) {
            native_handles[first_tombstone == kNativeHandleSlots ?
                               index : first_tombstone] = opaque_handle;
            ReleaseSRWLockExclusive(&native_handle_lock);
            return true;
        }
    }
    if (first_tombstone != kNativeHandleSlots) {
        native_handles[first_tombstone] = opaque_handle;
        ReleaseSRWLockExclusive(&native_handle_lock);
        return true;
    }
    ReleaseSRWLockExclusive(&native_handle_lock);
    return false;
}

[[nodiscard]] bool unregister_native_handle(void** opaque_handle) noexcept {
    void** const tombstone = reinterpret_cast<void**>(static_cast<uintptr_t>(1));
    const std::size_t start =
        (reinterpret_cast<uintptr_t>(opaque_handle) >> 4) % kNativeHandleSlots;
    AcquireSRWLockExclusive(&native_handle_lock);
    for (std::size_t probe = 0; probe < kNativeHandleSlots; ++probe) {
        const std::size_t index = (start + probe) % kNativeHandleSlots;
        if (native_handles[index] == opaque_handle) {
            native_handles[index] = tombstone;
            ReleaseSRWLockExclusive(&native_handle_lock);
            return true;
        }
        if (native_handles[index] == nullptr) break;
    }
    ReleaseSRWLockExclusive(&native_handle_lock);
    return false;
}

void set_guard(void* const allocation,
               const std::size_t logical_size) noexcept {
    std::memset(static_cast<unsigned char*>(allocation) +
                    payload_size(logical_size),
                kGuardValue, kGuardBytes);
}

[[nodiscard]] bool guard_is_valid(
    const void* const allocation,
    const std::size_t logical_size) noexcept {
    const auto* guard = static_cast<const unsigned char*>(allocation) +
                        payload_size(logical_size);
    for (std::size_t index = 0; index < kGuardBytes; ++index) {
        if (guard[index] != kGuardValue) {
            return false;
        }
    }
    return true;
}

[[noreturn]] void report_guard_failure() noexcept {
    constexpr DWORD kOpusHeapGuardFailure = 0xe0421001u;
    RaiseException(kOpusHeapGuardFailure, EXCEPTION_NONCONTINUABLE, 0,
                   nullptr);
    TerminateProcess(GetCurrentProcess(), kOpusHeapGuardFailure);
    // Unreachable: both calls above are terminal. __assume is MSVC-only.
#if defined(_MSC_VER)
    __assume(false);
#else
    __builtin_unreachable();
#endif
}

void unregister_prc_handle(void** handle) noexcept {
    AcquireSRWLockExclusive(&prc_registry_lock);
    for (int token = 1; token <= kPrcTokenMac; ++token) {
        if (prc_handles[token] == handle) {
            prc_handles[token] = nullptr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&prc_registry_lock);
}

void unregister_compact_handle(void** handle) noexcept {
    AcquireSRWLockExclusive(&compact_handle_lock);
    for (int token = 1; token <= kCompactHandleMac; ++token) {
        if (compact_handles[token] == handle) {
            compact_handles[token] = nullptr;
            break;
        }
    }
    ReleaseSRWLockExclusive(&compact_handle_lock);
}

}  // namespace

extern "C" SB sbMac = 0;
extern "C" SB _sbCur = sbDds;
extern "C" SB sbCur = sbDds;
extern "C" WORD mpsbps[kSegmentSlots]{};

extern "C" int FInitSegTable(const SB requested_max) {
    sbMac = (std::min)(requested_max,
                      static_cast<SB>(kSegmentSlots));
    return 1;
}

extern "C" HP OpusHpOfSbIbImpl(const SB segment, const uintptr_t offset) {
    if (segment == sbDds) {
        return reinterpret_cast<HP>(offset);
    }
    if (segment >= kSegmentSlots) {
        return reinterpret_cast<HP>(static_cast<uintptr_t>(segment) + offset);
    }
    AcquireSRWLockShared(&segment_registry_lock);
    const auto& slot = native_segments[static_cast<std::size_t>(segment)];
    void* base = slot.data;
    const std::size_t size = slot.size;
    ReleaseSRWLockShared(&segment_registry_lock);
    return base == nullptr || offset > size
               ? nullptr
               : reinterpret_cast<HP>(reinterpret_cast<uintptr_t>(base) +
                                      offset);
}

extern "C" SB OpusSbOfHp(const void* pointer) {
    if (pointer == nullptr) {
        return 0;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    AcquireSRWLockShared(&segment_registry_lock);
    for (std::size_t index = 2; index < kSegmentSlots; ++index) {
        const auto& segment = native_segments[index];
        const uintptr_t base = reinterpret_cast<uintptr_t>(segment.data);
        if (base != 0 && address >= base && address < base + segment.size) {
            ReleaseSRWLockShared(&segment_registry_lock);
            return static_cast<SB>(index);
        }
    }
    ReleaseSRWLockShared(&segment_registry_lock);
    return sbDds;
}

/* Some original modules declared the native helper as a routine instead of
 * including sbmgr.h's macro spelling. */
extern "C" SB SbOfHp(const void* pointer) { return OpusSbOfHp(pointer); }

extern "C" IB OpusIbOfHp(const void* pointer) {
    if (pointer == nullptr) {
        return 0;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    AcquireSRWLockShared(&segment_registry_lock);
    for (std::size_t index = 2; index < kSegmentSlots; ++index) {
        const auto& segment = native_segments[index];
        const uintptr_t base = reinterpret_cast<uintptr_t>(segment.data);
        if (base != 0 && address >= base && address < base + segment.size) {
            ReleaseSRWLockShared(&segment_registry_lock);
            return static_cast<IB>(address - base);
        }
    }
    ReleaseSRWLockShared(&segment_registry_lock);
    return static_cast<IB>(address);
}

extern "C" unsigned int CbAllocSb(const SB segment,
                                    const unsigned int byte_count, int) {
    if (segment == sbDds) {
        return byte_count;
    }
    if (segment < 2 || segment >= kSegmentSlots) {
        return 0;
    }
    std::size_t allocation_size = 0;
    if (!guarded_size(byte_count, &allocation_size)) {
        return 0;
    }
    void* allocation =
        HeapAlloc(process_heap(), HEAP_ZERO_MEMORY, allocation_size);
    if (allocation == nullptr) {
        return 0;
    }
    set_guard(allocation, byte_count);
    AcquireSRWLockExclusive(&segment_registry_lock);
    auto& slot = native_segments[static_cast<std::size_t>(segment)];
    if (slot.data != nullptr) {
        ReleaseSRWLockExclusive(&segment_registry_lock);
        HeapFree(process_heap(), 0, allocation);
        return 0;
    }
    slot = {allocation, byte_count};
    mpsbps[static_cast<std::size_t>(segment)] = 1;
    ReleaseSRWLockExclusive(&segment_registry_lock);
    heap_bytes_used.fetch_add(byte_count, std::memory_order_relaxed);
    return byte_count;
}

extern "C" unsigned int CbReallocSb(const SB segment,
                                      const unsigned int byte_count, int) {
    if (segment == sbDds) {
        return byte_count;
    }
    if (segment < 2 || segment >= kSegmentSlots) {
        return 0;
    }
    AcquireSRWLockExclusive(&segment_registry_lock);
    auto& slot = native_segments[static_cast<std::size_t>(segment)];
    if (slot.data == nullptr) {
        ReleaseSRWLockExclusive(&segment_registry_lock);
        return CbAllocSb(segment, byte_count, 0);
    }
    const std::size_t old_size = slot.size;
    if (!guard_is_valid(slot.data, old_size)) {
        ReleaseSRWLockExclusive(&segment_registry_lock);
        report_guard_failure();
    }
    std::size_t allocation_size = 0;
    if (!guarded_size(byte_count, &allocation_size)) {
        ReleaseSRWLockExclusive(&segment_registry_lock);
        return 0;
    }
    void* resized =
        HeapReAlloc(process_heap(), HEAP_ZERO_MEMORY, slot.data,
                    allocation_size);
    if (resized == nullptr) {
        ReleaseSRWLockExclusive(&segment_registry_lock);
        return 0;
    }
    set_guard(resized, byte_count);
    slot = {resized, byte_count};
    ReleaseSRWLockExclusive(&segment_registry_lock);
    if (byte_count >= old_size) {
        heap_bytes_used.fetch_add(byte_count - old_size,
                                  std::memory_order_relaxed);
    } else {
        heap_bytes_used.fetch_sub(old_size - byte_count,
                                  std::memory_order_relaxed);
    }
    return byte_count;
}

extern "C" void FreeSb(const SB segment) {
    if (segment < 2 || segment >= kSegmentSlots) {
        return;
    }
    AcquireSRWLockExclusive(&segment_registry_lock);
    auto& slot = native_segments[static_cast<std::size_t>(segment)];
    void* allocation = slot.data;
    const std::size_t byte_count = slot.size;
    slot = {};
    mpsbps[static_cast<std::size_t>(segment)] = 0;
    ReleaseSRWLockExclusive(&segment_registry_lock);
    if (allocation != nullptr) {
        if (!guard_is_valid(allocation, byte_count)) {
            report_guard_failure();
        }
        heap_bytes_used.fetch_sub(byte_count, std::memory_order_relaxed);
        HeapFree(process_heap(), 0, allocation);
    }
}

extern "C" unsigned int CbSizeSb(const SB segment) {
    if (segment == sbDds || segment >= kSegmentSlots) {
        return 0;
    }
    AcquireSRWLockShared(&segment_registry_lock);
    const std::size_t size =
        native_segments[static_cast<std::size_t>(segment)].size;
    ReleaseSRWLockShared(&segment_registry_lock);
    return static_cast<unsigned int>((std::min)(
        size, static_cast<std::size_t>((std::numeric_limits<unsigned int>::max)())));
}

extern "C" SB SbAllocEmmCb(const unsigned int byte_count) {
    const std::size_t limit = (std::min)(
        static_cast<std::size_t>(sbMac), kSegmentSlots);
    for (std::size_t index = 2; index < limit; ++index) {
        AcquireSRWLockShared(&segment_registry_lock);
        const bool available = native_segments[index].data == nullptr;
        ReleaseSRWLockShared(&segment_registry_lock);
        if (available && CbAllocSb(static_cast<SB>(index), byte_count, 0) != 0) {
            return static_cast<SB>(index);
        }
    }
    return 0;
}

extern "C" void FreeEmmSb(const SB segment) { FreeSb(segment); }
extern "C" long CbFreeEmm() { return 0; }
extern "C" void EndEmm() {}
extern "C" int ReloadSb(const SB segment) {
    _sbCur = sbCur = segment;
    return segment != 0;
}

extern "C" SB SbScanNext(int) {
    const std::size_t limit = (std::min)(
        static_cast<std::size_t>(sbMac), kSegmentSlots);
    for (std::size_t index = 2; index < limit; ++index) {
        if (mpsbps[index] == 0) {
            return static_cast<SB>(index);
        }
    }
    return 0;
}

extern "C" void ResetSbCur() { _sbCur = sbCur = sbDds; }

extern "C" void* LpConvHp(void* pointer) { return pointer; }

extern "C" int FAssureHcb(void*** handle_address,
                            const int byte_count_needed, int* byte_count,
                            int* byte_capacity) {
    if (handle_address == nullptr || byte_count_needed < 0 ||
        byte_capacity == nullptr || *byte_capacity < 0) {
        return 0;
    }
    if (byte_count != nullptr) {
        *byte_count = byte_count_needed;
    }
    if (*handle_address != nullptr && byte_count_needed <= *byte_capacity) {
        return 1;
    }
    const int doubled = *byte_capacity >
            (std::numeric_limits<int>::max)() / 2 ?
        (std::numeric_limits<int>::max)() :
        (*byte_capacity > 0 ? *byte_capacity * 2 : 16);
    const int new_capacity = (std::max)(byte_count_needed, doubled);
    if (*handle_address == nullptr) {
        *handle_address = OpusHAllocateCb(static_cast<std::size_t>(new_capacity));
        if (*handle_address == nullptr) {
            return 0;
        }
    } else if (!OpusFChngSizeHCb(*handle_address,
                                 static_cast<std::size_t>(new_capacity), 0)) {
        return 0;
    }
    *byte_capacity = new_capacity;
    return 1;
}

extern "C" unsigned HcCompactH(void** handle) {
    if (handle == nullptr) {
        return 0;
    }
    AcquireSRWLockExclusive(&compact_handle_lock);
    for (int token = 1; token <= kCompactHandleMac; ++token) {
        if (compact_handles[token] == handle) {
            ReleaseSRWLockExclusive(&compact_handle_lock);
            return static_cast<unsigned>(token);
        }
    }
    for (int token = 1; token <= kCompactHandleMac; ++token) {
        if (compact_handles[token] == nullptr) {
            compact_handles[token] = handle;
            ReleaseSRWLockExclusive(&compact_handle_lock);
            return static_cast<unsigned>(token);
        }
    }
    ReleaseSRWLockExclusive(&compact_handle_lock);
    return 0;
}

extern "C" void** HExpandHc(const unsigned compact_handle) {
    if (compact_handle == 0 || compact_handle > kCompactHandleMac) {
        return nullptr;
    }
    AcquireSRWLockShared(&compact_handle_lock);
    void** const handle = compact_handles[compact_handle];
    ReleaseSRWLockShared(&compact_handle_lock);
    return handle;
}

extern "C" void** OpusHAllocateCb(const std::size_t byte_count) {
    auto* handle = static_cast<NativeHandle*>(
        HeapAlloc(process_heap(), HEAP_ZERO_MEMORY, sizeof(NativeHandle)));
    if (handle == nullptr) {
        return nullptr;
    }
    std::size_t allocation_size = 0;
    if (!guarded_size(byte_count, &allocation_size)) {
        HeapFree(process_heap(), 0, handle);
        return nullptr;
    }
    handle->data = HeapAlloc(process_heap(), 0, allocation_size);
    if (handle->data == nullptr) {
        HeapFree(process_heap(), 0, handle);
        return nullptr;
    }
    set_guard(handle->data, byte_count);
    handle->size = byte_count;
    if (!register_native_handle(reinterpret_cast<void**>(handle))) {
        HeapFree(process_heap(), 0, handle->data);
        HeapFree(process_heap(), 0, handle);
        return nullptr;
    }
    heap_bytes_used.fetch_add(byte_count, std::memory_order_relaxed);
    return reinterpret_cast<void**>(handle);
}

extern "C" void OpusFreeH(void** opaque_handle) {
    if (opaque_handle == nullptr) {
        return;
    }
    if (!unregister_native_handle(opaque_handle)) {
        report_guard_failure();
    }
    unregister_prc_handle(opaque_handle);
    unregister_compact_handle(opaque_handle);
    auto* handle = reinterpret_cast<NativeHandle*>(opaque_handle);
    if (handle->data != nullptr &&
        !guard_is_valid(handle->data, handle->size)) {
        report_guard_failure();
    }
    heap_bytes_used.fetch_sub(handle->size, std::memory_order_relaxed);
    if (handle->data != nullptr) {
        HeapFree(process_heap(), 0, handle->data);
    }
    HeapFree(process_heap(), 0, handle);
}

extern "C" void OpusFreePh(void*** handle_address) {
    if (handle_address == nullptr || *handle_address == nullptr) {
        return;
    }
    OpusFreeH(*handle_address);
    *handle_address = nullptr;
}

extern "C" int OpusFChngSizeHCb(void** opaque_handle,
                                  const std::size_t byte_count,
                                  const int allow_shrink) {
    if (opaque_handle == nullptr) {
        return 0;
    }
    if (!valid_native_handle(opaque_handle)) {
        report_guard_failure();
    }
    auto* handle = reinterpret_cast<NativeHandle*>(opaque_handle);
    if (!allow_shrink && byte_count <= handle->size) {
        return 1;
    }
    if (handle->data != nullptr &&
        !guard_is_valid(handle->data, handle->size)) {
        report_guard_failure();
    }
    std::size_t allocation_size = 0;
    if (!guarded_size(byte_count, &allocation_size)) {
        return 0;
    }
    void* resized = HeapReAlloc(process_heap(), 0, handle->data, allocation_size);
    if (resized == nullptr) {
        return 0;
    }
    const std::size_t old_size = handle->size;
    set_guard(resized, byte_count);
    handle->data = resized;
    handle->size = byte_count;
    if (byte_count >= old_size) {
        heap_bytes_used.fetch_add(byte_count - old_size,
                                  std::memory_order_relaxed);
    } else {
        heap_bytes_used.fetch_sub(old_size - byte_count,
                                  std::memory_order_relaxed);
    }
    return 1;
}

extern "C" int OpusFChngSizePhqLcb(void*** handle_address,
                                     const std::size_t byte_count) {
    return handle_address != nullptr &&
           OpusFChngSizeHCb(*handle_address, byte_count, 1);
}

extern "C" size_t OpusCbOfH(void** opaque_handle) {
    return !valid_native_handle(opaque_handle)
               ? 0
               : reinterpret_cast<NativeHandle*>(opaque_handle)->size;
}

extern "C" size_t OpusHeapBytesUsed() {
    return heap_bytes_used.load(std::memory_order_relaxed);
}

extern "C" void* OpusDerefH(void** opaque_handle) {
    return !valid_native_handle(opaque_handle)
               ? nullptr
               : reinterpret_cast<NativeHandle*>(opaque_handle)->data;
}

extern "C" void* OpusHpAlloc(const std::size_t byte_count) {
    if (byte_count > kMaxAllocationBytes) return nullptr;
    void* allocation =
        HeapAlloc(process_heap(), 0, byte_count == 0 ? 1 : byte_count);
    if (allocation != nullptr) {
        const SIZE_T size = HeapSize(process_heap(), 0, allocation);
        if (size != static_cast<SIZE_T>(-1)) {
            heap_bytes_used.fetch_add(size, std::memory_order_relaxed);
        }
    }
    return allocation;
}

extern "C" void OpusFreeHp(void* pointer) {
    if (pointer != nullptr) {
        const SIZE_T size = HeapSize(process_heap(), 0, pointer);
        if (size == static_cast<SIZE_T>(-1)) report_guard_failure();
        heap_bytes_used.fetch_sub(size, std::memory_order_relaxed);
        HeapFree(process_heap(), 0, pointer);
    }
}

extern "C" int OpusPrcTokenFromHandle(void** handle) {
    if (handle == nullptr) {
        return kPrcTokenNil;
    }

    int free_token = 0;
    AcquireSRWLockExclusive(&prc_registry_lock);
    for (int token = 1; token <= kPrcTokenMac; ++token) {
        if (prc_handles[token] == handle) {
            ReleaseSRWLockExclusive(&prc_registry_lock);
            return token;
        }
        if (free_token == 0 && prc_handles[token] == nullptr) {
            free_token = token;
        }
    }
    if (free_token != 0) {
        prc_handles[free_token] = handle;
    }
    ReleaseSRWLockExclusive(&prc_registry_lock);
    return free_token == 0 ? kPrcTokenNil : free_token;
}

extern "C" void** OpusPrcHandleFromToken(const int token) {
    if (token <= 0 || token > kPrcTokenMac) {
        return nullptr;
    }
    AcquireSRWLockShared(&prc_registry_lock);
    void** const handle = prc_handles[token];
    ReleaseSRWLockShared(&prc_registry_lock);
    return handle;
}

/* Flat-memory implementations of the LMEM public API used by the original
 * interpreter and SDM-facing code.  SB is intentionally ignored: every
 * native handle is addressable in the process' single 64-bit address space. */
extern "C" void** PpvAllocCb(SB, WORD byte_count) {
    return OpusHAllocateCb(byte_count);
}

extern "C" void** PpvAllocCbWW(SB, int byte_count) {
    return byte_count < 0 ? nullptr
                          : OpusHAllocateCb(static_cast<std::size_t>(byte_count));
}

extern "C" int FReallocPpv(SB, void** handle, WORD byte_count) {
    return OpusFChngSizeHCb(handle, byte_count, 1);
}

extern "C" void FreePpv(SB, void** handle) {
    OpusFreeH(handle);
}

extern "C" WORD CbSizePpv(SB, void** handle) {
    const std::size_t size = OpusCbOfH(handle);
    return static_cast<WORD>((std::min)(size,
        static_cast<std::size_t>((std::numeric_limits<WORD>::max)())));
}

extern "C" void* PvAllocFixedCb(SB, WORD byte_count) {
    return OpusHpAlloc(byte_count);
}

extern "C" WORD CbSizeFixedPv(SB, void* pointer) {
    if (pointer == nullptr) {
        return 0;
    }
    const SIZE_T size = HeapSize(process_heap(), 0, pointer);
    return size == static_cast<SIZE_T>(-1)
               ? 0
               : static_cast<WORD>((std::min)(size,
                    static_cast<SIZE_T>((std::numeric_limits<WORD>::max)())));
}

extern "C" void CreateHeap(SB) {}

extern "C" WORD CbCompactHeap(SB, WORD requested) {
    HeapCompact(process_heap(), 0);
    return requested;
}

extern "C" WORD CbAvailHeap(SB) {
    /* LMEM callers use this as a 16-bit capacity hint, not as an allocation
     * guarantee. Native allocation failure remains authoritative. */
    return (std::numeric_limits<WORD>::max)();
}
