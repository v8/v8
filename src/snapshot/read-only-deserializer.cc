// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/read-only-deserializer.h"

#include <limits>

#include "src/handles/handles-inl.h"
#include "src/heap/heap-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/logging/counters-scopes.h"
#include "src/numbers/hash-seed.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/objects/string-table.h"
#include "src/snapshot/embedded/embedded-data-inl.h"
#include "src/snapshot/read-only-serializer-deserializer.h"
#include "src/snapshot/snapshot-data.h"

namespace v8 {
namespace internal {

class ReadOnlyHeapImageDeserializer final {
 public:
  static void Deserialize(Isolate* isolate, SnapshotByteSource* source,
                          std::vector<PostProcessRange>* ranges) {
    ReadOnlyHeapImageDeserializer{isolate, source, ranges}.DeserializeImpl();
  }

 private:
  using Bytecode = ro::Bytecode;

  ReadOnlyHeapImageDeserializer(Isolate* isolate, SnapshotByteSource* source,
                                std::vector<PostProcessRange>* ranges)
      : source_(source), isolate_(isolate), post_process_ranges_(ranges) {}

  void DeserializeImpl() {
    while (true) {
      int bytecode_as_int = source_->Get();
      DCHECK_LT(bytecode_as_int, ro::kNumberOfBytecodes);
      switch (static_cast<Bytecode>(bytecode_as_int)) {
        case Bytecode::kAllocatePage:
          AllocatePage(false);
          break;
        case Bytecode::kAllocatePageAt:
          AllocatePage(true);
          break;
        case Bytecode::kSegment:
          DeserializeSegment();
          break;
        case Bytecode::kRelocateSegment:
          UNREACHABLE();  // Handled together with kSegment.
        case Bytecode::kReadOnlyRootsTable:
          DeserializeReadOnlyRootsTable();
          break;
        case Bytecode::kPostProcessRange:
          ReadPostProcessRange();
          break;
        case Bytecode::kRoSpaceImage:
          DeserializeRoSpaceImage();
          break;
        case Bytecode::kFinalizeReadOnlySpace:
          ro_space()->FinalizeSpaceForDeserialization(source_->GetUint30());
          // The RO bytecode stream is self-terminating at
          // kFinalizeReadOnlySpace. Skip to the end so the base
          // Deserializer destructor's padding check is satisfied.
          // (In the blob path, alignment padding and the blob image
          // follow the bytecodes; in the legacy path, Pad() nops do.)
          source_->set_position(source_->length());
          return;
      }
    }
  }

  void AllocatePage(bool fixed_offset) {
    CHECK_EQ(V8_STATIC_ROOTS_BOOL, fixed_offset);
    size_t expected_page_index = static_cast<size_t>(source_->GetUint30());
    size_t actual_page_index = static_cast<size_t>(-1);
    size_t area_size_in_bytes = static_cast<size_t>(source_->GetUint30());
    if (fixed_offset) {
#ifdef V8_COMPRESS_POINTERS
      uint32_t compressed_page_addr = source_->GetUint32();
      Address pos = isolate_->cage_base() + compressed_page_addr;
      actual_page_index = ro_space()->AllocateNextPageAt(pos);
#else
      UNREACHABLE();
#endif  // V8_COMPRESS_POINTERS
    } else {
      actual_page_index = ro_space()->AllocateNextPage();
    }
    CHECK_EQ(actual_page_index, expected_page_index);
    ro_space()->InitializePageForDeserialization(PageAt(actual_page_index),
                                                 area_size_in_bytes);
  }

  void DeserializeSegment() {
    uint32_t page_index = source_->GetUint30();
    ReadOnlyPage* page = PageAt(page_index);

    // Copy over raw contents.
    Address start = page->area_start() + source_->GetUint30();
    int size_in_bytes = source_->GetUint30();
    CHECK_LE(start + size_in_bytes, page->area_end());
    source_->CopyRaw(reinterpret_cast<void*>(start), size_in_bytes);

    if (!V8_STATIC_ROOTS_BOOL) {
      uint8_t relocate_marker_bytecode = source_->Get();
      CHECK_EQ(relocate_marker_bytecode, Bytecode::kRelocateSegment);
      int tagged_slots_size_in_bits = size_in_bytes / kTaggedSize;
      // The const_cast is unfortunate, but we promise not to mutate data.
      uint8_t* data =
          const_cast<uint8_t*>(source_->data() + source_->position());
      ro::BitSet tagged_slots(data, tagged_slots_size_in_bits);
      DecodeTaggedSlots(start, tagged_slots);
      CHECK_LE(tagged_slots.size_in_bytes(), std::numeric_limits<int>::max());
      source_->Advance(static_cast<int>(tagged_slots.size_in_bytes()));
    }
  }

  Address Decode(ro::EncodedTagged encoded) const {
    ReadOnlyPage* page = PageAt(encoded.page_index);
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

  ReadOnlyPage* PageAt(size_t index) const {
    DCHECK_LT(index, ro_space()->pages().size());
    return ro_space()->pages()[index];
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

  void ReadPostProcessRange() {
    PostProcessRange range;
    range.page_index = source_->GetUint30();
    range.first_offset = source_->GetUint30();
    range.end_offset = source_->GetUint30();
    post_process_ranges_->push_back(range);
  }

  void DeserializeRoSpaceImage() {
    uint32_t blob_offset_from_end = source_->GetUint32();

    // The blob is at the end of the payload.
    CHECK_LE(blob_offset_from_end, source_->length());
    const uint8_t* blob_start =
        source_->data() + source_->length() - blob_offset_from_end;
    uint32_t blob_size = blob_offset_from_end;

    // When the snapshot blob is compiled into the binary with proper alignment
    // (alignas(kTargetMinimumOSPageSize) on blob_data[] in the generated
    // snapshot .cc), the RO space image blob will be page-aligned here. This
    // enables future mmap-based deserialization. The alignment won't hold when
    // the snapshot is loaded from an external file or when snapshot compression
    // is enabled.
#if !defined(V8_USE_EXTERNAL_STARTUP_DATA) && !defined(V8_SNAPSHOT_COMPRESSION)
    CHECK(
        IsAligned(reinterpret_cast<uintptr_t>(blob_start), kMinimumOSPageSize));
#endif

    const auto& pages = ro_space()->pages();
    DCHECK(!pages.empty());

    // First page is at cage base.
    Address cage_base = pages.front()->ChunkAddress();

    // Copy entire pages from the blob, including headers. The blob has
    // pre-populated MemoryChunk headers with the correct flags and metadata
    // index.
    for (const ReadOnlyPage* page : pages) {
      size_t page_offset_in_blob = page->ChunkAddress() - cage_base;
      size_t page_used = page->HighWaterMark() - page->ChunkAddress();

      CHECK_LE(page_offset_in_blob + page_used, blob_size);

      memcpy(reinterpret_cast<void*>(page->ChunkAddress()),
             blob_start + page_offset_in_blob, page_used);
    }
  }

  ReadOnlySpace* ro_space() const {
    return isolate_->read_only_heap()->read_only_space();
  }

  SnapshotByteSource* const source_;
  Isolate* const isolate_;
  std::vector<PostProcessRange>* const post_process_ranges_;
};

ReadOnlyDeserializer::ReadOnlyDeserializer(Isolate* isolate,
                                           const SnapshotData* data,
                                           bool can_rehash)
    : Deserializer(isolate, data->Payload(), data->GetMagicNumber(), false,
                   can_rehash) {}

void ReadOnlyDeserializer::DeserializeIntoIsolate() {
  base::ElapsedTimer timer;
  if (V8_UNLIKELY(v8_flags.profile_deserialization)) timer.Start();
  NestedTimedHistogramScope histogram_timer(
      isolate()->counters()->snapshot_deserialize_rospace());
  HandleScope scope(isolate());

  std::vector<PostProcessRange> post_process_ranges;
  ReadOnlyHeapImageDeserializer::Deserialize(isolate(), source(),
                                             &post_process_ranges);

  if (should_rehash()) {
    // Initialize hash seed before PostProcessNewObjects, since it computes
    // string hashes.
    HashSeed::InitializeRoots(isolate());
  }

  PostProcessNewObjects(post_process_ranges);

  ReadOnlyRoots roots(isolate());
  roots.VerifyNameForProtectorsPages();
#ifdef DEBUG
  roots.VerifyTypes();
  roots.VerifyNameForProtectors();
#endif

  if (should_rehash()) {
    Rehash();
  }

  if (V8_UNLIKELY(v8_flags.profile_deserialization)) {
    // ATTENTION: The Memory.json benchmark greps for this exact output. Do not
    // change it without also updating Memory.json.
    const size_t bytes = source()->length();
    const double ms = timer.Elapsed().InMillisecondsF();
    PrintF("[Deserializing read-only space (%zu bytes) took %0.3f ms]\n", bytes,
           ms);
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
  explicit ObjectPostProcessor(Isolate* isolate)
      : isolate_(isolate), embedded_data_(EmbeddedData::FromBlob(isolate_)) {}

  void Finalize() {
#ifdef V8_ENABLE_SANDBOX
    std::vector<ReadOnlyArtifacts::ExternalPointerRegistryEntry> registry;
    registry.reserve(external_pointer_slots_.size());
    for (auto& slot : external_pointer_slots_) {
      registry.emplace_back(slot.Relaxed_LoadHandle(), slot.load(isolate_),
                            slot.exact_tag());
    }

    isolate_->read_only_artifacts()->set_external_pointer_registry(
        std::move(registry));
#endif  // V8_ENABLE_SANDBOX
  }
  V8_INLINE void PostProcessIfNeeded(Tagged<HeapObject> o,
                                     InstanceType instance_type) {
    DCHECK_EQ(o->map()->instance_type(), instance_type);
#define V(TYPE)                                       \
  if (InstanceTypeChecker::Is##TYPE(instance_type)) { \
    PostProcess##TYPE(TrustedCast<TYPE>(o));          \
    return;                                           \
  }
    RO_POST_PROCESS_TYPE_LIST(V)
#undef V
    // If we reach here, no postprocessing is needed for this object.
  }

 private:
  Address GetAnyExternalReferenceAt(int index, bool is_api_reference) const {
    if (is_api_reference) {
      const intptr_t* refs = isolate_->api_external_references();
      Address address =
          refs == nullptr
              ? reinterpret_cast<Address>(NoExternalReferencesCallback)
              : static_cast<Address>(refs[index]);
      DCHECK_NE(address, kNullAddress);
      return address;
    }
    // Note we allow `address` to be kNullAddress since some of our tests
    // rely on this (e.g. when testing an incompletely initialized ER table).
    return isolate_->external_reference_table_unsafe()->address(index);
  }

  void DecodeExternalPointerSlot(Tagged<HeapObject> host,
                                 ExternalPointerSlot slot) {
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    auto encoded = ro::EncodedExternalReference::FromUint32(
        slot.GetContentAsIndexAfterDeserialization(no_gc));
    Address slot_value =
        GetAnyExternalReferenceAt(encoded.index, encoded.is_api_reference);
    ExternalPointerTag tag = static_cast<ExternalPointerTag>(encoded.tag);
    slot.init(isolate_, host, slot_value, tag);
#ifdef V8_ENABLE_SANDBOX
    // Register these slots during deserialization s.t. later isolates (which
    // share the RO space we are currently deserializing) can properly
    // initialize their external pointer table RO space. Note that slot values
    // are only fully finalized at the end of deserialization, thus we only
    // register the slot itself now and read the handle/value in Finalize.
    //
    // We have to create a new ExternalPointerSlot here because the incoming
    // ExternalPointerSlot has a tag range, but here we need an exact tag.
    external_pointer_slots_.emplace_back(
        ExternalPointerSlot(slot.address(), tag));
#endif  // V8_ENABLE_SANDBOX
  }
  void DecodeLazilyInitializedExternalPointerSlot(Tagged<HeapObject> host,
                                                  ExternalPointerSlot slot) {
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    auto encoded = ro::EncodedExternalReference::FromUint32(
        slot.GetContentAsIndexAfterDeserialization(no_gc));
    Address slot_value =
        GetAnyExternalReferenceAt(encoded.index, encoded.is_api_reference);
    if (slot_value == kNullAddress) {
      slot.init_lazily_initialized();
    } else {
      ExternalPointerTag tag = static_cast<ExternalPointerTag>(encoded.tag);
      slot.init(isolate_, host, slot_value, tag);
#ifdef V8_ENABLE_SANDBOX
      // Register these slots during deserialization s.t. later isolates (which
      // share the RO space we are currently deserializing) can properly
      // initialize their external pointer table RO space. Note that slot values
      // are only fully finalized at the end of deserialization, thus we only
      // register the slot itself now and read the handle/value in Finalize.
      //
      // We have to create a new ExternalPointerSlot here because the incoming
      // ExternalPointerSlot has a tag range, but here we need an exact tag.
      external_pointer_slots_.emplace_back(
          ExternalPointerSlot(slot.address(), tag));
#endif  // V8_ENABLE_SANDBOX
    }
  }
  void PostProcessAccessorInfo(Tagged<AccessorInfo> o) {
    DecodeExternalPointerSlot(
        o, o->RawExternalPointerField(offsetof(AccessorInfo, setter_),
                                      kAccessorInfoSetterTag));
    DecodeExternalPointerSlot(
        o, o->RawExternalPointerField(offsetof(AccessorInfo, getter_),
                                      kAccessorInfoGetterTag));
    if (USE_SIMULATOR_BOOL) {
      o->RestoreCallbackRedirectionAfterDeserialization(isolate_);
    }
  }
  void PostProcessInterceptorInfo(Tagged<InterceptorInfo> o) {
    const bool is_named = o->is_named();

#define PROCESS_NAMED_FIELD(Name, name)                                 \
  DecodeLazilyInitializedExternalPointerSlot(                           \
      o, o->RawExternalPointerField(offsetof(InterceptorInfo, name##_), \
                                    kApiNamedProperty##Name##CallbackTag));

#define PROCESS_INDEXED_FIELD(Name, name)                               \
  DecodeLazilyInitializedExternalPointerSlot(                           \
      o, o->RawExternalPointerField(offsetof(InterceptorInfo, name##_), \
                                    kApiIndexedProperty##Name##CallbackTag));

    if (is_named) {
      NAMED_INTERCEPTOR_INFO_CALLBACK_LIST(PROCESS_NAMED_FIELD)
    } else {
      INDEXED_INTERCEPTOR_INFO_CALLBACK_LIST(PROCESS_INDEXED_FIELD)
    }
#undef PROCESS_NAMED_FIELD
#undef PROCESS_INDEXED_FIELD

    if (USE_SIMULATOR_BOOL) {
      o->RestoreCallbackRedirectionAfterDeserialization(isolate_);
    }
  }
  void PostProcessJSExternalObject(Tagged<JSExternalObject> o) {
    DecodeExternalPointerSlot(
        o, ExternalPointerSlot(&o->value_, kExternalObjectValueTagRange));
  }
  void PostProcessFunctionTemplateInfo(Tagged<FunctionTemplateInfo> o) {
    DecodeExternalPointerSlot(o, ExternalPointerSlot(&o->callback_));
    if (USE_SIMULATOR_BOOL) {
      o->RestoreCallbackRedirectionAfterDeserialization(isolate_);
    }
  }

#if V8_ENABLE_GEARBOX
  V8_INLINE void UpdateGearboxPlaceholderBuiltin(Tagged<Code> code) {
    // When gearbox is enabled, placeholder builtins dispatch to a variant
    // (either generic or ISX) based on the client's CPU supported ISA
    // (instruction set architecture) feature.
    if (code->is_gearbox_placeholder_builtin()) {
      Tagged<Code> src = code;
      Builtin generic_id = potential_generic_code_->builtin_id();
      Builtin ISX_id = potential_ISX_code_->builtin_id();
      DCHECK(Builtins::IsGenericVariant(generic_id));
      DCHECK(Builtins::IsISXVariant(ISX_id));
      USE(generic_id);
      USE(ISX_id);
      // Check for the two code object's builtin id were adjacent.
      DCHECK_EQ(++generic_id, ISX_id);
      if (Builtins::CpuHasISXSupport()) {
        src = potential_ISX_code_;
      } else {
        src = potential_generic_code_;
      }
      Code::CopyFieldsWithGearboxForDeserialization(code, src, isolate_);
    }
    potential_generic_code_ = potential_ISX_code_;
    potential_ISX_code_ = code;
  }
#endif

  void PostProcessCode(Tagged<Code> o) {
    o->InitAndPublish(isolate_);
    o->wrapper()->set_code(o);
    // RO space only contains builtin Code objects which don't have an
    // attached InstructionStream.
    DCHECK(o->is_builtin());
    DCHECK(!o->has_instruction_stream());
    Builtin builtin = o->builtin_id();
    // Mark disabled builtins as such (RO space serializer resets this flag).
    DCHECK(!o->is_disabled_builtin());
    if (Builtins::IsDisabled(builtin)) {
      o->set_is_disabled_builtin(true);
    }
    o->SetInstructionStartForOffHeapBuiltin(
        isolate_, EmbeddedData::FromBlob(isolate_).InstructionStartOf(builtin));

#if V8_ENABLE_GEARBOX
    UpdateGearboxPlaceholderBuiltin(o);
#endif
  }

  Isolate* const isolate_;
  const EmbeddedData embedded_data_;

#if V8_ENABLE_GEARBOX
  // We have to store preceding and second preceding Code object here, because
  // they are not registered in isolate, we couldn't find them through isolate
  // yet.
  // We use them for maintain the potential generic and ISX code object.
  Tagged<Code> potential_generic_code_;
  Tagged<Code> potential_ISX_code_;
#endif

#ifdef V8_ENABLE_SANDBOX
  std::vector<ExternalPointerSlot> external_pointer_slots_;
#endif  // V8_ENABLE_SANDBOX
};

void ReadOnlyDeserializer::PostProcessNewObjects(
    const std::vector<PostProcessRange>& ranges) {
  // See also Deserializer<IsolateT>::PostProcessNewObject.
#ifdef V8_COMPRESS_POINTERS
  ExternalPointerTable::UnsealReadOnlySegmentScope unseal_scope(
      &isolate()->external_pointer_table());
#endif  // V8_COMPRESS_POINTERS
#ifdef V8_ENABLE_SANDBOX
  TrustedPointerTable::UnsealReadOnlySegmentScope trusted_unseal_scope(
      &isolate()->trusted_pointer_table());
#endif  // V8_ENABLE_SANDBOX
  ObjectPostProcessor post_processor(isolate());

  // Without pointer compression, we need to iterate all RO objects to find
  // InternalizedStrings for the string table. With pointer compression, we
  // only need to iterate all objects when rehashing.
#ifdef V8_COMPRESS_POINTERS
  constexpr bool kPopulateStringTable = false;
#else
  constexpr bool kPopulateStringTable = true;
#endif

  StringTable* string_table =
      kPopulateStringTable && isolate()->OwnsStringTables()
          ? isolate()->string_table()
          : nullptr;

  if (kPopulateStringTable || should_rehash()) {
    // Full iteration over all RO heap objects.
    ReadOnlyHeapObjectIterator it(isolate()->read_only_heap());
    for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
      const InstanceType instance_type = o->map()->instance_type();
      if (InstanceTypeChecker::IsString(instance_type)) {
        // All strings in RO space are internalized.
        DCHECK(InstanceTypeChecker::IsInternalizedString(instance_type));
        Tagged<String> str = Cast<String>(o);
        if (should_rehash()) {
          str->set_raw_hash_field(Name::kEmptyHashField);
          str->EnsureHash();
        }
        if (string_table != nullptr) {
          string_table->InsertForReadOnlyDeserialization(
              isolate(), Cast<InternalizedString>(o));
        }
      } else if (should_rehash() && o->NeedsRehashing(instance_type)) {
        PushObjectToRehash(direct_handle(o, isolate()));
      }
      post_processor.PostProcessIfNeeded(o, instance_type);
    }
  } else {
    // Compressed pointers + no rehash: only iterate post-process ranges.
    USE(string_table);
#ifdef V8_COMPRESS_POINTERS
    ReadOnlySpace* ro_space = isolate()->read_only_heap()->read_only_space();
    for (const PostProcessRange& range : ranges) {
      const ReadOnlyPage* page = ro_space->pages()[range.page_index];
      Address start = page->area_start() + range.first_offset;
      Address end = page->area_start() + range.end_offset;
      ReadOnlyPageObjectIterator it(page, start);
      for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
        if (o.address() >= end) break;
        const InstanceType instance_type = o->map()->instance_type();
        post_processor.PostProcessIfNeeded(o, instance_type);
      }
    }
#else
    USE(ranges);
    UNREACHABLE();
#endif
  }

  post_processor.Finalize();
}

}  // namespace internal
}  // namespace v8
