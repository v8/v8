// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/builtin-deserializer-allocator.h"

#include "src/heap/heap-inl.h"
#include "src/snapshot/builtin-deserializer.h"
#include "src/snapshot/deserializer.h"

namespace v8 {
namespace internal {

BuiltinDeserializerAllocator::BuiltinDeserializerAllocator(
    Deserializer<BuiltinDeserializerAllocator>* deserializer)
    : deserializer_(deserializer) {}

Address BuiltinDeserializerAllocator::Allocate(AllocationSpace space,
                                               int size) {
  const int builtin_id = deserializer()->CurrentBuiltinId();
  DCHECK_EQ(CODE_SPACE, space);
  DCHECK_EQ(deserializer()->ExtractBuiltinSize(builtin_id), size);
#ifdef DEBUG
  RegisterBuiltinAllocation(builtin_id);
#endif
  Object* obj = isolate()->builtins()->builtin(builtin_id);
  DCHECK(Internals::HasHeapObjectTag(obj));
  return HeapObject::cast(obj)->address();
}

Heap::Reservation
BuiltinDeserializerAllocator::CreateReservationsForEagerBuiltins() {
  Heap::Reservation result;

  // DeserializeLazy is always the first reservation (to simplify logic in
  // InitializeBuiltinsTable).
  {
    DCHECK(!Builtins::IsLazy(Builtins::kDeserializeLazy));
    uint32_t builtin_size =
        deserializer()->ExtractBuiltinSize(Builtins::kDeserializeLazy);
    DCHECK_LE(builtin_size, MemoryAllocator::PageAreaSize(CODE_SPACE));
    result.push_back({builtin_size, nullptr, nullptr});
  }

  for (int i = 0; i < Builtins::builtin_count; i++) {
    if (i == Builtins::kDeserializeLazy) continue;

    // Skip lazy builtins. These will be replaced by the DeserializeLazy code
    // object in InitializeBuiltinsTable and thus require no reserved space.
    if (deserializer()->IsLazyDeserializationEnabled() && Builtins::IsLazy(i)) {
      continue;
    }

    uint32_t builtin_size = deserializer()->ExtractBuiltinSize(i);
    DCHECK_LE(builtin_size, MemoryAllocator::PageAreaSize(CODE_SPACE));
    result.push_back({builtin_size, nullptr, nullptr});
  }

  return result;
}

void BuiltinDeserializerAllocator::InitializeBuiltinFromReservation(
    const Heap::Chunk& chunk, int builtin_id) {
  DCHECK_EQ(deserializer()->ExtractBuiltinSize(builtin_id), chunk.size);
  DCHECK_EQ(chunk.size, chunk.end - chunk.start);

  SkipList::Update(chunk.start, chunk.size);
  isolate()->builtins()->set_builtin(builtin_id,
                                     HeapObject::FromAddress(chunk.start));

#ifdef DEBUG
  RegisterBuiltinReservation(builtin_id);
#endif
}

void BuiltinDeserializerAllocator::InitializeBuiltinsTable(
    const Heap::Reservation& reservation) {
  DCHECK(!AllowHeapAllocation::IsAllowed());

  Builtins* builtins = isolate()->builtins();
  int reservation_index = 0;

  // Other builtins can be replaced by DeserializeLazy so it may not be lazy.
  // It always occupies the first reservation slot.
  {
    DCHECK(!Builtins::IsLazy(Builtins::kDeserializeLazy));
    InitializeBuiltinFromReservation(reservation[reservation_index],
                                     Builtins::kDeserializeLazy);
    reservation_index++;
  }

  Code* deserialize_lazy = builtins->builtin(Builtins::kDeserializeLazy);

  for (int i = 0; i < Builtins::builtin_count; i++) {
    if (i == Builtins::kDeserializeLazy) continue;

    if (deserializer()->IsLazyDeserializationEnabled() && Builtins::IsLazy(i)) {
      builtins->set_builtin(i, deserialize_lazy);
    } else {
      InitializeBuiltinFromReservation(reservation[reservation_index], i);
      reservation_index++;
    }
  }

  DCHECK_EQ(reservation.size(), reservation_index);
}

void BuiltinDeserializerAllocator::ReserveAndInitializeBuiltinsTableForBuiltin(
    int builtin_id) {
  DCHECK(AllowHeapAllocation::IsAllowed());
  DCHECK(isolate()->builtins()->is_initialized());
  DCHECK(Builtins::IsBuiltinId(builtin_id));
  DCHECK_NE(Builtins::kDeserializeLazy, builtin_id);
  DCHECK_EQ(Builtins::kDeserializeLazy,
            isolate()->builtins()->builtin(builtin_id)->builtin_index());

  const uint32_t builtin_size = deserializer()->ExtractBuiltinSize(builtin_id);
  DCHECK_LE(builtin_size, MemoryAllocator::PageAreaSize(CODE_SPACE));

  Handle<HeapObject> o =
      isolate()->factory()->NewCodeForDeserialization(builtin_size);

  // Note: After this point and until deserialization finishes, heap allocation
  // is disallowed. We currently can't safely assert this since we'd need to
  // pass the DisallowHeapAllocation scope out of this function.

  // Write the allocated filler object into the builtins table. It will be
  // returned by our custom Allocate method below once needed.

  isolate()->builtins()->set_builtin(builtin_id, *o);

#ifdef DEBUG
  RegisterBuiltinReservation(builtin_id);
#endif
}

#ifdef DEBUG
void BuiltinDeserializerAllocator::RegisterBuiltinReservation(int builtin_id) {
  const auto result = unused_reservations_.emplace(builtin_id);
  CHECK(result.second);  // False, iff builtin_id was already present in set.
}

void BuiltinDeserializerAllocator::RegisterBuiltinAllocation(int builtin_id) {
  const size_t removed_elems = unused_reservations_.erase(builtin_id);
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
