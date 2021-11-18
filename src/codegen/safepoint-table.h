// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SAFEPOINT_TABLE_H_
#define V8_CODEGEN_SAFEPOINT_TABLE_H_

#include "src/base/iterator.h"
#include "src/base/memory.h"
#include "src/common/assert-scope.h"
#include "src/utils/allocation.h"
#include "src/utils/utils.h"
#include "src/zone/zone-chunk-list.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

namespace wasm {
class WasmCode;
}  // namespace wasm

class SafepointEntry {
 public:
  static constexpr int kNoDeoptIndex = -1;
  static constexpr int kNoTrampolinePC = -1;

  SafepointEntry() = default;

  SafepointEntry(int deopt_index, uint8_t* bits, uint8_t* bits_end,
                 int trampoline_pc)
      : deopt_index_(deopt_index),
        bits_(bits),
        bits_end_(bits_end),
        trampoline_pc_(trampoline_pc) {
    DCHECK(is_valid());
  }

  bool is_valid() const { return bits_ != nullptr; }

  bool Equals(const SafepointEntry& other) const {
    return deopt_index_ == other.deopt_index_ && bits_ == other.bits_;
  }

  void Reset() {
    deopt_index_ = kNoDeoptIndex;
    bits_ = nullptr;
    bits_end_ = nullptr;
  }

  int trampoline_pc() { return trampoline_pc_; }

  int deoptimization_index() const {
    DCHECK(is_valid() && has_deoptimization_index());
    return deopt_index_;
  }

  uint32_t register_bits() const {
    // The register bits use the same field as the deopt_index_.
    DCHECK(is_valid());
    return deopt_index_;
  }

  bool has_register_bits() const {
    // The register bits use the same field as the deopt_index_.
    DCHECK(is_valid());
    return deopt_index_ != kNoDeoptIndex;
  }

  bool has_deoptimization_index() const {
    DCHECK(is_valid());
    return deopt_index_ != kNoDeoptIndex;
  }

  uint8_t* bits() const {
    DCHECK(is_valid());
    return bits_;
  }

  base::iterator_range<uint8_t*> iterate_bits() const {
    return base::make_iterator_range(bits_, bits_end_);
  }

  size_t entry_size() const { return bits_end_ - bits_; }

 private:
  int deopt_index_ = kNoDeoptIndex;
  uint8_t* bits_ = nullptr;
  uint8_t* bits_end_ = nullptr;
  int trampoline_pc_ = kNoTrampolinePC;
};

class SafepointTable {
 public:
  // The isolate and pc arguments are used for figuring out whether pc
  // belongs to the embedded or un-embedded code blob.
  explicit SafepointTable(Isolate* isolate, Address pc, Code code);
#if V8_ENABLE_WEBASSEMBLY
  explicit SafepointTable(const wasm::WasmCode* code);
#endif  // V8_ENABLE_WEBASSEMBLY

  SafepointTable(const SafepointTable&) = delete;
  SafepointTable& operator=(const SafepointTable&) = delete;

  int size() const {
    return kHeaderSize + (length_ * (kFixedEntrySize + entry_size_));
  }
  int length() const { return length_; }
  int entry_size() const { return entry_size_; }

  int GetPcOffset(int index) const {
    DCHECK_GT(length_, index);
    return base::Memory<int>(GetPcOffsetLocation(index));
  }

  int GetTrampolinePcOffset(int index) const {
    DCHECK_GT(length_, index);
    return base::Memory<int>(GetTrampolineLocation(index));
  }

  int find_return_pc(int pc_offset);

  SafepointEntry GetEntry(int index) const {
    DCHECK_GT(length_, index);
    int deopt_index = base::Memory<int>(GetEncodedInfoLocation(index));
    uint8_t* bits = &base::Memory<uint8_t>(entries() + (index * entry_size_));
    int trampoline_pc = has_deopt_
                            ? base::Memory<int>(GetTrampolineLocation(index))
                            : SafepointEntry::kNoTrampolinePC;
    return SafepointEntry(deopt_index, bits, bits + entry_size_, trampoline_pc);
  }

  // Returns the entry for the given pc.
  SafepointEntry FindEntry(Address pc) const;

  void PrintEntry(int index, std::ostream& os) const;

 private:
  SafepointTable(Address instruction_start, Address safepoint_table_address,
                 bool has_deopt);

  static const uint8_t kNoRegisters = 0xFF;

  // Layout information.
  static const int kLengthOffset = 0;
  static const int kEntrySizeOffset = kLengthOffset + kIntSize;
  static const int kHeaderSize = kEntrySizeOffset + kIntSize;
  static const int kPcOffset = 0;
  static const int kEncodedInfoOffset = kPcOffset + kIntSize;
  static const int kTrampolinePcOffset = kEncodedInfoOffset + kIntSize;
  static const int kFixedEntrySize = kTrampolinePcOffset + kIntSize;

  static int ReadLength(Address table) {
    return base::Memory<int>(table + kLengthOffset);
  }
  static int ReadEntrySize(Address table) {
    return base::Memory<int>(table + kEntrySizeOffset);
  }
  Address pc_and_deoptimization_indexes() const {
    return safepoint_table_address_ + kHeaderSize;
  }
  Address entries() const {
    return safepoint_table_address_ + kHeaderSize + (length_ * kFixedEntrySize);
  }

  Address GetPcOffsetLocation(int index) const {
    return pc_and_deoptimization_indexes() + (index * kFixedEntrySize);
  }

  Address GetEncodedInfoLocation(int index) const {
    return GetPcOffsetLocation(index) + kEncodedInfoOffset;
  }

  Address GetTrampolineLocation(int index) const {
    return GetPcOffsetLocation(index) + kTrampolinePcOffset;
  }

  DISALLOW_GARBAGE_COLLECTION(no_gc_)

  const Address instruction_start_;
  const bool has_deopt_;

  // Safepoint table layout.
  const Address safepoint_table_address_;
  const int length_;
  const int entry_size_;

  friend class SafepointTableBuilder;
  friend class SafepointEntry;
};

class Safepoint {
 public:
  void DefinePointerSlot(int index) { stack_indexes_->push_back(index); }

  void DefineRegister(int reg_code) {
    // Make sure the recorded index is always less than 31, so that we don't
    // generate {kNoDeoptIndex} by accident.
    DCHECK_LT(reg_code, 31);
    *register_indexes_ |= 1u << reg_code;
  }

 private:
  Safepoint(ZoneChunkList<int>* stack_indexes, uint32_t* register_indexes)
      : stack_indexes_(stack_indexes), register_indexes_(register_indexes) {}
  ZoneChunkList<int>* const stack_indexes_;
  uint32_t* register_indexes_;

  friend class SafepointTableBuilder;
};

class SafepointTableBuilder {
 public:
  explicit SafepointTableBuilder(Zone* zone) : entries_(zone), zone_(zone) {}

  SafepointTableBuilder(const SafepointTableBuilder&) = delete;
  SafepointTableBuilder& operator=(const SafepointTableBuilder&) = delete;

  bool emitted() const { return offset_ != -1; }

  // Get the offset of the emitted safepoint table in the code.
  int GetCodeOffset() const {
    DCHECK(emitted());
    return offset_;
  }

  // Define a new safepoint for the current position in the body.
  Safepoint DefineSafepoint(Assembler* assembler);

  // Emit the safepoint table after the body. The number of bits per
  // entry must be enough to hold all the pointer indexes.
  V8_EXPORT_PRIVATE void Emit(Assembler* assembler, int bits_per_entry);

  // Find the Deoptimization Info with pc offset {pc} and update its
  // trampoline field. Calling this function ensures that the safepoint
  // table contains the trampoline PC {trampoline} that replaced the
  // return PC {pc} on the stack.
  int UpdateDeoptimizationInfo(int pc, int trampoline, int start,
                               int deopt_index);

 private:
  struct EntryBuilder {
    int pc;
    int deopt_index;
    int trampoline;
    ZoneChunkList<int>* stack_indexes;
    uint32_t register_indexes;
    EntryBuilder(Zone* zone, int pc)
        : pc(pc),
          deopt_index(SafepointEntry::kNoDeoptIndex),
          trampoline(SafepointEntry::kNoTrampolinePC),
          stack_indexes(zone->New<ZoneChunkList<int>>(
              zone, ZoneChunkList<int>::StartMode::kSmall)),
          register_indexes(0) {}
  };

  // If all entries are identical, replace them by 1 entry with pc = kMaxUInt32.
  void RemoveDuplicates();

  // Try to trim entries by removing trailing zeros (and shrinking
  // {bits_per_entry}).
  void TrimEntries(int* bits_per_entry);

  ZoneChunkList<EntryBuilder> entries_;

  int offset_ = -1;

  Zone* zone_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_SAFEPOINT_TABLE_H_
