// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TYPES_INL_H_
#define V8_TYPES_INL_H_

#include "src/types.h"

#include "src/factory.h"
#include "src/handles-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// TypeImpl

template<class Config>
typename TypeImpl<Config>::bitset TypeImpl<Config>::BitsetType::SignedSmall() {
  return i::SmiValuesAre31Bits() ? kSigned31 : kSigned32;
}


template<class Config>
typename TypeImpl<Config>::bitset
TypeImpl<Config>::BitsetType::UnsignedSmall() {
  return i::SmiValuesAre31Bits() ? kUnsigned30 : kUnsigned31;
}


#define CONSTRUCT_SIMD_TYPE(NAME, Name, name, lane_count, lane_type) \
template<class Config> \
typename TypeImpl<Config>::TypeHandle TypeImpl<Config>::Name( \
    Isolate* isolate, Region* region) { \
  return Class(i::handle(isolate->heap()->name##_map()), region); \
}
SIMD128_TYPES(CONSTRUCT_SIMD_TYPE)
#undef CONSTRUCT_SIMD_TYPE


template<class Config>
TypeImpl<Config>* TypeImpl<Config>::cast(typename Config::Base* object) {
  TypeImpl* t = static_cast<TypeImpl*>(object);
  DCHECK(t->IsBitset() || t->IsClass() || t->IsConstant() || t->IsRange() ||
         t->IsUnion() || t->IsArray() || t->IsFunction() || t->IsContext());
  return t;
}


// Most precise _current_ type of a value (usually its class).
template<class Config>
typename TypeImpl<Config>::TypeHandle TypeImpl<Config>::NowOf(
    i::Object* value, Region* region) {
  if (value->IsSmi() ||
      i::HeapObject::cast(value)->map()->instance_type() == HEAP_NUMBER_TYPE) {
    return Of(value, region);
  }
  return Class(i::handle(i::HeapObject::cast(value)->map()), region);
}


template<class Config>
bool TypeImpl<Config>::NowContains(i::Object* value) {
  DisallowHeapAllocation no_allocation;
  if (this->IsAny()) return true;
  if (value->IsHeapObject()) {
    i::Map* map = i::HeapObject::cast(value)->map();
    for (Iterator<i::Map> it = this->Classes(); !it.Done(); it.Advance()) {
      if (*it.Current() == map) return true;
    }
  }
  return this->Contains(value);
}


// -----------------------------------------------------------------------------
// ZoneTypeConfig

// static
template<class T>
T* ZoneTypeConfig::handle(T* type) {
  return type;
}


// static
template<class T>
T* ZoneTypeConfig::cast(Type* type) {
  return static_cast<T*>(type);
}


// static
bool ZoneTypeConfig::is_bitset(Type* type) {
  return reinterpret_cast<uintptr_t>(type) & 1;
}


// static
bool ZoneTypeConfig::is_struct(Type* type, int tag) {
  DCHECK(tag != kRangeStructTag);
  if (is_bitset(type)) return false;
  int type_tag = struct_tag(as_struct(type));
  return type_tag == tag;
}


// static
bool ZoneTypeConfig::is_range(Type* type) {
  if (is_bitset(type)) return false;
  int type_tag = struct_tag(as_struct(type));
  return type_tag == kRangeStructTag;
}


// static
bool ZoneTypeConfig::is_class(Type* type) {
  return false;
}


// static
ZoneTypeConfig::Type::bitset ZoneTypeConfig::as_bitset(Type* type) {
  DCHECK(is_bitset(type));
  return static_cast<Type::bitset>(reinterpret_cast<uintptr_t>(type) ^ 1u);
}


// static
ZoneTypeConfig::Struct* ZoneTypeConfig::as_struct(Type* type) {
  DCHECK(!is_bitset(type));
  return reinterpret_cast<Struct*>(type);
}


// static
ZoneTypeConfig::Range* ZoneTypeConfig::as_range(Type* type) {
  DCHECK(!is_bitset(type));
  return reinterpret_cast<Range*>(type);
}


// static
i::Handle<i::Map> ZoneTypeConfig::as_class(Type* type) {
  UNREACHABLE();
  return i::Handle<i::Map>();
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_bitset(Type::bitset bitset) {
  return reinterpret_cast<Type*>(static_cast<uintptr_t>(bitset | 1u));
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_bitset(
    Type::bitset bitset, Zone* Zone) {
  return from_bitset(bitset);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_struct(Struct* structure) {
  return reinterpret_cast<Type*>(structure);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_range(Range* range) {
  return reinterpret_cast<Type*>(range);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_class(
    i::Handle<i::Map> map, Zone* zone) {
  return from_bitset(0);
}


// static
ZoneTypeConfig::Struct* ZoneTypeConfig::struct_create(
    int tag, int length, Zone* zone) {
  DCHECK(tag != kRangeStructTag);
  Struct* structure = reinterpret_cast<Struct*>(
      zone->New(sizeof(void*) * (length + 2)));  // NOLINT
  structure[0] = reinterpret_cast<void*>(tag);
  structure[1] = reinterpret_cast<void*>(length);
  return structure;
}


// static
void ZoneTypeConfig::struct_shrink(Struct* structure, int length) {
  DCHECK(0 <= length && length <= struct_length(structure));
  structure[1] = reinterpret_cast<void*>(length);
}


// static
int ZoneTypeConfig::struct_tag(Struct* structure) {
  return static_cast<int>(reinterpret_cast<intptr_t>(structure[0]));
}


// static
int ZoneTypeConfig::struct_length(Struct* structure) {
  return static_cast<int>(reinterpret_cast<intptr_t>(structure[1]));
}


// static
Type* ZoneTypeConfig::struct_get(Struct* structure, int i) {
  DCHECK(0 <= i && i <= struct_length(structure));
  return static_cast<Type*>(structure[2 + i]);
}


// static
void ZoneTypeConfig::struct_set(Struct* structure, int i, Type* x) {
  DCHECK(0 <= i && i <= struct_length(structure));
  structure[2 + i] = x;
}


// static
template<class V>
i::Handle<V> ZoneTypeConfig::struct_get_value(Struct* structure, int i) {
  DCHECK(0 <= i && i <= struct_length(structure));
  return i::Handle<V>(static_cast<V**>(structure[2 + i]));
}


// static
template<class V>
void ZoneTypeConfig::struct_set_value(
    Struct* structure, int i, i::Handle<V> x) {
  DCHECK(0 <= i && i <= struct_length(structure));
  structure[2 + i] = x.location();
}


// static
ZoneTypeConfig::Range* ZoneTypeConfig::range_create(Zone* zone) {
  Range* range = reinterpret_cast<Range*>(zone->New(sizeof(Range)));  // NOLINT
  range->tag = reinterpret_cast<void*>(kRangeStructTag);
  range->bitset = 0;
  range->limits[0] = 1;
  range->limits[1] = 0;
  return range;
}


// static
int ZoneTypeConfig::range_get_bitset(ZoneTypeConfig::Range* range) {
  return range->bitset;
}


// static
void ZoneTypeConfig::range_set_bitset(ZoneTypeConfig::Range* range, int value) {
  range->bitset = value;
}


// static
double ZoneTypeConfig::range_get_double(ZoneTypeConfig::Range* range,
                                        int index) {
  DCHECK(index >= 0 && index < 2);
  return range->limits[index];
}


// static
void ZoneTypeConfig::range_set_double(ZoneTypeConfig::Range* range, int index,
                                      double value, Zone*) {
  DCHECK(index >= 0 && index < 2);
  range->limits[index] = value;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TYPES_INL_H_
