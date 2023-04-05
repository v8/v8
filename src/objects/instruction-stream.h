// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INSTRUCTION_STREAM_H_
#define V8_OBJECTS_INSTRUCTION_STREAM_H_

#include "src/objects/heap-object.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class Code;

// InstructionStream contains the instruction stream for V8-generated code
// objects.
//
// When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
// allocated in a separate pointer compression cage instead of the cage where
// all the other objects are allocated.
class InstructionStream : public HeapObject {
 public:
  NEVER_READ_ONLY_SPACE

  // All InstructionStream objects have the following layout:
  //
  //  +--------------------------+
  //  |          header          |
  //  +--------------------------+  <-- body_start()
  //  |       instructions       |   == instruction_start()
  //  |           ...            |
  //  | padded to meta alignment |      see kMetadataAlignment
  //  +--------------------------+  <-- instruction_end()
  //  |         metadata         |   == metadata_start() (MS)
  //  |           ...            |
  //  |                          |  <-- MS + handler_table_offset()
  //  |                          |  <-- MS + constant_pool_offset()
  //  |                          |  <-- MS + code_comments_offset()
  //  |                          |  <-- MS + unwinding_info_offset()
  //  | padded to obj alignment  |
  //  +--------------------------+  <-- metadata_end() == body_end()
  //  | padded to kCodeAlignmentMinusCodeHeader
  //  +--------------------------+
  //
  // In other words, the variable-size 'body' consists of 'instructions' and
  // 'metadata'.

  // Constants for use in static asserts, stating whether the body is adjacent,
  // i.e. instructions and metadata areas are adjacent.
  static constexpr bool kOnHeapBodyIsContiguous = true;
  static constexpr bool kOffHeapBodyIsContiguous = false;
  static constexpr bool kBodyIsContiguous =
      kOnHeapBodyIsContiguous && kOffHeapBodyIsContiguous;

  inline Address instruction_start() const;

  // The metadata section is aligned to this value.
  static constexpr int kMetadataAlignment = kIntSize;

  // [code]: The associated Code object.
  DECL_RELEASE_ACQUIRE_ACCESSORS(code, Code)
  DECL_RELEASE_ACQUIRE_ACCESSORS(raw_code, HeapObject)
  inline Code unchecked_code(AcquireLoadTag tag) const;

  // When V8_EXTERNAL_CODE_SPACE is enabled, InstructionStream objects are
  // allocated in a separate pointer compression cage instead of the cage where
  // all the other objects are allocated. This field contains cage base value
  // which is used for decompressing the references to non-InstructionStream
  // objects (map, deoptimization_data, etc.).
  inline PtrComprCageBase main_cage_base() const;
  inline PtrComprCageBase main_cage_base(RelaxedLoadTag) const;
  inline void set_main_cage_base(Address cage_base, RelaxedStoreTag);

  // The size of the entire body section, containing instructions and inlined
  // metadata.
  DECL_PRIMITIVE_ACCESSORS(body_size, int)
  inline Address body_end() const;

  // [relocation_info]: InstructionStream relocation information
  DECL_ACCESSORS(relocation_info, ByteArray)
  // Unchecked accessor to be used during GC.
  inline ByteArray unchecked_relocation_info() const;

  inline byte* relocation_start() const;
  inline byte* relocation_end() const;
  inline int relocation_size() const;

  // The entire code object including its header is copied verbatim to the
  // snapshot so that it can be written in one, fast, memcpy during
  // deserialization. The deserializer will overwrite some pointers, rather
  // like a runtime linker, but the random allocation addresses used in the
  // mksnapshot process would still be present in the unlinked snapshot data,
  // which would make snapshot production non-reproducible. This method wipes
  // out the to-be-overwritten header data for reproducible snapshots.
  inline void WipeOutHeader();

  static inline InstructionStream FromTargetAddress(Address address);
  static inline InstructionStream FromEntryAddress(Address location_of_address);

  // Relocate the code by delta bytes. Called to signal that this code
  // object has been moved by delta bytes.
  void Relocate(intptr_t delta);

  inline void clear_padding();

  static constexpr int TrailingPaddingSizeFor(int body_size) {
    return RoundUp<kCodeAlignment>(kHeaderSize + body_size) - kHeaderSize -
           body_size;
  }
  static constexpr int SizeFor(int body_size) {
    return kHeaderSize + body_size + TrailingPaddingSizeFor(body_size);
  }
  inline int Size() const;

  DECL_CAST(InstructionStream)
  DECL_PRINTER(InstructionStream)
  DECL_VERIFIER(InstructionStream)

  // Layout description.
#define ISTREAM_FIELDS(V)                                             \
  V(kStartOfStrongFieldsOffset, 0)                                    \
  V(kCodeOffset, kTaggedSize)                                         \
  V(kRelocationInfoOffset, kTaggedSize)                               \
  V(kEndOfStrongFieldsOffset, 0)                                      \
  /* Data or code not directly visited by GC directly starts here. */ \
  V(kDataStart, 0)                                                    \
  V(kMainCageBaseUpper32BitsOffset,                                   \
    V8_EXTERNAL_CODE_SPACE_BOOL ? kTaggedSize : 0)                    \
  V(kBodySizeOffset, kIntSize)                                        \
  V(kUnalignedSize, OBJECT_POINTER_PADDING(kUnalignedSize))           \
  V(kHeaderSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, ISTREAM_FIELDS)
#undef ISTREAM_FIELDS

  static_assert(kCodeAlignment >= kHeaderSize);
  // We do two things to ensure kCodeAlignment of the entry address:
  // 1) Add kCodeAlignmentMinusCodeHeader padding once in the beginning of every
  //    MemoryChunk.
  // 2) Round up all IStream allocations to a multiple of kCodeAlignment, see
  //    TrailingPaddingSizeFor.
  // Together, the IStream object itself will always start at offset
  // kCodeAlignmentMinusCodeHeader, which aligns the entry to kCodeAlignment.
  static constexpr int kCodeAlignmentMinusCodeHeader =
      kCodeAlignment - kHeaderSize;

  class BodyDescriptor;

 private:
  OBJECT_CONSTRUCTORS(InstructionStream, HeapObject);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INSTRUCTION_STREAM_H_
