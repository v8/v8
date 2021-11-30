// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_SAFEPOINT_TABLE_H_
#define V8_CODEGEN_SAFEPOINT_TABLE_H_

#include "src/base/bit-field.h"
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

  SafepointEntry(int pc, int deopt_index, uint32_t tagged_register_indexes,
                 base::Vector<uint8_t> tagged_slots, int trampoline_pc)
      : pc_(pc),
        deopt_index_(deopt_index),
        tagged_register_indexes_(tagged_register_indexes),
        tagged_slots_(tagged_slots),
        trampoline_pc_(trampoline_pc) {
    DCHECK(is_valid());
  }

  bool is_valid() const { return tagged_slots_.begin() != nullptr; }

  bool operator==(const SafepointEntry& other) const {
    return pc_ == other.pc_ && deopt_index_ == other.deopt_index_ &&
           tagged_register_indexes_ == other.tagged_register_indexes_ &&
           tagged_slots_ == other.tagged_slots_ &&
           trampoline_pc_ == other.trampoline_pc_;
  }

  void Reset() {
    *this = SafepointEntry{};
    DCHECK(!is_valid());
  }

  int pc() const { return pc_; }

  int trampoline_pc() const { return trampoline_pc_; }

  bool has_deoptimization_index() const {
    DCHECK(is_valid());
    return deopt_index_ != kNoDeoptIndex;
  }

  int deoptimization_index() const {
    DCHECK(is_valid() && has_deoptimization_index());
    return deopt_index_;
  }

  uint32_t tagged_register_indexes() const {
    DCHECK(is_valid());
    return tagged_register_indexes_;
  }

  base::Vector<const uint8_t> tagged_slots() const {
    DCHECK(is_valid());
    return tagged_slots_;
  }

 private:
  int pc_ = -1;
  int deopt_index_ = kNoDeoptIndex;
  uint32_t tagged_register_indexes_ = 0;
  base::Vector<uint8_t> tagged_slots_;
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

  int length() const { return length_; }

  int byte_size() const {
    return kHeaderSize + length_ * (entry_size() + tagged_slots_bytes());
  }

  int find_return_pc(int pc_offset);

  SafepointEntry GetEntry(int index) const {
    DCHECK_GT(length_, index);
    Address entry_ptr =
        safepoint_table_address_ + kHeaderSize + index * entry_size();

    int pc = base::Memory<int>(entry_ptr);
    entry_ptr += kPcSize;
    int deopt_index = SafepointEntry::kNoDeoptIndex;
    int trampoline_pc = SafepointEntry::kNoTrampolinePC;
    if (has_deopt_data()) {
      deopt_index = base::Memory<int>(entry_ptr);
      trampoline_pc = base::Memory<int>(entry_ptr + kIntSize);
      entry_ptr += kDeoptDataSize;
    }
    int tagged_register_indexes = 0;
    if (has_register_indexes()) {
      tagged_register_indexes = base::Memory<int>(entry_ptr);
      entry_ptr += kRegisterIndexesSize;
    }

    // Entry bits start after the the vector of entries (thus the pc offset of
    // the non-existing entry after the last one).
    uint8_t* tagged_slots_start = reinterpret_cast<uint8_t*>(
        safepoint_table_address_ + kHeaderSize + length_ * entry_size());
    base::Vector<uint8_t> tagged_slots(
        tagged_slots_start + index * tagged_slots_bytes(),
        tagged_slots_bytes());

    return SafepointEntry(pc, deopt_index, tagged_register_indexes,
                          tagged_slots, trampoline_pc);
  }

  // Returns the entry for the given pc.
  SafepointEntry FindEntry(Address pc) const;

  void PrintEntry(int index, std::ostream& os) const;

 private:
  // Layout information.
  static constexpr int kLengthOffset = 0;
  static constexpr int kEntryConfigurationOffset = kLengthOffset + kIntSize;
  static constexpr int kHeaderSize = kEntryConfigurationOffset + kUInt32Size;

  // An entry consists of the pc, plus optional deopt data (deopt index and
  // trampoline PC), plus optional register indexes.
  static constexpr int kPcSize = kIntSize;
  static constexpr int kDeoptDataSize = 2 * kIntSize;
  static constexpr int kRegisterIndexesSize = kIntSize;

  using TaggedSlotsBytesField = base::BitField<int, 0, 30>;
  using HasDeoptDataField = TaggedSlotsBytesField::Next<bool, 1>;
  using HasRegisterIndexesField = HasDeoptDataField::Next<bool, 1>;

  SafepointTable(Address instruction_start, Address safepoint_table_address);

  int entry_size() const {
    return kPcSize + (has_deopt_data() ? kDeoptDataSize : 0) +
           (has_register_indexes() ? kRegisterIndexesSize : 0);
  }

  int tagged_slots_bytes() const {
    return TaggedSlotsBytesField::decode(entry_configuration_);
  }
  bool has_deopt_data() const {
    return HasDeoptDataField::decode(entry_configuration_);
  }
  bool has_register_indexes() const {
    return HasRegisterIndexesField::decode(entry_configuration_);
  }

  DISALLOW_GARBAGE_COLLECTION(no_gc_)

  const Address instruction_start_;

  // Safepoint table layout.
  const Address safepoint_table_address_;
  const int length_;
  const uint32_t entry_configuration_;

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

  // Remove consecutive identical entries.
  void RemoveDuplicates();

  // Try to trim entries by removing trailing zeros (and shrinking
  // {bits_per_entry}).
  void TrimEntries(int* bits_per_entry);

  ZoneChunkList<EntryBuilder> entries_;

  int offset_ = -1;

  Zone* const zone_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_SAFEPOINT_TABLE_H_
