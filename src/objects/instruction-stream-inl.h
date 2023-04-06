// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INSTRUCTION_STREAM_INL_H_
#define V8_OBJECTS_INSTRUCTION_STREAM_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/code.h"
#include "src/objects/instruction-stream.h"
#include "src/objects/objects-inl.h"  // For HeapObject::IsInstructionStream.

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

CAST_ACCESSOR(InstructionStream)
OBJECT_CONSTRUCTORS_IMPL(InstructionStream, HeapObject)
NEVER_READ_ONLY_SPACE_IMPL(InstructionStream)
INT_ACCESSORS(InstructionStream, body_size, kBodySizeOffset)

Address InstructionStream::body_end() const {
  static_assert(kOnHeapBodyIsContiguous);
  return instruction_start() + body_size();
}

// Same as ACCESSORS_CHECKED2 macro but with InstructionStream
// as a host and using main_cage_base() for computing the base.
#define INSTRUCTION_STREAM_ACCESSORS_CHECKED2(name, type, offset,           \
                                              get_condition, set_condition) \
  type InstructionStream::name() const {                                    \
    PtrComprCageBase cage_base = main_cage_base();                          \
    return InstructionStream::name(cage_base);                              \
  }                                                                         \
  type InstructionStream::name(PtrComprCageBase cage_base) const {          \
    type value = TaggedField<type, offset>::load(cage_base, *this);         \
    DCHECK(get_condition);                                                  \
    return value;                                                           \
  }                                                                         \
  void InstructionStream::set_##name(type value, WriteBarrierMode mode) {   \
    DCHECK(set_condition);                                                  \
    TaggedField<type, offset>::store(*this, value);                         \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);                  \
  }

// Same as RELEASE_ACQUIRE_ACCESSORS_CHECKED2 macro but with InstructionStream
// as a host and using main_cage_base(kRelaxedLoad) for computing the base.
#define RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS_CHECKED2(              \
    name, type, offset, get_condition, set_condition)                       \
  type InstructionStream::name(AcquireLoadTag tag) const {                  \
    PtrComprCageBase cage_base = main_cage_base(kRelaxedLoad);              \
    return InstructionStream::name(cage_base, tag);                         \
  }                                                                         \
  type InstructionStream::name(PtrComprCageBase cage_base, AcquireLoadTag)  \
      const {                                                               \
    type value = TaggedField<type, offset>::Acquire_Load(cage_base, *this); \
    DCHECK(get_condition);                                                  \
    return value;                                                           \
  }                                                                         \
  void InstructionStream::set_##name(type value, ReleaseStoreTag,           \
                                     WriteBarrierMode mode) {               \
    DCHECK(set_condition);                                                  \
    TaggedField<type, offset>::Release_Store(*this, value);                 \
    CONDITIONAL_WRITE_BARRIER(*this, offset, value, mode);                  \
  }

#define INSTRUCTION_STREAM_ACCESSORS(name, type, offset)                 \
  INSTRUCTION_STREAM_ACCESSORS_CHECKED2(name, type, offset,              \
                                        !ObjectInYoungGeneration(value), \
                                        !ObjectInYoungGeneration(value))

#define RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS(name, type, offset) \
  RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS_CHECKED2(                 \
      name, type, offset, !ObjectInYoungGeneration(value),               \
      !ObjectInYoungGeneration(value))

RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS(code, Code, kCodeOffset)
RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS(raw_code, Object, kCodeOffset)
INSTRUCTION_STREAM_ACCESSORS(relocation_info, ByteArray, kRelocationInfoOffset)
#undef INSTRUCTION_STREAM_ACCESSORS
#undef INSTRUCTION_STREAM_ACCESSORS_CHECKED2
#undef RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS
#undef RELEASE_ACQUIRE_INSTRUCTION_STREAM_ACCESSORS_CHECKED2

bool InstructionStream::TryGetCode(Code* code_out, AcquireLoadTag tag) const {
  Object maybe_code = raw_code(tag);
  if (maybe_code == Smi::zero()) return false;
  *code_out = Code::cast(maybe_code);
  return true;
}

bool InstructionStream::TryGetCodeUnchecked(Code* code_out,
                                            AcquireLoadTag tag) const {
  Object maybe_code = raw_code(tag);
  if (maybe_code == Smi::zero()) return false;
  *code_out = Code::unchecked_cast(maybe_code);
  return true;
}

void InstructionStream::initialize_code_to_smi_zero(ReleaseStoreTag) {
  TaggedField<Object, kCodeOffset>::Release_Store(*this, Smi::zero());
}

// TODO(v8:13788): load base value from respective scheme class and drop
// the kMainCageBaseUpper32BitsOffset field.

PtrComprCageBase InstructionStream::main_cage_base() const {
#ifdef V8_EXTERNAL_CODE_SPACE
  Address cage_base_hi = ReadField<Tagged_t>(kMainCageBaseUpper32BitsOffset);
  return PtrComprCageBase(cage_base_hi << 32);
#else
  return GetPtrComprCageBase(*this);
#endif
}

// TODO(v8:13788): load base value from respective scheme class and drop
// the kMainCageBaseUpper32BitsOffset field.
PtrComprCageBase InstructionStream::main_cage_base(RelaxedLoadTag) const {
#ifdef V8_EXTERNAL_CODE_SPACE
  Address cage_base_hi =
      Relaxed_ReadField<Tagged_t>(kMainCageBaseUpper32BitsOffset);
  return PtrComprCageBase(cage_base_hi << 32);
#else
  return GetPtrComprCageBase(*this);
#endif
}

void InstructionStream::set_main_cage_base(Address cage_base, RelaxedStoreTag) {
#ifdef V8_EXTERNAL_CODE_SPACE
  Tagged_t cage_base_hi = static_cast<Tagged_t>(cage_base >> 32);
  Relaxed_WriteField<Tagged_t>(kMainCageBaseUpper32BitsOffset, cage_base_hi);
#else
  UNREACHABLE();
#endif
}

// TODO(jgruber): Remove this method once main_cage_base is gone.
void InstructionStream::WipeOutHeader() {
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    set_main_cage_base(kNullAddress, kRelaxedStore);
  }
}

Address InstructionStream::instruction_start() const {
  return field_address(kHeaderSize);
}

ByteArray InstructionStream::unchecked_relocation_info() const {
  PtrComprCageBase cage_base = main_cage_base(kRelaxedLoad);
  return ByteArray::unchecked_cast(
      TaggedField<HeapObject, kRelocationInfoOffset>::Acquire_Load(cage_base,
                                                                   *this));
}

byte* InstructionStream::relocation_start() const {
  return relocation_info().GetDataStartAddress();
}

byte* InstructionStream::relocation_end() const {
  return relocation_info().GetDataEndAddress();
}

int InstructionStream::relocation_size() const {
  return relocation_info().length();
}

int InstructionStream::Size() const { return SizeFor(body_size()); }

void InstructionStream::clear_padding() {
  // Header padding.
  memset(reinterpret_cast<void*>(address() + kUnalignedSize), 0,
         kHeaderSize - kUnalignedSize);
  // Trailing padding.
  memset(reinterpret_cast<void*>(body_end()), 0,
         TrailingPaddingSizeFor(body_size()));
}

// static
InstructionStream InstructionStream::FromTargetAddress(Address address) {
  {
    // TODO(jgruber,v8:6666): Support embedded builtins here. We'd need to pass
    // in the current isolate.
    Address start =
        reinterpret_cast<Address>(Isolate::CurrentEmbeddedBlobCode());
    Address end = start + Isolate::CurrentEmbeddedBlobCodeSize();
    CHECK(address < start || address >= end);
  }

  HeapObject code =
      HeapObject::FromAddress(address - InstructionStream::kHeaderSize);
  // Unchecked cast because we can't rely on the map currently not being a
  // forwarding pointer.
  return InstructionStream::unchecked_cast(code);
}

// static
InstructionStream InstructionStream::FromEntryAddress(
    Address location_of_address) {
  Address code_entry = base::Memory<Address>(location_of_address);
  HeapObject code =
      HeapObject::FromAddress(code_entry - InstructionStream::kHeaderSize);
  // Unchecked cast because we can't rely on the map currently not being a
  // forwarding pointer.
  return InstructionStream::unchecked_cast(code);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INSTRUCTION_STREAM_INL_H_
