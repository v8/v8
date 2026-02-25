// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/read-only-serializer.h"

#include "src/common/globals.h"
#include "src/heap/heap-inl.h"
#include "src/heap/memory-chunk-constants.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/visit-object.h"
#include "src/objects/free-space-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots.h"
#include "src/snapshot/read-only-serializer-deserializer.h"

namespace v8 {
namespace internal {

namespace {

// Preprocess an object to prepare it for serialization.
class ObjectPreProcessor final {
 public:
  explicit ObjectPreProcessor(Isolate* isolate)
      : isolate_(isolate), extref_encoder_(isolate) {}

#define PRE_PROCESS_TYPE_LIST(V) \
  V(AccessorInfo)                \
  V(InterceptorInfo)             \
  V(JSExternalObject)            \
  V(FunctionTemplateInfo)        \
  V(Code)

  void PreProcessIfNeeded(Tagged<HeapObject> o) {
    const InstanceType itype = o->map()->instance_type();
#define V(TYPE)                                    \
  if (InstanceTypeChecker::Is##TYPE(itype)) {      \
    return PreProcess##TYPE(TrustedCast<TYPE>(o)); \
  }
    PRE_PROCESS_TYPE_LIST(V)
#undef V
    // If we reach here, no preprocessing is needed for this object.
  }
#undef PRE_PROCESS_TYPE_LIST

 private:
  void EncodeExternalPointerSlot(ExternalPointerSlot slot) {
    Address value = slot.load(isolate_);
    EncodeExternalPointerSlot(slot, value);
  }

  void EncodeExternalPointerSlot(ExternalPointerSlot slot, Address value) {
    // Note it's possible that `value != slot.load(...)`, e.g. after
    // AccessorInfo::RemoveCallbackRedirectionForSerialization() and other
    // similar functions.
    ExternalReferenceEncoder::Value encoder_value =
        extref_encoder_.Encode(value);
    DCHECK_LT(encoder_value.index(),
              1UL << ro::EncodedExternalReference::kIndexBits);
    DCHECK(slot.ExactTagIsKnown());
    ro::EncodedExternalReference encoded(
        slot.exact_tag(), encoder_value.is_from_api(), encoder_value.index());
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    slot.ReplaceContentWithIndexForSerialization(no_gc, encoded.ToUint32());
  }

  void EncodeExternalPointerSlotWithTagRange(ExternalPointerSlot slot) {
    Address value = slot.load(isolate_);
    ExternalPointerTag tag = slot.load_tag(isolate_);

    ExternalReferenceEncoder::Value encoder_value =
        extref_encoder_.Encode(value);

    DCHECK_LT(encoder_value.index(),
              1UL << ro::EncodedExternalReference::kIndexBits);

    ro::EncodedExternalReference encoded(tag, encoder_value.is_from_api(),
                                         encoder_value.index());

    DisallowGarbageCollection no_gc;
    slot.ReplaceContentWithIndexForSerialization(no_gc, encoded.ToUint32());
  }

  void PreProcessAccessorInfo(Tagged<AccessorInfo> o) {
    EncodeExternalPointerSlot(
        o->RawExternalPointerField(offsetof(AccessorInfo, getter_),
                                   kAccessorInfoGetterTag),
        o->getter(isolate_));  // Pass the non-redirected value.
    EncodeExternalPointerSlot(o->RawExternalPointerField(
        offsetof(AccessorInfo, setter_), kAccessorInfoSetterTag));
  }
  void PreProcessInterceptorInfo(Tagged<InterceptorInfo> o) {
    const bool is_named = o->is_named();

#define PROCESS_NAMED_FIELD(Name, name)                                 \
  EncodeExternalPointerSlot(                                            \
      o->RawExternalPointerField(offsetof(InterceptorInfo, name##_),    \
                                 kApiNamedProperty##Name##CallbackTag), \
      o->named_##name(isolate_) /* non-redirected */);

#define PROCESS_INDEXED_FIELD(Name, name)                                 \
  EncodeExternalPointerSlot(                                              \
      o->RawExternalPointerField(offsetof(InterceptorInfo, name##_),      \
                                 kApiIndexedProperty##Name##CallbackTag), \
      o->indexed_##name(isolate_) /* non-redirected */);

    if (is_named) {
      NAMED_INTERCEPTOR_INFO_CALLBACK_LIST(PROCESS_NAMED_FIELD)
    } else {
      INDEXED_INTERCEPTOR_INFO_CALLBACK_LIST(PROCESS_INDEXED_FIELD)
    }
#undef PROCESS_NAMED_FIELD
#undef PROCESS_INDEXED_FIELD
  }
  void PreProcessJSExternalObject(Tagged<JSExternalObject> o) {
    ExternalPointerSlot value_slot(&o->value_, kExternalObjectValueTagRange);
    EncodeExternalPointerSlotWithTagRange(value_slot);
  }
  void PreProcessFunctionTemplateInfo(Tagged<FunctionTemplateInfo> o) {
    EncodeExternalPointerSlot(
        ExternalPointerSlot(&o->callback_),
        o->callback(isolate_));  // Pass the non-redirected value.
  }
#if V8_ENABLE_GEARBOX
  V8_INLINE void ResetGearboxPlaceholderBuiltin(Tagged<Code> code) {
    // In order to ensure predictable state of placeholder builtins Code
    // objects after serialization we replace their fields with the contents
    // of kIllegal builtin.
    if (code->is_gearbox_placeholder_builtin()) {
      Builtin variant_builtin_id = code->builtin_id();
      Builtin placeholder_builtin_id =
          Builtins::GetGearboxPlaceholderFromVariant(variant_builtin_id);
      DCHECK_EQ(
          isolate_->builtins()->code(placeholder_builtin_id)->builtin_id(),
          code->builtin_id());
      Tagged<Code> src = isolate_->builtins()->code(Builtin::kIllegal);
      Code::CopyFieldsWithGearboxForSerialization(code, src, isolate_);
      // We should use the placeholder id instead of kIllegal.
      code->set_builtin_id(placeholder_builtin_id);
    }
  }
#endif
  void PreProcessCode(Tagged<Code> o) {
    // Clear disabled builtin flag to make snapshot state predictable.
    if (o->is_builtin()) {
      o->set_is_disabled_builtin(false);
      // Builtins might have source position tables generated during compilation
      // (e.g. RecordWriteSaveFP). Clear them for the snapshot as they are not
      // needed and read-only space expects no source positions.
      o->clear_source_position_table_and_bytecode_offset_table();
    }
    o->ClearInstructionStartForSerialization(isolate_);
    CHECK(!o->has_source_position_table_or_bytecode_offset_table());
    CHECK(!o->has_deoptimization_data_or_interpreter_data());
    CHECK_EQ(o->js_dispatch_handle(), kNullJSDispatchHandle);
#if V8_ENABLE_GEARBOX
    ResetGearboxPlaceholderBuiltin(o);
#endif
  }

  Isolate* const isolate_;
  ExternalReferenceEncoder extref_encoder_;
};

// Returns true if the object will need post-processing during deserialization.
bool NeedsPostProcessing(Isolate* isolate, Tagged<HeapObject> o) {
  const InstanceType itype = o->map()->instance_type();
#define V(TYPE) \
  if (InstanceTypeChecker::Is##TYPE(itype)) return true;
  RO_POST_PROCESS_TYPE_LIST(V)
#undef V
  return false;
}

struct ReadOnlySegmentForSerialization {
  ReadOnlySegmentForSerialization(Isolate* isolate, const ReadOnlyPage* page,
                                  Address segment_start, size_t segment_size,
                                  ObjectPreProcessor* pre_processor)
      : page(page),
        segment_start(segment_start),
        segment_size(segment_size),
        segment_offset(segment_start - page->area_start()),
        contents(new uint8_t[segment_size]),
        tagged_slots(segment_size / kTaggedSize) {
    // .. because tagged_slots records a bit for each slot:
    DCHECK(IsAligned(segment_size, kTaggedSize));
    // Ensure incoming pointers to this page are representable.
    CHECK_LT(isolate->read_only_heap()->read_only_space()->IndexOf(page),
             1UL << ro::EncodedTagged::kPageIndexBits);

    MemCopy(contents.get(), reinterpret_cast<void*>(segment_start),
            segment_size);
    PreProcessSegment(pre_processor);
    if (!V8_STATIC_ROOTS_BOOL) EncodeTaggedSlots(isolate);
  }

  void PreProcessSegment(ObjectPreProcessor* pre_processor) {
    // Iterate the RO page and the contents copy in lockstep, preprocessing
    // objects as we go along.
    //
    // See also ObjectSerializer::OutputRawData.
    DCHECK_GE(segment_start, page->area_start());
    const Address segment_end = segment_start + segment_size;
    ReadOnlyPageObjectIterator it(page, segment_start);
    for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
      if (o.address() >= segment_end) break;
      size_t o_offset = o.ptr() - segment_start;
      Address o_dst = reinterpret_cast<Address>(contents.get()) + o_offset;
      pre_processor->PreProcessIfNeeded(
          Cast<HeapObject>(Tagged<Object>(o_dst)));
    }
  }

  void EncodeTaggedSlots(Isolate* isolate);

  const ReadOnlyPage* const page;
  const Address segment_start;
  const size_t segment_size;
  const size_t segment_offset;
  // The (mutated) off-heap copy of the on-heap segment.
  std::unique_ptr<uint8_t[]> contents;
  // The relocation table.
  ro::BitSet tagged_slots;

  friend class EncodeRelocationsVisitor;
};

ro::EncodedTagged Encode(Isolate* isolate, Tagged<HeapObject> o) {
  Address o_address = o.address();
  BasePage* chunk = BasePage::FromAddress(isolate, o_address);

  ReadOnlySpace* ro_space = isolate->read_only_heap()->read_only_space();
  int index = static_cast<int>(ro_space->IndexOf(chunk));
  uint32_t offset = static_cast<int>(chunk->Offset(o_address));
  DCHECK(IsAligned(offset, kTaggedSize));

  return ro::EncodedTagged(index, offset / kTaggedSize);
}

// If relocations are needed, this class
// - encodes all tagged slots s.t. valid pointers can be reconstructed during
//   deserialization, and
// - records the location of all tagged slots in a table.
class EncodeRelocationsVisitor final : public ObjectVisitor {
 public:
  EncodeRelocationsVisitor(Isolate* isolate,
                           ReadOnlySegmentForSerialization* segment)
      : isolate_(isolate), segment_(segment) {
    DCHECK(!V8_STATIC_ROOTS_BOOL);
  }

  void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                     ObjectSlot end) override {
    VisitPointers(host, MaybeObjectSlot(start), MaybeObjectSlot(end));
  }

  void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                     MaybeObjectSlot end) override {
    for (MaybeObjectSlot slot = start; slot < end; slot++) {
      ProcessSlot(slot);
    }
  }

  void VisitMapPointer(Tagged<HeapObject> host) override {
    ProcessSlot(host->RawMaybeWeakField(offsetof(HeapObject, map_)));
  }

  // Sanity-checks:
  void VisitInstructionStreamPointer(Tagged<Code> host,
                                     InstructionStreamSlot slot) override {
    // RO space contains only builtin Code objects.
    DCHECK(!host->has_instruction_stream());
  }
  void VisitCodeTarget(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitEmbeddedPointer(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitExternalReference(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitInternalReference(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitOffHeapTarget(Tagged<InstructionStream>, RelocInfo*) override {
    UNREACHABLE();
  }
  void VisitExternalPointer(Tagged<HeapObject>,
                            ExternalPointerSlot slot) override {
    // This slot was encoded in a previous pass, see EncodeExternalPointerSlot.
#ifdef DEBUG
    ExternalPointerTag tag;
    // `slot` can have a tag range, but below we need an exact tag. Therefore we
    // load the actual tag of the slot. However, we do that only if there is
    // actually a value stored in the slot. If not, then the slot is
    // uninitialized, and so far code with tag ranges only handles initialized
    // slots. Therefore we can use the exact tag of the slot.
    if (slot.load(isolate_)) {
      tag = slot.load_tag(isolate_);
    } else {
      DCHECK(slot.ExactTagIsKnown());
      tag = slot.exact_tag();
    }
    ExternalPointerSlot slot_in_segment{
        reinterpret_cast<Address>(segment_->contents.get() +
                                  SegmentOffsetOf(slot)),
        tag};
    // Constructing no_gc here is not the intended use pattern (instead we
    // should pass it along the entire callchain); but there's little point of
    // doing that here - all of the code in this file relies on GC being
    // disabled, and that's guarded at entry points.
    DisallowGarbageCollection no_gc;
    auto encoded = ro::EncodedExternalReference::FromUint32(
        slot_in_segment.GetContentAsIndexAfterDeserialization(no_gc));
    if (encoded.is_api_reference) {
      // Can't validate these since we don't know how many entries
      // api_external_references contains.
    } else {
      CHECK_LT(encoded.index, ExternalReferenceTable::kSize);
    }
#endif  // DEBUG
  }

 private:
  void ProcessSlot(MaybeObjectSlot slot) {
    Tagged<MaybeObject> o = *slot;
    if (!o.IsStrongOrWeak()) return;  // Smis don't need relocation.
    DCHECK(o.IsStrong());

    int slot_offset = SegmentOffsetOf(slot);
    DCHECK(IsAligned(slot_offset, kTaggedSize));

    // Encode:
    ro::EncodedTagged encoded = Encode(isolate_, o.GetHeapObject());
    memcpy(segment_->contents.get() + slot_offset, &encoded,
           ro::EncodedTagged::kSize);

    // Record:
    segment_->tagged_slots.set(AsSlot(slot_offset));
  }

  template <class SlotT>
  int SegmentOffsetOf(SlotT slot) const {
    Address addr = slot.address();
    DCHECK_GE(addr, segment_->segment_start);
    DCHECK_LT(addr, segment_->segment_start + segment_->segment_size);
    return static_cast<int>(addr - segment_->segment_start);
  }

  static constexpr int AsSlot(int byte_offset) {
    return byte_offset / kTaggedSize;
  }

  Isolate* const isolate_;
  ReadOnlySegmentForSerialization* const segment_;
};

void ReadOnlySegmentForSerialization::EncodeTaggedSlots(Isolate* isolate) {
  DCHECK(!V8_STATIC_ROOTS_BOOL);
  EncodeRelocationsVisitor v(isolate, this);

  DCHECK_GE(segment_start, page->area_start());
  const Address segment_end = segment_start + segment_size;
  ReadOnlyPageObjectIterator it(page, segment_start,
                                SkipFreeSpaceOrFiller::kNo);
  for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
    if (o.address() >= segment_end) break;
    VisitObject(isolate, o, &v);
  }
}

class ReadOnlyHeapImageSerializer {
 public:
  struct MemoryRegion {
    Address start;
    size_t size;
  };

  static void Serialize(Isolate* isolate, SnapshotByteSink* sink) {
    ReadOnlyHeapImageSerializer{isolate, sink}.SerializeImpl();
  }

 private:
  using Bytecode = ro::Bytecode;

  ReadOnlyHeapImageSerializer(Isolate* isolate, SnapshotByteSink* sink)
      : isolate_(isolate), sink_(sink), pre_processor_(isolate) {}

  void SerializeImpl() {
    DCHECK_EQ(sink_->Position(), 0);

    ReadOnlySpace* ro_space = isolate_->read_only_heap()->read_only_space();

    // Allocate all pages first s.t. the deserializer can easily handle forward
    // references (e.g.: an object on page i points at an object on page i+1).
    for (const ReadOnlyPage* page : ro_space->pages()) {
      EmitAllocatePage(page);
    }

#if V8_STATIC_ROOTS_BOOL && V8_ENABLE_SANDBOX
    // With static roots + sandbox we emit the entire RO space as a single
    // contiguous image blob at the end of the payload, instead of
    // per-segment inline data. The blob contains full pages (including
    // pre-populated MemoryChunk headers), with unmapped bodies (holes,
    // WasmNull, filler) zeroed. This prepares for future mmap-based
    // deserialization.
    // The sandbox is required because without it the MemoryChunk header
    // contains a raw BasePage* pointer that can't be predicted at
    // serialization time.

    EmitReadOnlyRootsTable();
    EmitPostProcessRanges();

    // Build the blob.
    std::vector<uint8_t> blob = BuildRoSpaceImageBlob(ro_space);
    uint32_t blob_size = static_cast<uint32_t>(blob.size());

    // The blob goes at the very end of the payload (no Pad() after it).
    // We store offset-from-end so the deserializer can find it without
    // knowing the total payload size at bytecode-write time.
    // Since the blob is last, offset_from_end == blob_size.
    sink_->Put(Bytecode::kRoSpaceImage, "ro space image");
    sink_->PutUint32(blob_size, "blob offset from end");
    sink_->Put(Bytecode::kFinalizeReadOnlySpace, "space end");
    sink_->PutUint30(isolate_->next_unique_sfi_id(), "shared function info ID");

    // Pad to page alignment within the payload, then write the blob.
    // The snapshot blob header is padded so that this payload starts at a
    // page-aligned offset within the blob. When the blob is compiled into
    // the binary with alignas(kTargetMinimumOSPageSize), the blob will be
    // at an absolute page-aligned address.
    size_t aligned_blob_start = RoundUp(
        sink_->Position(), static_cast<size_t>(kTargetMinimumOSPageSize));
    sink_->PutN(aligned_blob_start - sink_->Position(), 0,
                "blob alignment padding");
    sink_->PutRaw(blob.data(), static_cast<int>(blob_size),
                  "ro space image blob");
#else
    // Legacy path: emit per-segment inline data.
    for (const ReadOnlyPage* page : ro_space->pages()) {
      SerializePage(page);
    }

    EmitReadOnlyRootsTable();
    EmitPostProcessRanges();
    sink_->Put(Bytecode::kFinalizeReadOnlySpace, "space end");
    sink_->PutUint30(isolate_->next_unique_sfi_id(), "shared function info ID");
#endif
  }

  uint32_t IndexOf(const ReadOnlyPage* page) {
    ReadOnlySpace* ro_space = isolate_->read_only_heap()->read_only_space();
    return static_cast<uint32_t>(ro_space->IndexOf(page));
  }

  void EmitAllocatePage(const ReadOnlyPage* page) {
    if (V8_STATIC_ROOTS_BOOL) {
      sink_->Put(Bytecode::kAllocatePageAt, "fixed page begin");
    } else {
      sink_->Put(Bytecode::kAllocatePage, "page begin");
    }
    sink_->PutUint30(IndexOf(page), "page index");
    sink_->PutUint30(
        static_cast<uint32_t>(page->HighWaterMark() - page->area_start()),
        "area size in bytes");
    if (V8_STATIC_ROOTS_BOOL) {
      auto page_addr = page->ChunkAddress();
      sink_->PutUint32(V8HeapCompressionScheme::CompressAny(page_addr),
                       "page start offset");
    }
  }

  void EmitPostProcessRanges() {
    // Use 4KB granules to minimize scanning of non-post-process objects.
    // Objects are attributed to the granule where they start.
    static constexpr size_t kGranuleSize = 4 * KB;

    ReadOnlySpace* ro_space = isolate_->read_only_heap()->read_only_space();
    for (const ReadOnlyPage* page : ro_space->pages()) {
      const size_t page_size = page->HighWaterMark() - page->area_start();
      const size_t num_granules = (page_size + kGranuleSize - 1) / kGranuleSize;

      // Track first/end for each granule.
      std::vector<Address> granule_first(num_granules, kNullAddress);
      std::vector<Address> granule_end(num_granules, kNullAddress);

      ReadOnlyPageObjectIterator it(page, page->area_start());
      for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
        if (o.address() >= page->HighWaterMark()) break;
        if (NeedsPostProcessing(isolate_, o)) {
          size_t idx = (o.address() - page->area_start()) / kGranuleSize;
          DCHECK_LT(idx, num_granules);
          if (granule_first[idx] == kNullAddress) {
            granule_first[idx] = o.address();
          }
          granule_end[idx] = o.address() + o->Size();
        }
      }

      // Emit a range for each granule that has post-process objects.
      for (size_t i = 0; i < num_granules; i++) {
        if (granule_first[i] != kNullAddress) {
          sink_->Put(Bytecode::kPostProcessRange, "post-process range");
          sink_->PutUint30(IndexOf(page), "page index");
          sink_->PutUint30(
              static_cast<uint32_t>(granule_first[i] - page->area_start()),
              "first offset");
          sink_->PutUint30(
              static_cast<uint32_t>(granule_end[i] - page->area_start()),
              "end offset");
        }
      }
    }
  }

  struct UnmappedBody {
    Address start;
    int size;
  };
  static std::optional<UnmappedBody> GetUnmappedBody(Tagged<HeapObject> obj) {
    if (Tagged<FreeSpace> free_space; TryCast<FreeSpace>(obj, &free_space)) {
      return {{free_space.address() + sizeof(FreeSpace),
               free_space->Size() - static_cast<int>(sizeof(FreeSpace))}};
    }
    if (Tagged<Hole> hole; TryCast<Hole>(obj, &hole)) {
      return {{hole.address() + sizeof(HeapObject),
               sizeof(Hole) - sizeof(HeapObject)}};
    }
#ifdef V8_ENABLE_WEBASSEMBLY
    if (Tagged<WasmNull> wasm_null; TryCast<WasmNull>(obj, &wasm_null)) {
      return {{wasm_null.address() + WasmNull::kHeaderSize,
               WasmNull::kSize - WasmNull::kHeaderSize}};
    }
#endif
    return {};
  }

  void SerializePage(const ReadOnlyPage* page) {
    Address pos = page->area_start();
    if (v8_flags.trace_serializer) {
      PrintF("[ro serializer] Serializing page %p -> %p\n",
             reinterpret_cast<char*>(page->area_start()),
             reinterpret_cast<char*>(page->HighWaterMark()));
    }

    ReadOnlyPageObjectIterator it(page, SkipFreeSpaceOrFiller::kNo);
    while (true) {
      Tagged<HeapObject> obj = it.Next();
      if (obj.is_null() || obj->address() == page->HighWaterMark()) {
        // We have either reached the end of the allocated part of the page,
        // either by exhausting the iterator, or by hitting the high water mark
        // filler.

        if (obj.is_null()) {
          // If we reached the end of the page by exhausting the iterator, we
          // didn't see a page-ending filler, so our last object must have
          // exactly fit the end of the page.
          CHECK_EQ(page->HighWaterMark(), page->area_end());
        } else {
          // Otherwise, this must be a filler which reaches the end of the page.
          CHECK(IsFreeSpaceOrFiller(obj));
          CHECK_EQ(obj->address() + obj->Size(), page->area_end());
        }

        // Either way, do the remaining serialization up to the water mark.
        ptrdiff_t segment_size = page->HighWaterMark() - pos;
        if (segment_size > 0) {
          ReadOnlySegmentForSerialization segment(
              isolate_, page, pos, segment_size, &pre_processor_);
          EmitSegment(&segment);
        }
        return;
      }

      // Otherwise, continue iterating until we see a section we want to skip.
      // Some objects (e.g. FreeSpace) won't care about their body contents, so
      // we don't want to serialize them.
      std::optional<UnmappedBody> unmapped_body = GetUnmappedBody(obj);
      if (!unmapped_body) continue;
      if (unmapped_body->size == 0) continue;

      // Serialize a segment from the current pos, up to the start of the
      // unmapped body.
      ptrdiff_t segment_size = unmapped_body->start - pos;
      CHECK_GT(segment_size, 0);
      ReadOnlySegmentForSerialization segment(isolate_, page, pos, segment_size,
                                              &pre_processor_);
      EmitSegment(&segment);

      if (v8_flags.trace_serializer) {
        PrintF(
            "[ro serializer] * Skipping %p -> %p because of ",
            reinterpret_cast<char*>(pos) + segment_size,
            reinterpret_cast<char*>(pos) + segment_size + unmapped_body->size);
        ShortPrint(obj);
        PrintF("\n");
      }
      pos += segment_size + unmapped_body->size;
    }
  }

  void EmitSegment(const ReadOnlySegmentForSerialization* segment) {
    if (segment->segment_size == 0) return;
    if (v8_flags.trace_serializer) {
      PrintF("[ro serializer] * Serializing segment %p -> %p\n",
             reinterpret_cast<char*>(segment->segment_start),
             reinterpret_cast<char*>(segment->segment_start) +
                 segment->segment_size);
    }
    sink_->Put(Bytecode::kSegment, "segment begin");
    sink_->PutUint30(IndexOf(segment->page), "page index");
    sink_->PutUint30(static_cast<uint32_t>(segment->segment_offset),
                     "segment start offset");
    sink_->PutUint30(static_cast<uint32_t>(segment->segment_size),
                     "segment byte size");
    sink_->PutRaw(segment->contents.get(),
                  static_cast<int>(segment->segment_size), "page");
    if (!V8_STATIC_ROOTS_BOOL) {
      sink_->Put(Bytecode::kRelocateSegment, "relocate segment");
      sink_->PutRaw(segment->tagged_slots.data(),
                    static_cast<int>(segment->tagged_slots.size_in_bytes()),
                    "tagged_slots");
    }
  }

#if V8_ENABLE_SANDBOX
  // Write the MemoryChunk header for a page in the blob and verify it
  // matches the live page's header.
  // The layout is: [MainThreadFlags (uintptr_t)] [metadata_index (uint32_t)]
  // For RO pages with contiguous compressed RO space:
  //   flags = NO_FLAGS (0)
  //   metadata_index = kMainCageMetadataOffset + (page_offset >> kPageSizeBits)
  static void PopulatePageHeader(uint8_t* page_start, size_t page_cage_offset,
                                 const ReadOnlyPage* page) {
    static constexpr size_t kFlagsOffset = 0;
    static constexpr size_t kFlagsSize = sizeof(uintptr_t);
    static constexpr size_t kMetadataIndexOffset = kFlagsSize;

    // Flags are 0 (NO_FLAGS) for contiguous compressed RO space.
    // The blob is zero-initialized so flags are already correct.
    // Verify the live page also has zero flags.
    MemoryChunk* live_chunk = MemoryChunk::FromAddress(page->ChunkAddress());
    uintptr_t live_flags;
    memcpy(&live_flags, reinterpret_cast<uint8_t*>(live_chunk) + kFlagsOffset,
           kFlagsSize);
    DCHECK_EQ(0u, live_flags);

    // metadata_index immediately follows flags.
    uint32_t metadata_index =
        static_cast<uint32_t>(MemoryChunkConstants::kMainCageMetadataOffset +
                              (page_cage_offset >> kPageSizeBits));
    memcpy(page_start + kMetadataIndexOffset, &metadata_index,
           sizeof(metadata_index));

    // Verify our computed metadata_index matches the live page's.
    uint32_t live_metadata_index;
    memcpy(&live_metadata_index,
           reinterpret_cast<uint8_t*>(live_chunk) + kMetadataIndexOffset,
           sizeof(live_metadata_index));
    DCHECK_EQ(metadata_index, live_metadata_index);
  }

  // Build a single contiguous blob containing the full RO space image.
  // The blob contains complete pages at their natural cage-relative offsets.
  // Unmapped bodies (holes, WasmNull, filler past high-water-mark) are zeroed
  // for good gzip compression. Page headers are pre-populated with the correct
  // flags (0) and metadata index so the mmap path won't need to write them.
  std::vector<uint8_t> BuildRoSpaceImageBlob(ReadOnlySpace* ro_space) {
    DCHECK(V8_STATIC_ROOTS_BOOL);
    const auto& pages = ro_space->pages();
    DCHECK(!pages.empty());

    // First page is at cage base (offset 0).
    Address cage_base = pages.front()->ChunkAddress();
    // Blob covers from the cage base to the last page's high water mark.
    const ReadOnlyPage* last_page = pages.back();
    size_t blob_size = (last_page->ChunkAddress() - cage_base) +
                       (last_page->HighWaterMark() - last_page->ChunkAddress());

    // Allocate zero-initialized blob.
    std::vector<uint8_t> blob(blob_size, 0);

    size_t header_size =
        MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(RO_SPACE);

    // Pass 1: Populate headers and copy area content into the blob, skipping
    // unmapped bodies (hole/WasmNull payloads) which may have been
    // decommitted at runtime. The blob has zeros there from initialization.
    for (const ReadOnlyPage* page : pages) {
      size_t page_offset = page->ChunkAddress() - cage_base;
      PopulatePageHeader(blob.data() + page_offset, page_offset, page);

      uint8_t* page_area_in_blob = blob.data() + page_offset + header_size;
      Address area_start = page->area_start();
      Address segment_start = area_start;

      ReadOnlyPageObjectIterator it(page, SkipFreeSpaceOrFiller::kNo);
      for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
        if (o.address() >= page->HighWaterMark()) break;
        std::optional<UnmappedBody> unmapped = GetUnmappedBody(o);
        if (unmapped && unmapped->size > 0) {
          if (segment_start < unmapped->start) {
            MemCopy(page_area_in_blob + (segment_start - area_start),
                    reinterpret_cast<void*>(segment_start),
                    unmapped->start - segment_start);
          }
          segment_start = unmapped->start + unmapped->size;
        }
      }
      Address high_water_mark = page->HighWaterMark();
      if (segment_start < high_water_mark) {
        MemCopy(page_area_in_blob + (segment_start - area_start),
                reinterpret_cast<void*>(segment_start),
                high_water_mark - segment_start);
      }
    }

    // Pass 2: Pre-process all objects in the blob copy (encode external
    // pointers etc.).
    for (const ReadOnlyPage* page : pages) {
      size_t page_offset = page->ChunkAddress() - cage_base;
      uint8_t* page_area_in_blob = blob.data() + page_offset + header_size;
      Address area_start = page->area_start();

      ReadOnlyPageObjectIterator it(page, SkipFreeSpaceOrFiller::kNo);
      for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
        if (o.address() >= page->HighWaterMark()) break;
        size_t o_offset = o.ptr() - area_start;
        Address o_in_blob =
            reinterpret_cast<Address>(page_area_in_blob) + o_offset;
        pre_processor_.PreProcessIfNeeded(
            Cast<HeapObject>(Tagged<Object>(o_in_blob)));
      }
    }

    return blob;
  }
#endif  // V8_COMPRESS_POINTERS

  void EmitReadOnlyRootsTable() {
    sink_->Put(Bytecode::kReadOnlyRootsTable, "read only roots table");
    if (!V8_STATIC_ROOTS_BOOL) {
      ReadOnlyRoots roots(isolate_);
      for (size_t i = 0; i < ReadOnlyRoots::kEntriesCount; i++) {
        RootIndex rudi = static_cast<RootIndex>(i);
        Tagged<HeapObject> rudolf = Cast<HeapObject>(roots.object_at(rudi));
        ro::EncodedTagged encoded = Encode(isolate_, rudolf);
        sink_->PutUint32(encoded.ToUint32(), "read only roots entry");
      }
    }
  }

  Isolate* const isolate_;
  SnapshotByteSink* const sink_;
  ObjectPreProcessor pre_processor_;
};

}  // namespace

ReadOnlySerializer::ReadOnlySerializer(Isolate* isolate,
                                       Snapshot::SerializerFlags flags)
    : RootsSerializer(isolate, flags, RootIndex::kFirstReadOnlyRoot) {}

ReadOnlySerializer::~ReadOnlySerializer() {
  OutputStatistics("ReadOnlySerializer");
}

void ReadOnlySerializer::Serialize() {
  DisallowGarbageCollection no_gc;
  ReadOnlyHeapImageSerializer::Serialize(isolate(), &sink_);

  ReadOnlyHeapObjectIterator it(isolate()->read_only_heap());
  for (Tagged<HeapObject> o = it.Next(); !o.is_null(); o = it.Next()) {
    CheckRehashability(o);
    if (v8_flags.serialization_statistics) {
      CountAllocation(o->map(), o->Size(), SnapshotSpace::kReadOnlyHeap);
    }
  }

  // In the blob path the blob is the last thing in the payload and provides
  // sufficient read-ahead bytes for GetUint30. In the legacy path we need
  // explicit Pad() nops.
#if !(V8_STATIC_ROOTS_BOOL && V8_ENABLE_SANDBOX)
  Pad();
#endif
}

}  // namespace internal
}  // namespace v8
