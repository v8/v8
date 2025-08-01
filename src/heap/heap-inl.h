// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_INL_H_
#define V8_HEAP_HEAP_INL_H_

#include "src/heap/heap.h"
// Include the non-inl header before the rest of the headers.

#include <atomic>
#include <optional>

// Clients of this interface shouldn't depend on lots of heap internals.
// Avoid including anything but `heap.h` from `src/heap` where possible.
#include "src/base/atomic-utils.h"
#include "src/base/platform/mutex.h"
#include "src/common/assert-scope.h"
#include "src/common/code-memory-access-inl.h"
#include "src/execution/isolate-data.h"
#include "src/execution/isolate.h"
#include "src/heap/code-range.h"
#include "src/heap/heap-allocator-inl.h"
#include "src/heap/heap-layout-inl.h"
#include "src/heap/heap-write-barrier.h"
#include "src/heap/large-spaces.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk-inl.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/new-spaces-inl.h"
#include "src/heap/paged-spaces-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/safepoint.h"
#include "src/heap/spaces-inl.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/cell-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots-inl.h"
#include "src/objects/visitors-inl.h"
#include "src/roots/static-roots.h"
#include "src/utils/allocation.h"
#include "src/utils/ostreams.h"
#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

template <typename T>
Tagged<T> ForwardingAddress(Tagged<T> heap_obj) {
  MapWord map_word = Cast<HeapObject>(heap_obj)->map_word(kRelaxedLoad);

  if (map_word.IsForwardingAddress()) {
    return Cast<T>(map_word.ToForwardingAddress(heap_obj));
  } else if (Heap::InFromPage(heap_obj)) {
    DCHECK(!v8_flags.minor_ms);
    return Tagged<T>();
  } else {
    return heap_obj;
  }
}

Isolate* Heap::isolate() const { return Isolate::FromHeap(this); }

bool Heap::IsMainThread() const {
  return isolate()->thread_id() == ThreadId::Current();
}

uint64_t Heap::external_memory() const { return external_memory_.total(); }

RootsTable& Heap::roots_table() { return isolate()->roots_table(); }

#define ROOT_ACCESSOR(Type, name, CamelName)                                   \
  Tagged<Type> Heap::name() {                                                  \
    return Cast<Type>(Tagged<Object>(roots_table()[RootIndex::k##CamelName])); \
  }
MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

#define STATIC_ROOTS_FAILED_MSG                                            \
  "Read-only heap layout changed. Run `tools/dev/gen-static-roots.py` to " \
  "update static-roots.h."
#if V8_STATIC_ROOTS_BOOL
// Check all read only roots are allocated where we expect it. Skip `Exception`
// which changes during setup-heap-internal.
#define DCHECK_STATIC_ROOT(obj, name)                                        \
  if constexpr (RootsTable::IsReadOnly(RootIndex::k##name) &&                \
                RootIndex::k##name != RootIndex::kException) {               \
    DCHECK_WITH_MSG(V8HeapCompressionScheme::CompressObject(obj.ptr()) ==    \
                        StaticReadOnlyRootsPointerTable[static_cast<size_t>( \
                            RootIndex::k##name)],                            \
                    STATIC_ROOTS_FAILED_MSG);                                \
  }
#else
#define DCHECK_STATIC_ROOT(obj, name)
#endif

#define ROOT_ACCESSOR(type, name, CamelName)                                   \
  void Heap::set_##name(Tagged<type> value) {                                  \
    /* The deserializer makes use of the fact that these common roots are */   \
    /* never in new space and never on a page that is being compacted.    */   \
    DCHECK_IMPLIES(deserialization_complete(),                                 \
                   !RootsTable::IsImmortalImmovable(RootIndex::k##CamelName)); \
    if constexpr (RootsTable::IsImmortalImmovable(RootIndex::k##CamelName)) {  \
      /* Cast via object to avoid compile errors when trying to cast a Smi */  \
      /* to HeapObject (these Smis will anyway be excluded by */               \
      /* RootsTable::IsImmortalImmovable but this isn't enough for the*/       \
      /* compiler, even with `if constexpr`)*/                                 \
      DCHECK(IsImmovable(Cast<HeapObject>(Cast<Object>(value))));              \
    }                                                                          \
    DCHECK_STATIC_ROOT(value, CamelName);                                      \
    roots_table()[RootIndex::k##CamelName] = value.ptr();                      \
  }
ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR
#undef CHECK_STATIC_ROOT
#undef STATIC_ROOTS_FAILED_MSG

void Heap::SetRootMaterializedObjects(Tagged<FixedArray> objects) {
  roots_table()[RootIndex::kMaterializedObjects] = objects.ptr();
}

void Heap::SetRootScriptList(Tagged<Object> value) {
  roots_table()[RootIndex::kScriptList] = value.ptr();
}

void Heap::SetMessageListeners(Tagged<ArrayList> value) {
  roots_table()[RootIndex::kMessageListeners] = value.ptr();
}

void Heap::SetFunctionsMarkedForManualOptimization(Tagged<Object> hash_table) {
  DCHECK(IsObjectHashTable(hash_table) || IsUndefined(hash_table, isolate()));
  roots_table()[RootIndex::kFunctionsMarkedForManualOptimization] =
      hash_table.ptr();
}

void Heap::SetSmiStringCache(Tagged<SmiStringCache> cache) {
  set_smi_string_cache(cache);
}

void Heap::SetDoubleStringCache(Tagged<DoubleStringCache> cache) {
  set_double_string_cache(cache);
}

#if V8_ENABLE_WEBASSEMBLY
void Heap::SetWasmCanonicalRtts(Tagged<WeakFixedArray> rtts) {
  set_wasm_canonical_rtts(rtts);
}
void Heap::SetJSToWasmWrappers(Tagged<WeakFixedArray> js_to_wasm_wrappers) {
  set_js_to_wasm_wrappers(js_to_wasm_wrappers);
}
#endif  // V8_ENABLE_WEBASSEMBLY

PagedSpace* Heap::paged_space(int idx) const {
  DCHECK(idx == OLD_SPACE || idx == CODE_SPACE || idx == SHARED_SPACE ||
         idx == TRUSTED_SPACE || idx == SHARED_TRUSTED_SPACE);
  return static_cast<PagedSpace*>(space_[idx].get());
}

Space* Heap::space(int idx) const { return space_[idx].get(); }

Address* Heap::NewSpaceAllocationTopAddress() {
  return new_space_ || v8_flags.sticky_mark_bits
             ? isolate()->isolate_data()->new_allocation_info_.top_address()
             : nullptr;
}

Address* Heap::NewSpaceAllocationLimitAddress() {
  return new_space_ || v8_flags.sticky_mark_bits
             ? isolate()->isolate_data()->new_allocation_info_.limit_address()
             : nullptr;
}

Address* Heap::OldSpaceAllocationTopAddress() {
  return allocator()->old_space_allocator()->allocation_top_address();
}

Address* Heap::OldSpaceAllocationLimitAddress() {
  return allocator()->old_space_allocator()->allocation_limit_address();
}

inline const base::AddressRegion& Heap::code_region() {
  static constexpr base::AddressRegion kEmptyRegion;
  return code_range_ ? code_range_->reservation()->region() : kEmptyRegion;
}

Address Heap::code_range_base() {
  return code_range_ ? code_range_->base() : kNullAddress;
}

int Heap::MaxRegularHeapObjectSize(AllocationType allocation) {
  if (allocation == AllocationType::kCode) {
    DCHECK_EQ(MemoryChunkLayout::MaxRegularCodeObjectSize(),
              max_regular_code_object_size_);
    return max_regular_code_object_size_;
  }
  return kMaxRegularHeapObjectSize;
}

AllocationResult Heap::AllocateRaw(int size_in_bytes, AllocationType type,
                                   AllocationOrigin origin,
                                   AllocationAlignment alignment) {
  return heap_allocator_->AllocateRaw(size_in_bytes, type, origin, alignment);
}

Address Heap::AllocateRawOrFail(int size, AllocationType allocation,
                                AllocationOrigin origin,
                                AllocationAlignment alignment) {
  return heap_allocator_
      ->AllocateRawWith<HeapAllocator::kRetryOrFail>(size, allocation, origin,
                                                     alignment)
      .address();
}

void Heap::RegisterExternalString(Tagged<String> string) {
  DCHECK(IsExternalString(string));
  DCHECK(!IsThinString(string));
  external_string_table_.AddString(string);
}

void Heap::FinalizeExternalString(Tagged<String> string) {
  DCHECK(IsExternalString(string));
  Tagged<ExternalString> ext_string = Cast<ExternalString>(string);
  PageMetadata* page = PageMetadata::FromHeapObject(string);
  page->DecrementExternalBackingStoreBytes(
      ExternalBackingStoreType::kExternalString,
      ext_string->ExternalPayloadSize());
  ext_string->DisposeResource(isolate());
}

Address Heap::NewSpaceTop() {
  return new_space_ || v8_flags.sticky_mark_bits
             ? allocator()->new_space_allocator()->top()
             : kNullAddress;
}

Address Heap::NewSpaceLimit() {
  return new_space_ || v8_flags.sticky_mark_bits
             ? allocator()->new_space_allocator()->limit()
             : kNullAddress;
}

// static
bool Heap::InFromPage(Tagged<Object> object) {
  DCHECK(!HasWeakHeapObjectTag(object));
  return IsHeapObject(object) && InFromPage(Cast<HeapObject>(object));
}

// static
bool Heap::InFromPage(Tagged<MaybeObject> object) {
  Tagged<HeapObject> heap_object;
  return object.GetHeapObject(&heap_object) && InFromPage(heap_object);
}

// static
bool Heap::InFromPage(Tagged<HeapObject> heap_object) {
  return MemoryChunk::FromHeapObject(heap_object)->IsFromPage();
}

// static
bool Heap::InToPage(Tagged<Object> object) {
  DCHECK(!HasWeakHeapObjectTag(object));
  return IsHeapObject(object) && InToPage(Cast<HeapObject>(object));
}

// static
bool Heap::InToPage(Tagged<MaybeObject> object) {
  Tagged<HeapObject> heap_object;
  return object.GetHeapObject(&heap_object) && InToPage(heap_object);
}

// static
bool Heap::InToPage(Tagged<HeapObject> heap_object) {
  return MemoryChunk::FromHeapObject(heap_object)->IsToPage();
}

bool Heap::InOldSpace(Tagged<Object> object) {
  return old_space_->Contains(object) &&
         (!v8_flags.sticky_mark_bits || !HeapLayout::InYoungGeneration(object));
}

// static
Heap* Heap::FromWritableHeapObject(Tagged<HeapObject> obj) {
  MemoryChunkMetadata* chunk = MemoryChunkMetadata::FromHeapObject(obj);
  // RO_SPACE can be shared between heaps, so we can't use RO_SPACE objects to
  // find a heap. The exception is when the ReadOnlySpace is writeable, during
  // bootstrapping, so explicitly allow this case.
  SLOW_DCHECK(chunk->IsWritable());
  Heap* heap = chunk->heap();
  SLOW_DCHECK(heap != nullptr);
  return heap;
}

void Heap::CopyBlock(Address dst, Address src, size_t byte_size) {
  DCHECK(IsAligned(byte_size, kTaggedSize));
  CopyTagged(dst, src, byte_size / kTaggedSize);
}

bool Heap::IsPendingAllocationInternal(Tagged<HeapObject> object) {
  DCHECK(deserialization_complete());

  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  if (chunk->InReadOnlySpace()) return false;

  BaseSpace* base_space = chunk->Metadata(isolate())->owner();
  Address addr = object.address();

  switch (base_space->identity()) {
    case NEW_SPACE: {
      return allocator()->new_space_allocator()->IsPendingAllocation(addr);
    }

    case OLD_SPACE: {
      return allocator()->old_space_allocator()->IsPendingAllocation(addr);
    }

    case CODE_SPACE: {
      return allocator()->code_space_allocator()->IsPendingAllocation(addr);
    }

    case TRUSTED_SPACE: {
      return allocator()->trusted_space_allocator()->IsPendingAllocation(addr);
    }

    case LO_SPACE:
    case CODE_LO_SPACE:
    case TRUSTED_LO_SPACE:
    case NEW_LO_SPACE: {
      LargeObjectSpace* large_space =
          static_cast<LargeObjectSpace*>(base_space);
      base::MutexGuard guard(large_space->pending_allocation_mutex());
      return addr == large_space->pending_object();
    }

    case SHARED_SPACE:
    case SHARED_LO_SPACE:
    case SHARED_TRUSTED_SPACE:
    case SHARED_TRUSTED_LO_SPACE:
      // TODO(v8:13267): Ensure that all shared space objects have a memory
      // barrier after initialization.
      return false;

    case RO_SPACE:
      UNREACHABLE();
  }

  UNREACHABLE();
}

bool Heap::IsPendingAllocation(Tagged<HeapObject> object) {
  bool result = IsPendingAllocationInternal(object);
  if (v8_flags.trace_pending_allocations && result) {
    StdoutStream{} << "Pending allocation: " << std::hex << "0x" << object.ptr()
                   << "\n";
  }
  return result;
}

bool Heap::IsPendingAllocation(Tagged<Object> object) {
  return IsHeapObject(object) && IsPendingAllocation(Cast<HeapObject>(object));
}

void Heap::ExternalStringTable::AddString(Tagged<String> string) {
  std::optional<base::MutexGuard> guard;

  // With --shared-string-table client isolates may insert into the main
  // isolate's table concurrently.
  if (v8_flags.shared_string_table &&
      heap_->isolate()->is_shared_space_isolate()) {
    guard.emplace(&mutex_);
  }

  DCHECK(IsExternalString(string));
  DCHECK(!Contains(string));

  if (HeapLayout::InYoungGeneration(string)) {
    young_strings_.push_back(string);
  } else {
    old_strings_.push_back(string);
  }
}

Tagged<Boolean> Heap::ToBoolean(bool condition) {
  ReadOnlyRoots roots(this);
  return roots.boolean_value(condition);
}

uint32_t Heap::GetNextTemplateSerialNumber() {
  uint32_t next_serial_number =
      static_cast<uint32_t>(next_template_serial_number().value());
  if (next_serial_number < Smi::kMaxValue) {
    ++next_serial_number;
  } else {
    // In case of overflow, restart from a range where it's ok for serial
    // numbers to be non-unique.
    next_serial_number = TemplateInfo::kFirstNonUniqueSerialNumber;
  }
  DCHECK_NE(next_serial_number, TemplateInfo::kUninitializedSerialNumber);
  set_next_template_serial_number(Smi::FromInt(next_serial_number));
  return next_serial_number;
}

void Heap::IncrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                              size_t amount) {
  base::CheckedIncrement(&backing_store_bytes_, static_cast<uint64_t>(amount),
                         std::memory_order_relaxed);
  // TODO(mlippautz): Implement interrupt for global memory allocations that can
  // trigger garbage collections.
}

void Heap::DecrementExternalBackingStoreBytes(ExternalBackingStoreType type,
                                              size_t amount) {
  base::CheckedDecrement(&backing_store_bytes_, static_cast<uint64_t>(amount),
                         std::memory_order_relaxed);
}

AlwaysAllocateScope::AlwaysAllocateScope(Heap* heap) : heap_(heap) {
  heap_->always_allocate_scope_count_++;
}

AlwaysAllocateScope::~AlwaysAllocateScope() {
  heap_->always_allocate_scope_count_--;
}

AlwaysAllocateScopeForTesting::AlwaysAllocateScopeForTesting(Heap* heap)
    : scope_(heap) {}

PagedNewSpace* Heap::paged_new_space() const {
  return PagedNewSpace::From(new_space());
}

SemiSpaceNewSpace* Heap::semi_space_new_space() const {
  return SemiSpaceNewSpace::From(new_space());
}

StickySpace* Heap::sticky_space() const {
  DCHECK(v8_flags.sticky_mark_bits);
  return StickySpace::From(old_space());
}

IgnoreLocalGCRequests::IgnoreLocalGCRequests(Heap* heap) : heap_(heap) {
  heap_->ignore_local_gc_requests_depth_++;
}

IgnoreLocalGCRequests::~IgnoreLocalGCRequests() {
  DCHECK_GT(heap_->ignore_local_gc_requests_depth_, 0);
  heap_->ignore_local_gc_requests_depth_--;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_INL_H_
