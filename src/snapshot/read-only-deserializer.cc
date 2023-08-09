// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/read-only-deserializer.h"

#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/logging/counters-scopes.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/read-only-serializer-deserializer.h"
#include "src/snapshot/snapshot-data.h"

namespace v8 {
namespace internal {

class ReadOnlyHeapImageDeserializer final {
 public:
  static void Deserialize(Isolate* isolate, SnapshotByteSource* source) {
    ReadOnlyHeapImageDeserializer{isolate, source}.DeserializeImpl();
  }

 private:
  using Bytecode = ro::Bytecode;

  ReadOnlyHeapImageDeserializer(Isolate* isolate, SnapshotByteSource* source)
      : source_(source), isolate_(isolate) {}

  void DeserializeImpl() {
    while (true) {
      int bytecode_as_int = source_->Get();
      DCHECK_LT(bytecode_as_int, ro::kNumberOfBytecodes);
      switch (static_cast<Bytecode>(bytecode_as_int)) {
        case Bytecode::kPage:
          DeserializeReadOnlyPage();
          break;
        case Bytecode::kSegment:
          DeserializeReadOnlySegment();
          break;
        case Bytecode::kRelocateSegment:
          UNREACHABLE();  // Handled together with kSegment.
        case Bytecode::kFinalizePage:
          ro_space()->FinalizeExternallyInitializedPage();
          break;
        case Bytecode::kReadOnlyRootsTable:
          DeserializeReadOnlyRootsTable();
          break;
        case Bytecode::kFinalizeReadOnlySpace:
          ro_space()->FinalizeExternallyInitializedSpace();
          return;
      }
    }
  }

  void DeserializeReadOnlyPage() {
    if (V8_STATIC_ROOTS_BOOL) {
      uint32_t compressed_page_addr = source_->GetUint32();
      Address pos = isolate_->GetPtrComprCage()->base() + compressed_page_addr;
      ro_space()->AllocateNextPageAt(pos);
    } else {
      ro_space()->AllocateNextPage();
    }
  }

  void DeserializeReadOnlySegment() {
    ReadOnlyPage* cur_page = ro_space()->pages().back();

    // Copy over raw contents.
    Address start = cur_page->area_start() + source_->GetUint30();
    int size_in_bytes = source_->GetUint30();
    CHECK_LE(start + size_in_bytes, cur_page->area_end());
    source_->CopyRaw(reinterpret_cast<void*>(start), size_in_bytes);
    ro_space()->top_ = start + size_in_bytes;

    if (!V8_STATIC_ROOTS_BOOL) {
      uint8_t relocate_marker_bytecode = source_->Get();
      CHECK_EQ(relocate_marker_bytecode, Bytecode::kRelocateSegment);
      int tagged_slots_size_in_bits = size_in_bytes / kTaggedSize;
      // The const_cast is unfortunate, but we promise not to mutate data.
      uint8_t* data =
          const_cast<uint8_t*>(source_->data() + source_->position());
      ro::BitSet tagged_slots(data, tagged_slots_size_in_bits);
      DecodeTaggedSlots(start, tagged_slots);
      source_->Advance(static_cast<int>(tagged_slots.size_in_bytes()));
    }
  }

  Address Decode(ro::EncodedTagged encoded) const {
    DCHECK_LT(encoded.page_index, static_cast<int>(ro_space()->pages().size()));
    ReadOnlyPage* page = ro_space()->pages()[encoded.page_index];
    return page->OffsetToAddress(encoded.offset * kTaggedSize);
  }

  void DecodeTaggedSlots(Address segment_start,
                         const ro::BitSet& tagged_slots) {
    DCHECK(!V8_STATIC_ROOTS_BOOL);
    for (size_t i = 0; i < tagged_slots.size_in_bits(); i++) {
      // TODO(jgruber): Depending on sparseness, different iteration methods
      // could be more efficient.
      if (!tagged_slots.contains(static_cast<int>(i))) continue;
      Address slot_addr = segment_start + i * kTaggedSize;
      Address obj_addr = Decode(ro::EncodedTagged::FromAddress(slot_addr));
      Address obj_ptr = obj_addr + kHeapObjectTag;

      Tagged_t* dst = reinterpret_cast<Tagged_t*>(slot_addr);
      *dst = COMPRESS_POINTERS_BOOL
                 ? V8HeapCompressionScheme::CompressObject(obj_ptr)
                 : static_cast<Tagged_t>(obj_ptr);
    }
  }

  void DeserializeReadOnlyRootsTable() {
    ReadOnlyRoots roots(isolate_);
    if (V8_STATIC_ROOTS_BOOL) {
      roots.InitFromStaticRootsTable(isolate_->cage_base());
    } else {
      for (size_t i = 0; i < ReadOnlyRoots::kEntriesCount; i++) {
        uint32_t encoded_as_int = source_->GetUint32();
        Address rudolf = Decode(ro::EncodedTagged::FromUint32(encoded_as_int));
        roots.read_only_roots_[i] = rudolf + kHeapObjectTag;
      }
    }
  }

  ReadOnlySpace* ro_space() const {
    return isolate_->read_only_heap()->read_only_space();
  }

  SnapshotByteSource* const source_;
  Isolate* const isolate_;
};

ReadOnlyDeserializer::ReadOnlyDeserializer(Isolate* isolate,
                                           const SnapshotData* data,
                                           bool can_rehash)
    : Deserializer(isolate, data->Payload(), data->GetMagicNumber(), false,
                   can_rehash) {}

void ReadOnlyDeserializer::DeserializeIntoIsolate() {
  NestedTimedHistogramScope histogram_timer(
      isolate()->counters()->snapshot_deserialize_rospace());
  HandleScope scope(isolate());
  ReadOnlyHeap* ro_heap = isolate()->read_only_heap();

  ReadOnlyHeapImageDeserializer::Deserialize(isolate(), source());
  ro_heap->read_only_space()->RepairFreeSpacesAfterDeserialization();
  PostProcessNewObjects();

  ReadOnlyRoots roots(isolate());
  roots.VerifyNameForProtectorsPages();
#ifdef DEBUG
  roots.VerifyNameForProtectors();
#endif

  if (should_rehash()) {
    isolate()->heap()->InitializeHashSeed();
    Rehash();
  }
}

void NoExternalReferencesCallback() {
  // The following check will trigger if a function or object template with
  // references to native functions have been deserialized from snapshot, but
  // no actual external references were provided when the isolate was created.
  FATAL("No external references provided via API");
}

class ObjectPostProcessor final {
 public:
  explicit ObjectPostProcessor(Isolate* isolate) : isolate_(isolate) {}

#define POST_PROCESS_TYPE_LIST(V) \
  V(AccessorInfo)                 \
  V(CallHandlerInfo)              \
  V(Code)                         \
  V(SharedFunctionInfo)

  void PostProcessIfNeeded(HeapObject o) {
    const InstanceType itype = o.map(isolate_).instance_type();
#define V(TYPE)                               \
  if (InstanceTypeChecker::Is##TYPE(itype)) { \
    return PostProcess##TYPE(TYPE::cast(o));  \
  }
    POST_PROCESS_TYPE_LIST(V)
#undef V
    // If we reach here, no postprocessing is needed for this object.
  }
#undef POST_PROCESS_TYPE_LIST

 private:
  void DecodeExternalPointerSlot(ExternalPointerSlot slot,
                                 ExternalPointerTag tag) {
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    auto encoded = ro::EncodedExternalReference::FromUint32(
        slot.GetContentAsIndexAfterDeserialization(no_gc));
    if (encoded.is_api_reference) {
      Address address =
          isolate_->api_external_references() == nullptr
              ? reinterpret_cast<Address>(NoExternalReferencesCallback)
              : static_cast<Address>(
                    isolate_->api_external_references()[encoded.index]);
      DCHECK_NE(address, kNullAddress);
      slot.init(isolate_, address, tag);
    } else {
      Address address =
          isolate_->external_reference_table_unsafe()->address(encoded.index);
      // Note we allow `address` to be kNullAddress since some of our tests
      // rely on this (e.g. when testing an incompletely initialized ER table).
      slot.init(isolate_, address, tag);
    }
  }
  void PostProcessAccessorInfo(AccessorInfo o) {
    DecodeExternalPointerSlot(
        o.RawExternalPointerField(AccessorInfo::kSetterOffset),
        kAccessorInfoSetterTag);
    DecodeExternalPointerSlot(
        o.RawExternalPointerField(AccessorInfo::kMaybeRedirectedGetterOffset),
        kAccessorInfoGetterTag);
    if (USE_SIMULATOR_BOOL) o.init_getter_redirection(isolate_);
  }
  void PostProcessCallHandlerInfo(CallHandlerInfo o) {
    DecodeExternalPointerSlot(
        o.RawExternalPointerField(
            CallHandlerInfo::kMaybeRedirectedCallbackOffset),
        kCallHandlerInfoCallbackTag);
    if (USE_SIMULATOR_BOOL) o.init_callback_redirection(isolate_);
  }
  void PostProcessCode(Code o) {
    o->init_instruction_start(isolate_, kNullAddress);
    // RO space only contains builtin Code objects which don't have an
    // attached InstructionStream.
    DCHECK(o->is_builtin());
    DCHECK(!o->has_instruction_stream());
    o->SetInstructionStartForOffHeapBuiltin(
        isolate_,
        EmbeddedData::FromBlob(isolate_).InstructionStartOf(o->builtin_id()));
  }
  void PostProcessSharedFunctionInfo(SharedFunctionInfo o) {
    // Reset the id to avoid collisions - it must be unique in this isolate.
    o.set_unique_id(isolate_->GetAndIncNextUniqueSfiId());
  }

  Isolate* const isolate_;
};

void ReadOnlyDeserializer::PostProcessNewObjects() {
  // Since we are not deserializing individual objects we need to scan the
  // heap and search for objects that need post-processing.
  //
  // See also Deserializer<IsolateT>::PostProcessNewObject.
  PtrComprCageBase cage_base(isolate());
  ObjectPostProcessor post_processor(isolate());
  ReadOnlyHeapObjectIterator it(isolate()->read_only_heap());
  for (HeapObject o = it.Next(); !o.is_null(); o = it.Next()) {
    if (should_rehash()) {
      const InstanceType instance_type = o->map(cage_base)->instance_type();
      if (InstanceTypeChecker::IsString(instance_type)) {
        String str = String::cast(o);
        str->set_raw_hash_field(Name::kEmptyHashField);
        PushObjectToRehash(handle(str, isolate()));
      } else if (o->NeedsRehashing(instance_type)) {
        PushObjectToRehash(handle(o, isolate()));
      }
    }

    post_processor.PostProcessIfNeeded(o);
  }
}

}  // namespace internal
}  // namespace v8
