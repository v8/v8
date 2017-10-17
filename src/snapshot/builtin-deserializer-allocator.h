// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_BUILTIN_DESERIALIZER_ALLOCATOR_H_
#define V8_SNAPSHOT_BUILTIN_DESERIALIZER_ALLOCATOR_H_

#include <unordered_set>

#include "src/globals.h"
#include "src/heap/heap.h"
#include "src/snapshot/serializer-common.h"

namespace v8 {
namespace internal {

template <class AllocatorT>
class Deserializer;

class BuiltinDeserializer;

class BuiltinDeserializerAllocator final {
 public:
  BuiltinDeserializerAllocator(
      Deserializer<BuiltinDeserializerAllocator>* deserializer);

  // ------- Allocation Methods -------
  // Methods related to memory allocation during deserialization.

  // Allocation works differently here than in other deserializers. Instead of
  // a statically-known memory area determined at serialization-time, our
  // memory requirements here are determined at runtime. Another major
  // difference is that we create builtin Code objects up-front (before
  // deserialization) in order to avoid having to patch builtin references
  // later on. See also the kBuiltin case in deserializer.cc.
  //
  // Allocate simply returns the pre-allocated object prepared by
  // InitializeBuiltinsTable.
  Address Allocate(AllocationSpace space, int size);

  void MoveToNextChunk(AllocationSpace space) { UNREACHABLE(); }
  void SetAlignment(AllocationAlignment alignment) { UNREACHABLE(); }

  HeapObject* GetMap(uint32_t index) { UNREACHABLE(); }
  HeapObject* GetLargeObject(uint32_t index) { UNREACHABLE(); }
  HeapObject* GetObject(AllocationSpace space, uint32_t chunk_index,
                        uint32_t chunk_offset) {
    UNREACHABLE();
  }

  // ------- Reservation Methods -------
  // Methods related to memory reservations (prior to deserialization).

  // Builtin deserialization does not bake reservations into the snapshot, hence
  // this is a nop.
  void DecodeReservation(Vector<const SerializedData::Reservation> res) {}

  // These methods are used to pre-allocate builtin objects prior to
  // deserialization.
  // TODO(jgruber): Refactor reservation/allocation logic in deserializers to
  // make this less messy.
  Heap::Reservation CreateReservationsForEagerBuiltins();
  void InitializeBuiltinsTable(const Heap::Reservation& reservation);

  // Creates reservations and initializes the builtins table in preparation for
  // lazily deserializing a single builtin.
  void ReserveAndInitializeBuiltinsTableForBuiltin(int builtin_id);

#ifdef DEBUG
  bool ReservationsAreFullyUsed() const;
#endif

  // For SortMapDescriptors();
  const std::vector<Address>& GetAllocatedMaps() const {
    static std::vector<Address> empty_vector(0);
    return empty_vector;
  }

 private:
  Isolate* isolate() const;

  // Used after memory allocation prior to isolate initialization, to register
  // the newly created object in code space and add it to the builtins table.
  void InitializeBuiltinFromReservation(const Heap::Chunk& chunk,
                                        int builtin_id);

#ifdef DEBUG
  void RegisterBuiltinReservation(int builtin_id);
  void RegisterBuiltinAllocation(int builtin_id);
  std::unordered_set<int> unused_reservations_;
#endif

 private:
  // The current deserializer.
  BuiltinDeserializer* const deserializer_;

  DISALLOW_COPY_AND_ASSIGN(BuiltinDeserializerAllocator)
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SNAPSHOT_BUILTIN_DESERIALIZER_ALLOCATOR_H_
