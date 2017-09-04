// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/builtin-deserializer.h"

#include "src/objects-inl.h"
#include "src/snapshot/snapshot.h"

namespace v8 {
namespace internal {

// Tracks the builtin currently being deserialized (required for allocation).
class DeserializingBuiltinScope {
 public:
  DeserializingBuiltinScope(BuiltinDeserializer* builtin_deserializer,
                            int builtin_id)
      : builtin_deserializer_(builtin_deserializer) {
    DCHECK_EQ(BuiltinDeserializer::kNoBuiltinId,
              builtin_deserializer->current_builtin_id_);
    builtin_deserializer->current_builtin_id_ = builtin_id;
  }

  ~DeserializingBuiltinScope() {
    builtin_deserializer_->current_builtin_id_ =
        BuiltinDeserializer::kNoBuiltinId;
  }

 private:
  BuiltinDeserializer* builtin_deserializer_;

  DISALLOW_COPY_AND_ASSIGN(DeserializingBuiltinScope)
};

BuiltinDeserializer::BuiltinDeserializer(const BuiltinSnapshotData* data)
    : Deserializer(data, false) {
  // We may have to relax this at some point to pack reloc infos and handler
  // tables into the builtin blob (instead of the partial snapshot cache).
  DCHECK(ReservesOnlyCodeSpace());

  builtin_offsets_ = data->BuiltinOffsets();
  DCHECK_EQ(Builtins::builtin_count, builtin_offsets_.length());
  DCHECK(std::is_sorted(builtin_offsets_.begin(), builtin_offsets_.end()));

  builtin_sizes_ = ExtractBuiltinSizes();
  DCHECK_EQ(Builtins::builtin_count, builtin_sizes_.size());
}

void BuiltinDeserializer::DeserializeEagerBuiltins() {
  DCHECK(!AllowHeapAllocation::IsAllowed());
  DCHECK_EQ(0, source()->position());

  // TODO(jgruber): Replace lazy builtins with DeserializeLazy.

  Builtins* builtins = isolate()->builtins();
  for (int i = 0; i < Builtins::builtin_count; i++) {
    builtins->set_builtin(i, DeserializeBuiltin(i));
  }

#ifdef DEBUG
  for (int i = 0; i < Builtins::builtin_count; i++) {
    Object* o = builtins->builtin(static_cast<Builtins::Name>(i));
    DCHECK(o->IsCode() && Code::cast(o)->is_builtin());
  }
#endif
}

Code* BuiltinDeserializer::DeserializeBuiltin(int builtin_id) {
  DCHECK(!AllowHeapAllocation::IsAllowed());
  DCHECK(Builtins::IsBuiltinId(builtin_id));

  DeserializingBuiltinScope scope(this, builtin_id);

  const int initial_position = source()->position();
  const uint32_t offset = builtin_offsets_[builtin_id];
  source()->set_position(offset);

  Object* o = ReadDataSingle();
  DCHECK(o->IsCode() && Code::cast(o)->is_builtin());

  // Rewind.
  source()->set_position(initial_position);

  return Code::cast(o);
}

uint32_t BuiltinDeserializer::ExtractBuiltinSize(int builtin_id) {
  DCHECK(Builtins::IsBuiltinId(builtin_id));

  const int initial_position = source()->position();
  const uint32_t offset = builtin_offsets_[builtin_id];
  source()->set_position(offset);

  // Grab the size of the code object.
  byte data = source()->Get();

  // The first bytecode can either be kNewObject, or kNextChunk if the current
  // chunk has been exhausted. Since we do allocations differently here, we
  // don't care about kNextChunk and can simply skip over it.
  // TODO(jgruber): When refactoring (de)serializer allocations, ensure we don't
  // generate kNextChunk bytecodes anymore for the builtins snapshot. In fact,
  // the entire reservations mechanism is unused for the builtins snapshot.
  if (data == kNextChunk) {
    source()->Get();  // Skip over kNextChunk's {space} parameter.
    data = source()->Get();
  }

  DCHECK_EQ(kNewObject | kPlain | kStartOfObject | CODE_SPACE, data);
  const uint32_t result = source()->GetInt() << kObjectAlignmentBits;

  // Rewind.
  source()->set_position(initial_position);

  return result;
}

std::vector<uint32_t> BuiltinDeserializer::ExtractBuiltinSizes() {
  std::vector<uint32_t> result;
  result.reserve(Builtins::builtin_count);
  for (int i = 0; i < Builtins::builtin_count; i++) {
    result.push_back(ExtractBuiltinSize(i));
  }
  return result;
}

Heap::Reservation BuiltinDeserializer::CreateReservationsForEagerBuiltins() {
  DCHECK(ReservesOnlyCodeSpace());

  Heap::Reservation result;
  for (int i = 0; i < Builtins::builtin_count; i++) {
    // TODO(jgruber): Skip lazy builtins.

    const uint32_t builtin_size = builtin_sizes_[i];
    DCHECK_LE(builtin_size, MemoryAllocator::PageAreaSize(CODE_SPACE));
    result.push_back({builtin_size, nullptr, nullptr});
  }

  return result;
}

void BuiltinDeserializer::InitializeBuiltinsTable(
    const Heap::Reservation& reservation) {
  DCHECK(!AllowHeapAllocation::IsAllowed());

  // Other builtins can be replaced by DeserializeLazy so it may not be lazy.
  DCHECK(!Builtins::IsLazy(Builtins::kDeserializeLazy));

  Builtins* builtins = isolate()->builtins();
  int reservation_index = 0;

  for (int i = 0; i < Builtins::builtin_count; i++) {
    // TODO(jgruber): Replace lazy builtins with DeserializeLazy.

    Address start = reservation[reservation_index].start;
    DCHECK_EQ(builtin_sizes_[i], reservation[reservation_index].size);
    DCHECK_EQ(builtin_sizes_[i], reservation[reservation_index].end - start);

    builtins->set_builtin(i, HeapObject::FromAddress(start));

    reservation_index++;
  }

  DCHECK_EQ(reservation.size(), reservation_index);
}

Address BuiltinDeserializer::Allocate(int space_index, int size) {
  DCHECK_EQ(CODE_SPACE, space_index);
  DCHECK_EQ(ExtractBuiltinSize(current_builtin_id_), size);
  Object* obj = isolate()->builtins()->builtin(
      static_cast<Builtins::Name>(current_builtin_id_));
  DCHECK(Internals::HasHeapObjectTag(obj));
  Address address = HeapObject::cast(obj)->address();
  SkipList::Update(address, size);
  return address;
}

}  // namespace internal
}  // namespace v8
