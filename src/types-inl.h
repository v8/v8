// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TYPES_INL_H_
#define V8_TYPES_INL_H_

#include "types.h"

#include "factory.h"
#include "handles-inl.h"

namespace v8 {
namespace internal {

// static
ZoneTypeConfig::Tagged* ZoneTypeConfig::tagged_create(
    Tag tag, int size, Zone* zone) {
  Tagged* tagged = new(zone) Tagged(size + 1, zone);
  tagged->Add(reinterpret_cast<void*>(tag), zone);
  tagged->AddBlock(NULL, size, zone);
  return tagged;
}


// static
void ZoneTypeConfig::tagged_shrink(Tagged* tagged, int size) {
  tagged->Rewind(size + 1);
}


// static
ZoneTypeConfig::Tag ZoneTypeConfig::tagged_tag(Tagged* tagged) {
  return static_cast<Tag>(reinterpret_cast<intptr_t>(tagged->at(0)));
}


// static
template<class T>
T ZoneTypeConfig::tagged_get(Tagged* tagged, int i) {
  return reinterpret_cast<T>(tagged->at(i + 1));
}


// static
template<class T>
void ZoneTypeConfig::tagged_set(Tagged* tagged, int i, T value) {
  tagged->at(i + 1) = reinterpret_cast<void*>(value);
}


// static
int ZoneTypeConfig::tagged_length(Tagged* tagged) {
  return tagged->length() - 1;
}


// static
Type* ZoneTypeConfig::handle(Type* type) {
  return type;
}


// static
bool ZoneTypeConfig::is(Type* type, Tag tag) {
  return is_tagged(type) && tagged_tag(as_tagged(type)) == tag;
}


// static
bool ZoneTypeConfig::is_bitset(Type* type) {
  return reinterpret_cast<intptr_t>(type) & 1;
}


// static
bool ZoneTypeConfig::is_tagged(Type* type) {
  return !is_bitset(type);
}


// static
bool ZoneTypeConfig::is_class(Type* type) {
  return is(type, kClassTag);
}


// static
bool ZoneTypeConfig::is_constant(Type* type) {
  return is(type, kConstantTag);
}


// static
bool ZoneTypeConfig::is_union(Type* type) {
  return is(type, kUnionTag);
}


// static
bool ZoneTypeConfig::tagged_is_union(Tagged* tagged) {
  return is(from_tagged(tagged), kUnionTag);
}


// static
int ZoneTypeConfig::as_bitset(Type* type) {
  ASSERT(is_bitset(type));
  return static_cast<int>(reinterpret_cast<intptr_t>(type) >> 1);
}


// static
ZoneTypeConfig::Tagged* ZoneTypeConfig::as_tagged(Type* type) {
  ASSERT(is_tagged(type));
  return reinterpret_cast<Tagged*>(type);
}


// static
i::Handle<i::Map> ZoneTypeConfig::as_class(Type* type) {
  ASSERT(is_class(type));
  return i::Handle<i::Map>(tagged_get<i::Map**>(as_tagged(type), 1));
}


// static
i::Handle<i::Object> ZoneTypeConfig::as_constant(Type* type) {
  ASSERT(is_constant(type));
  return i::Handle<i::Object>(tagged_get<i::Object**>(as_tagged(type), 1));
}


// static
ZoneTypeConfig::Unioned* ZoneTypeConfig::as_union(Type* type) {
  ASSERT(is_union(type));
  return tagged_as_union(as_tagged(type));
}


// static
ZoneTypeConfig::Unioned* ZoneTypeConfig::tagged_as_union(Tagged* tagged) {
  ASSERT(tagged_is_union(tagged));
  return reinterpret_cast<Unioned*>(tagged);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_bitset(int bitset) {
  return reinterpret_cast<Type*>((bitset << 1) | 1);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_bitset(int bitset, Zone* Zone) {
  return from_bitset(bitset);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_tagged(Tagged* tagged) {
  return reinterpret_cast<Type*>(tagged);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_class(
    i::Handle<i::Map> map, int lub, Zone* zone) {
  Tagged* tagged = tagged_create(kClassTag, 2, zone);
  tagged_set(tagged, 0, lub);
  tagged_set(tagged, 1, map.location());
  return from_tagged(tagged);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_constant(
    i::Handle<i::Object> value, int lub, Zone* zone) {
  Tagged* tagged = tagged_create(kConstantTag, 2, zone);
  tagged_set(tagged, 0, lub);
  tagged_set(tagged, 1, value.location());
  return from_tagged(tagged);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::from_union(Unioned* unioned) {
  return from_tagged(tagged_from_union(unioned));
}


// static
ZoneTypeConfig::Tagged* ZoneTypeConfig::tagged_from_union(Unioned* unioned) {
  return reinterpret_cast<Tagged*>(unioned);
}


// static
ZoneTypeConfig::Unioned* ZoneTypeConfig::union_create(int size, Zone* zone) {
  return tagged_as_union(tagged_create(kUnionTag, size, zone));
}


// static
void ZoneTypeConfig::union_shrink(Unioned* unioned, int size) {
  tagged_shrink(tagged_from_union(unioned), size);
}


// static
ZoneTypeConfig::Type* ZoneTypeConfig::union_get(Unioned* unioned, int i) {
  Type* type = tagged_get<Type*>(tagged_from_union(unioned), i);
  ASSERT(!is_union(type));
  return type;
}


// static
void ZoneTypeConfig::union_set(Unioned* unioned, int i, Type* type) {
  ASSERT(!is_union(type));
  tagged_set(tagged_from_union(unioned), i, type);
}


// static
int ZoneTypeConfig::union_length(Unioned* unioned) {
  return tagged_length(tagged_from_union(unioned));
}


// static
int ZoneTypeConfig::lub_bitset(Type* type) {
  ASSERT(is_class(type) || is_constant(type));
  return static_cast<int>(tagged_get<intptr_t>(as_tagged(type), 0));
}

// -------------------------------------------------------------------------- //

// static
i::Handle<HeapTypeConfig::Type> HeapTypeConfig::handle(Type* type) {
  return i::handle(type, i::HeapObject::cast(type)->GetIsolate());
}


// static
bool HeapTypeConfig::is_bitset(Type* type) {
  return type->IsSmi();
}


// static
bool HeapTypeConfig::is_class(Type* type) {
  return type->IsMap();
}


// static
bool HeapTypeConfig::is_constant(Type* type) {
  return type->IsBox();
}


// static
bool HeapTypeConfig::is_union(Type* type) {
  return type->IsFixedArray();
}


// static
int HeapTypeConfig::as_bitset(Type* type) {
  return Smi::cast(type)->value();
}


// static
i::Handle<i::Map> HeapTypeConfig::as_class(Type* type) {
  return i::handle(i::Map::cast(type));
}


// static
i::Handle<i::Object> HeapTypeConfig::as_constant(Type* type) {
  i::Box* box = i::Box::cast(type);
  return i::handle(box->value(), box->GetIsolate());
}


// static
i::Handle<HeapTypeConfig::Unioned> HeapTypeConfig::as_union(Type* type) {
  return i::handle(i::FixedArray::cast(type));
}


// static
HeapTypeConfig::Type* HeapTypeConfig::from_bitset(int bitset) {
  return Type::cast(i::Smi::FromInt(bitset));
}


// static
i::Handle<HeapTypeConfig::Type> HeapTypeConfig::from_bitset(
    int bitset, Isolate* isolate) {
  return i::handle(from_bitset(bitset), isolate);
}


// static
i::Handle<HeapTypeConfig::Type> HeapTypeConfig::from_class(
    i::Handle<i::Map> map, int lub, Isolate* isolate) {
  return i::Handle<Type>::cast(i::Handle<Object>::cast(map));
}


// static
i::Handle<HeapTypeConfig::Type> HeapTypeConfig::from_constant(
    i::Handle<i::Object> value, int lub, Isolate* isolate) {
  i::Handle<Box> box = isolate->factory()->NewBox(value);
  return i::Handle<Type>::cast(i::Handle<Object>::cast(box));
}


// static
i::Handle<HeapTypeConfig::Type> HeapTypeConfig::from_union(
    i::Handle<Unioned> unioned) {
  return i::Handle<Type>::cast(i::Handle<Object>::cast(unioned));
}


// static
i::Handle<HeapTypeConfig::Unioned> HeapTypeConfig::union_create(
    int size, Isolate* isolate) {
  return isolate->factory()->NewFixedArray(size);
}


// static
void HeapTypeConfig::union_shrink(i::Handle<Unioned> unioned, int size) {
  unioned->Shrink(size);
}


// static
i::Handle<HeapTypeConfig::Type> HeapTypeConfig::union_get(
    i::Handle<Unioned> unioned, int i) {
  Type* type = static_cast<Type*>(unioned->get(i));
  ASSERT(!is_union(type));
  return i::handle(type, unioned->GetIsolate());
}


// static
void HeapTypeConfig::union_set(
    i::Handle<Unioned> unioned, int i, i::Handle<Type> type) {
  ASSERT(!is_union(*type));
  unioned->set(i, *type);
}


// static
int HeapTypeConfig::union_length(i::Handle<Unioned> unioned) {
  return unioned->length();
}


// static
int HeapTypeConfig::lub_bitset(Type* type) {
  return 0;  // kNone, which causes recomputation.
}

} }  // namespace v8::internal

#endif  // V8_TYPES_INL_H_
