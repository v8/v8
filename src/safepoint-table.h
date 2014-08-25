// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SAFEPOINT_TABLE_H_
#define V8_SAFEPOINT_TABLE_H_

#include "src/allocation.h"
#include "src/heap/heap.h"
#include "src/v8memory.h"
#include "src/zone.h"

namespace v8 {
namespace internal {

struct Register;

class SafepointEntry BASE_EMBEDDED {
 public:
  SafepointEntry() : info_(0), deoptimization_pc_(0), bits_(NULL) {}

  SafepointEntry(unsigned info, unsigned deoptimization_pc, uint8_t* bits)
      : info_(info), deoptimization_pc_(deoptimization_pc), bits_(bits) {
    DCHECK(is_valid());
  }

  bool is_valid() const { return bits_ != NULL; }

  bool Equals(const SafepointEntry& other) const {
    return info_ == other.info_ && bits_ == other.bits_;
  }

  void Reset() {
    info_ = 0;
    bits_ = NULL;
  }

  int deoptimization_index() const {
    DCHECK(is_valid());
    return DeoptimizationIndexField::decode(info_);
  }

  unsigned deoptimization_pc() const {
    DCHECK(is_valid());
    return deoptimization_pc_;
  }

  static const int kArgumentsFieldBits = 3;
  static const int kSaveDoublesFieldBits = 1;
  static const int kDeoptIndexBits =
      32 - kArgumentsFieldBits - kSaveDoublesFieldBits;
  class DeoptimizationIndexField:
    public BitField<int, 0, kDeoptIndexBits> {};  // NOLINT
  class ArgumentsField:
    public BitField<unsigned,
                    kDeoptIndexBits,
                    kArgumentsFieldBits> {};  // NOLINT
  class SaveDoublesField:
    public BitField<bool,
                    kDeoptIndexBits + kArgumentsFieldBits,
                    kSaveDoublesFieldBits> { }; // NOLINT

  int argument_count() const {
    DCHECK(is_valid());
    return ArgumentsField::decode(info_);
  }

  bool has_doubles() const {
    DCHECK(is_valid());
    return SaveDoublesField::decode(info_);
  }

  uint8_t* bits() {
    DCHECK(is_valid());
    return bits_;
  }

  bool HasRegisters() const;
  bool HasRegisterAt(int reg_index) const;

 private:
  unsigned info_;
  unsigned deoptimization_pc_;
  uint8_t* bits_;
};


class SafepointTable BASE_EMBEDDED {
 public:
  explicit SafepointTable(Code* code);

  int size() const {
    return kHeaderSize +
           (length_ * (kPcAndDeoptimizationInfoSize + entry_size_));
  }
  unsigned length() const { return length_; }
  unsigned entry_size() const { return entry_size_; }

  unsigned GetPcOffset(unsigned index) const {
    DCHECK(index < length_);
    return Memory::uint32_at(GetPcOffsetLocation(index));
  }

  unsigned GetDeoptimizationPcOffset(unsigned index) const {
    DCHECK(index < length_);
    return Memory::uint32_at(GetDeoptimizationPcLocation(index));
  }

  SafepointEntry GetEntry(unsigned index) const {
    DCHECK(index < length_);
    unsigned info = Memory::uint32_at(GetInfoLocation(index));
    unsigned deopt_pc = Memory::uint32_at(GetDeoptimizationPcLocation(index));
    uint8_t* bits = &Memory::uint8_at(entries_ + (index * entry_size_));
    return SafepointEntry(info, deopt_pc, bits);
  }

  // Returns the entry for the given pc.
  SafepointEntry FindEntry(Address pc) const;

  void PrintEntry(unsigned index, OStream& os) const;  // NOLINT

 private:
  static const uint8_t kNoRegisters = 0xFF;

  static const int kLengthOffset = 0;
  static const int kEntrySizeOffset = kLengthOffset + kIntSize;
  static const int kHeaderSize = kEntrySizeOffset + kIntSize;

  static const int kPcSize = kIntSize;
  static const int kDeoptimizationIndexSize = kIntSize;
  static const int kDeoptimizationPcSize = kIntSize;
  static const int kPcAndDeoptimizationInfoSize =
      kPcSize + kDeoptimizationIndexSize + kDeoptimizationPcSize;

  Address GetPcOffsetLocation(unsigned index) const {
    return pc_and_deoptimization_indexes_ +
           (index * kPcAndDeoptimizationInfoSize);
  }

  Address GetInfoLocation(unsigned index) const {
    return GetPcOffsetLocation(index) + kPcSize;
  }

  Address GetDeoptimizationPcLocation(unsigned index) const {
    return GetInfoLocation(index) + kDeoptimizationIndexSize;
  }

  static void PrintBits(OStream& os,  // NOLINT
                        uint8_t byte, int digits);

  DisallowHeapAllocation no_allocation_;
  Code* code_;
  unsigned length_;
  unsigned entry_size_;

  Address pc_and_deoptimization_indexes_;
  Address entries_;

  friend class SafepointTableBuilder;
  friend class SafepointEntry;

  DISALLOW_COPY_AND_ASSIGN(SafepointTable);
};


class Safepoint BASE_EMBEDDED {
 public:
  typedef enum {
    kSimple = 0,
    kWithRegisters = 1 << 0,
    kWithDoubles = 1 << 1,
    kWithRegistersAndDoubles = kWithRegisters | kWithDoubles
  } Kind;

  enum DeoptMode {
    kNoLazyDeopt,
    kLazyDeopt
  };

  class Id {
   private:
    explicit Id(int id) : id_(id) {}

    int id_;

    friend class SafepointTableBuilder;
    friend class Safepoint;
  };

  static const int kNoDeoptimizationIndex =
      (1 << (SafepointEntry::kDeoptIndexBits)) - 1;

  static const unsigned kNoDeoptimizationPc = ~0U;

  void DefinePointerSlot(int index, Zone* zone) { indexes_->Add(index, zone); }
  void DefinePointerRegister(Register reg, Zone* zone);

  Id id() const { return Id(id_); }

 private:
  Safepoint(int id, ZoneList<int>* indexes, ZoneList<int>* registers)
      : id_(id), indexes_(indexes), registers_(registers) {}
  int id_;
  ZoneList<int>* indexes_;
  ZoneList<int>* registers_;

  friend class SafepointTableBuilder;
};


class SafepointTableBuilder BASE_EMBEDDED {
 public:
  explicit SafepointTableBuilder(Zone* zone)
      : deoptimization_info_(32, zone),
        deopt_index_list_(32, zone),
        indexes_(32, zone),
        registers_(32, zone),
        emitted_(false),
        last_lazy_safepoint_(0),
        zone_(zone) { }

  // Get the offset of the emitted safepoint table in the code.
  unsigned GetCodeOffset() const;

  // Define a new safepoint for the current position in the body.
  Safepoint DefineSafepoint(Assembler* assembler,
                            Safepoint::Kind kind,
                            int arguments,
                            Safepoint::DeoptMode mode);

  // Record deoptimization index for lazy deoptimization for the last
  // outstanding safepoints.
  void RecordLazyDeoptimizationIndex(int index);
  void BumpLastLazySafepointIndex() {
    last_lazy_safepoint_ = deopt_index_list_.length();
  }
  void SetDeoptimizationPc(Safepoint::Id safepoint_id,
                           unsigned deoptimization_pc) {
    deoptimization_info_[safepoint_id.id_].deoptimization_pc =
        deoptimization_pc;
  }

  // Emit the safepoint table after the body. The number of bits per
  // entry must be enough to hold all the pointer indexes.
  void Emit(Assembler* assembler, int bits_per_entry);


 private:
  struct DeoptimizationInfo {
    unsigned pc;
    unsigned arguments;
    bool has_doubles;
    unsigned deoptimization_pc;
  };

  uint32_t EncodeExceptPC(const DeoptimizationInfo& info, unsigned index);

  ZoneList<DeoptimizationInfo> deoptimization_info_;
  ZoneList<unsigned> deopt_index_list_;
  ZoneList<ZoneList<int>*> indexes_;
  ZoneList<ZoneList<int>*> registers_;

  unsigned offset_;
  bool emitted_;
  int last_lazy_safepoint_;

  Zone* zone_;

  DISALLOW_COPY_AND_ASSIGN(SafepointTableBuilder);
};

} }  // namespace v8::internal

#endif  // V8_SAFEPOINT_TABLE_H_
