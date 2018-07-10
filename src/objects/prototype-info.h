// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_PROTOTYPE_INFO_H_
#define V8_OBJECTS_PROTOTYPE_INFO_H_

#include "src/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// Container for metadata stored on each prototype map.
class PrototypeInfo : public Struct {
 public:
  static const int UNREGISTERED = -1;

  // [weak_cell]: A WeakCell containing this prototype. ICs cache the cell here.
  DECL_ACCESSORS(weak_cell, Object)

  // [prototype_users]: FixedArrayOfWeakCells containing maps using this
  // prototype, or Smi(0) if uninitialized.
  DECL_ACCESSORS(prototype_users, Object)

  // [object_create_map]: A field caching the map for Object.create(prototype).
  static inline void SetObjectCreateMap(Handle<PrototypeInfo> info,
                                        Handle<Map> map);
  inline Map* ObjectCreateMap();
  inline bool HasObjectCreateMap();

  // [registry_slot]: Slot in prototype's user registry where this user
  // is stored. Returns UNREGISTERED if this prototype has not been registered.
  inline int registry_slot() const;
  inline void set_registry_slot(int slot);

  // [bit_field]
  inline int bit_field() const;
  inline void set_bit_field(int bit_field);

  DECL_BOOLEAN_ACCESSORS(should_be_fast_map)

  DECL_CAST(PrototypeInfo)

  // Dispatched behavior.
  DECL_PRINTER(PrototypeInfo)
  DECL_VERIFIER(PrototypeInfo)

  static const int kWeakCellOffset = HeapObject::kHeaderSize;
  static const int kPrototypeUsersOffset = kWeakCellOffset + kPointerSize;
  static const int kRegistrySlotOffset = kPrototypeUsersOffset + kPointerSize;
  static const int kValidityCellOffset = kRegistrySlotOffset + kPointerSize;
  static const int kObjectCreateMapOffset = kValidityCellOffset + kPointerSize;
  static const int kBitFieldOffset = kObjectCreateMapOffset + kPointerSize;
  static const int kSize = kBitFieldOffset + kPointerSize;

  // Bit field usage.
  static const int kShouldBeFastBit = 0;

  class BodyDescriptor;

 private:
  DECL_ACCESSORS(object_create_map, MaybeObject)

  DISALLOW_IMPLICIT_CONSTRUCTORS(PrototypeInfo);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_PROTOTYPE_INFO_H_
