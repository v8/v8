// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_H_
#define V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_H_

#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"

namespace v8 {
namespace internal {

class Code;
class Isolate;

// Wraps an off-heap instruction stream.
// TODO(jgruber,v8:6666): Remove this class.
class InstructionStream final : public AllStatic {
 public:
  // Returns true, iff the given pc points into an off-heap instruction stream.
  static bool PcIsOffHeap(Isolate* isolate, Address pc);

  // Returns the corresponding Code object if it exists, and nullptr otherwise.
  static Code TryLookupCode(Isolate* isolate, Address address);

  // During snapshot creation, we first create an executable off-heap area
  // containing all off-heap code. The area is guaranteed to be contiguous.
  // Note that this only applies when building the snapshot, e.g. for
  // mksnapshot. Otherwise, off-heap code is embedded directly into the binary.
  static void CreateOffHeapInstructionStream(Isolate* isolate, uint8_t** code,
                                             uint32_t* code_size,
                                             uint8_t** metadata,
                                             uint32_t* metadata_size);
  static void FreeOffHeapInstructionStream(uint8_t* code, uint32_t code_size,
                                           uint8_t* metadata,
                                           uint32_t metadata_size);
};

class EmbeddedData final {
 public:
  static EmbeddedData FromIsolate(Isolate* isolate);

  static EmbeddedData FromBlob() {
    return EmbeddedData(Isolate::CurrentEmbeddedBlobCode(),
                        Isolate::CurrentEmbeddedBlobCodeSize(),
                        Isolate::CurrentEmbeddedBlobMetadata(),
                        Isolate::CurrentEmbeddedBlobMetadataSize());
  }

  static EmbeddedData FromBlob(Isolate* isolate) {
    return EmbeddedData(isolate->embedded_blob_code(),
                        isolate->embedded_blob_code_size(),
                        isolate->embedded_blob_metadata(),
                        isolate->embedded_blob_metadata_size());
  }

  const uint8_t* code() const { return code_; }
  uint32_t code_size() const { return code_size_; }
  const uint8_t* metadata() const { return metadata_; }
  uint32_t metadata_size() const { return metadata_size_; }

  void Dispose() {
    delete[] code_;
    code_ = nullptr;
    delete[] metadata_;
    metadata_ = nullptr;
  }

  Address InstructionStartOfBuiltin(int i) const;
  uint32_t InstructionSizeOfBuiltin(int i) const;

  Address InstructionStartOfBytecodeHandlers() const;
  Address InstructionEndOfBytecodeHandlers() const;

  Address MetadataStartOfBuiltin(int i) const;
  uint32_t MetadataSizeOfBuiltin(int i) const;

  uint32_t AddressForHashing(Address addr) {
    Address start = reinterpret_cast<Address>(code_);
    DCHECK(base::IsInRange(addr, start, start + code_size_));
    return static_cast<uint32_t>(addr - start);
  }

  // Padded with kCodeAlignment.
  // TODO(v8:11045): Consider removing code alignment.
  uint32_t PaddedInstructionSizeOfBuiltin(int i) const {
    STATIC_ASSERT(Code::kOffHeapBodyIsContiguous);
    uint32_t size = InstructionSizeOfBuiltin(i) + MetadataSizeOfBuiltin(i);
    CHECK_NE(size, 0);
    return PadAndAlign(size);
  }

  size_t CreateEmbeddedBlobHash() const;
  size_t EmbeddedBlobHash() const {
    return *reinterpret_cast<const size_t*>(metadata_ +
                                            EmbeddedBlobHashOffset());
  }

  size_t IsolateHash() const {
    return *reinterpret_cast<const size_t*>(metadata_ + IsolateHashOffset());
  }

  // Blob layout information for a single instruction stream. Corresponds
  // roughly to Code object layout (see the instruction and metadata area).
  // TODO(jgruber): With the addition of metadata sections in Code objects,
  // naming here has become confusing. Metadata refers to both this struct
  // and the Code section, and the embedded instruction area currently
  // contains both Code's instruction and metadata areas. Fix it.
  struct Metadata {
    // The offset and (unpadded) length of this builtin's instruction area
    // from the start of the embedded code section.
    uint32_t instruction_offset;
    uint32_t instruction_length;
    // The offset and (unpadded) length of this builtin's metadata area
    // from the start of the embedded code section.
    // TODO(jgruber,v8:11036): Move this to the embedded metadata area.
    uint32_t metadata_offset;
    uint32_t metadata_length;
  };
  STATIC_ASSERT(offsetof(Metadata, instruction_offset) == 0 * kUInt32Size);
  STATIC_ASSERT(offsetof(Metadata, instruction_length) == 1 * kUInt32Size);
  STATIC_ASSERT(offsetof(Metadata, metadata_offset) == 2 * kUInt32Size);
  STATIC_ASSERT(offsetof(Metadata, metadata_length) == 3 * kUInt32Size);
  STATIC_ASSERT(sizeof(Metadata) == 4 * kUInt32Size);

  // The layout of the blob is as follows:
  //
  // metadata:
  // [0] hash of the remaining blob
  // [1] hash of embedded-blob-relevant heap objects
  // [2] metadata of instruction stream 0
  // ... metadata
  //
  // code:
  // [0] instruction streams 0
  // ... instruction streams

  static constexpr uint32_t kTableSize = Builtins::builtin_count;
  static constexpr uint32_t EmbeddedBlobHashOffset() { return 0; }
  static constexpr uint32_t EmbeddedBlobHashSize() { return kSizetSize; }
  static constexpr uint32_t IsolateHashOffset() {
    return EmbeddedBlobHashOffset() + EmbeddedBlobHashSize();
  }
  static constexpr uint32_t IsolateHashSize() { return kSizetSize; }
  static constexpr uint32_t MetadataTableOffset() {
    return IsolateHashOffset() + IsolateHashSize();
  }
  static constexpr uint32_t MetadataTableSize() {
    return sizeof(struct Metadata) * kTableSize;
  }
  static constexpr uint32_t RawCodeOffset() { return 0; }

 private:
  EmbeddedData(const uint8_t* code, uint32_t code_size, const uint8_t* metadata,
               uint32_t metadata_size)
      : code_(code),
        code_size_(code_size),
        metadata_(metadata),
        metadata_size_(metadata_size) {
    DCHECK_NOT_NULL(code);
    DCHECK_LT(0, code_size);
    DCHECK_NOT_NULL(metadata);
    DCHECK_LT(0, metadata_size);
  }

  const Metadata* Metadata() const {
    return reinterpret_cast<const struct Metadata*>(metadata_ +
                                                    MetadataTableOffset());
  }
  const uint8_t* RawCode() const { return code_ + RawCodeOffset(); }

  static constexpr int PadAndAlign(int size) {
    // Ensure we have at least one byte trailing the actual builtin
    // instructions which we can later fill with int3.
    return RoundUp<kCodeAlignment>(size + 1);
  }

  void PrintStatistics() const;

  // This points to code for builtins. The contents are potentially unreadable
  // on platforms that disallow reads from the .text section.
  const uint8_t* code_;
  uint32_t code_size_;

  // This is metadata for the code.
  const uint8_t* metadata_;
  uint32_t metadata_size_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_EMBEDDED_EMBEDDED_DATA_H_
