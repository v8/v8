// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INSTRUCTION_STREAM_INL_H_
#define V8_OBJECTS_INSTRUCTION_STREAM_INL_H_

#include "src/common/ptr-compr-inl.h"
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
DEF_PRIMITIVE_ACCESSORS(InstructionStream, body_size, kBodySizeOffset, uint32_t)

// TODO(sroettger): remove unused setter functions once all code writes go
// through the WritableJitAllocation, e.g. the body_size setter above.
void InstructionStream::Initialize(uint32_t body_size,
                                   ByteArray reloc_info) const {
  {
    ThreadIsolation::WritableJitAllocation writable_allocation =
        ThreadIsolation::RegisterInstructionStreamAllocation(
            address(), InstructionStream::SizeFor(body_size));
    CHECK_EQ(InstructionStream::SizeFor(body_size), writable_allocation.size());
    writable_allocation.WriteHeaderSlot<uint32_t, kBodySizeOffset>(body_size);

    // During the Code initialization process, InstructionStream::code is
    // briefly unset (the Code object has not been allocated yet). In this state
    // it is only visible through heap iteration.
    writable_allocation.WriteHeaderSlot<Smi, kCodeOffset>(Smi::zero());

    DCHECK(!ObjectInYoungGeneration(reloc_info));
    writable_allocation.WriteHeaderSlot<ByteArray, kRelocationInfoOffset>(
        reloc_info);

    // Clear header padding
    writable_allocation.ClearBytes(kUnalignedSize,
                                   kHeaderSize - kUnalignedSize);
    // Clear trailing padding.
    writable_allocation.ClearBytes(kHeaderSize + body_size,
                                   TrailingPaddingSizeFor(body_size));
  }

  // We want to keep the code minimal that runs with write access to a JIT
  // allocation, so trigger the write barriers after the WritableJitAllocation
  // went out of scope.
  CONDITIONAL_WRITE_BARRIER(*this, kRelocationInfoOffset, reloc_info,
                            UPDATE_WRITE_BARRIER);
}

Address InstructionStream::body_end() const {
  static_assert(kOnHeapBodyIsContiguous);
  return instruction_start() + body_size();
}

Object InstructionStream::raw_code(AcquireLoadTag tag) const {
  PtrComprCageBase cage_base = main_cage_base();
  Object value =
      TaggedField<Object, kCodeOffset>::Acquire_Load(cage_base, *this);
  DCHECK(!ObjectInYoungGeneration(value));
  return value;
}

Code InstructionStream::code(AcquireLoadTag tag) const {
  return Code::cast(raw_code(tag));
}

void InstructionStream::set_code(Code value, ReleaseStoreTag) {
  DCHECK(!ObjectInYoungGeneration(value));
  TaggedField<Code, kCodeOffset>::Release_Store(*this, value);
  CONDITIONAL_WRITE_BARRIER(*this, kCodeOffset, value, UPDATE_WRITE_BARRIER);
}

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

ByteArray InstructionStream::relocation_info() const {
  PtrComprCageBase cage_base = main_cage_base();
  ByteArray value =
      TaggedField<ByteArray, kRelocationInfoOffset>::load(cage_base, *this);
  DCHECK(!ObjectInYoungGeneration(value));
  return value;
}

Address InstructionStream::instruction_start() const {
  return field_address(kHeaderSize);
}

ByteArray InstructionStream::unchecked_relocation_info() const {
  PtrComprCageBase cage_base = main_cage_base();
  return ByteArray::unchecked_cast(
      TaggedField<HeapObject, kRelocationInfoOffset>::Acquire_Load(cage_base,
                                                                   *this));
}

uint8_t* InstructionStream::relocation_start() const {
  return relocation_info()->GetDataStartAddress();
}

uint8_t* InstructionStream::relocation_end() const {
  return relocation_info()->GetDataEndAddress();
}

int InstructionStream::relocation_size() const {
  return relocation_info()->length();
}

int InstructionStream::Size() const { return SizeFor(body_size()); }

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

// static
PtrComprCageBase InstructionStream::main_cage_base() {
#ifdef V8_COMPRESS_POINTERS
  return PtrComprCageBase{V8HeapCompressionScheme::base()};
#else
  return PtrComprCageBase{};
#endif
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_INSTRUCTION_STREAM_INL_H_
