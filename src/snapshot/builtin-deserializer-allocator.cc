// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/builtin-deserializer-allocator.h"

#include "src/heap/heap-inl.h"
#include "src/interpreter/interpreter.h"
#include "src/snapshot/builtin-deserializer.h"
#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

using interpreter::Bytecodes;
using interpreter::Interpreter;

BuiltinDeserializerAllocator::BuiltinDeserializerAllocator(
    Deserializer<BuiltinDeserializerAllocator>* deserializer)
    : deserializer_(deserializer) {}

Address BuiltinDeserializerAllocator::Allocate(AllocationSpace space,
                                               int size) {
  const int code_object_id = deserializer()->CurrentCodeObjectId();
  DCHECK_NE(BuiltinDeserializer::kNoCodeObjectId, code_object_id);
  DCHECK_EQ(CODE_SPACE, space);
  DCHECK_EQ(deserializer()->ExtractCodeObjectSize(code_object_id), size);
#ifdef DEBUG
  RegisterCodeObjectAllocation(code_object_id);
#endif

  DCHECK(Builtins::IsBuiltinId(code_object_id));
  Object* obj = isolate()->builtins()->builtin(code_object_id);
  DCHECK(Internals::HasHeapObjectTag(reinterpret_cast<Address>(obj)));
  return HeapObject::cast(obj)->address();
}

Heap::Reservation
BuiltinDeserializerAllocator::CreateReservationsForEagerBuiltins() {
  Heap::Reservation result;

  // Reservations for builtins.

  for (int i = 0; i < Builtins::builtin_count; i++) {
    uint32_t builtin_size = deserializer()->ExtractCodeObjectSize(i);
    DCHECK_LE(builtin_size, MemoryChunkLayout::AllocatableMemoryInCodePage());
    result.push_back({builtin_size, kNullAddress, kNullAddress});
  }

  return result;
}

void BuiltinDeserializerAllocator::InitializeBuiltinFromReservation(
    const Heap::Chunk& chunk, int builtin_id) {
  DCHECK_EQ(deserializer()->ExtractCodeObjectSize(builtin_id), chunk.size);
  DCHECK_EQ(chunk.size, chunk.end - chunk.start);

  SkipList::Update(chunk.start, chunk.size);
  isolate()->builtins()->set_builtin(builtin_id,
                                     HeapObject::FromAddress(chunk.start));

#ifdef DEBUG
  RegisterCodeObjectReservation(builtin_id);
#endif
}

void BuiltinDeserializerAllocator::InitializeFromReservations(
    const Heap::Reservation& reservation) {
  DCHECK(!AllowHeapAllocation::IsAllowed());

  // Initialize the builtins table.
  for (int i = 0; i < Builtins::builtin_count; i++) {
    InitializeBuiltinFromReservation(reservation[i], i);
  }
}

#ifdef DEBUG
void BuiltinDeserializerAllocator::RegisterCodeObjectReservation(
    int code_object_id) {
  const auto result = unused_reservations_.emplace(code_object_id);
  CHECK(result.second);  // False, iff builtin_id was already present in set.
}

void BuiltinDeserializerAllocator::RegisterCodeObjectAllocation(
    int code_object_id) {
  const size_t removed_elems = unused_reservations_.erase(code_object_id);
  CHECK_EQ(removed_elems, 1);
}

bool BuiltinDeserializerAllocator::ReservationsAreFullyUsed() const {
  // Not 100% precise but should be good enough.
  return unused_reservations_.empty();
}
#endif  // DEBUG

Isolate* BuiltinDeserializerAllocator::isolate() const {
  return deserializer()->isolate();
}

BuiltinDeserializer* BuiltinDeserializerAllocator::deserializer() const {
  return static_cast<BuiltinDeserializer*>(deserializer_);
}

}  // namespace internal
}  // namespace v8
