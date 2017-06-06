// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <utility>
#include "src/v8.h"

#include "src/objects-inl.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;

static Isolate* GetIsolateFrom(LocalContext* context) {
  return reinterpret_cast<Isolate*>((*context)->GetIsolate());
}

void Verify(Handle<SmallOrderedHashSet> set) {
#if VERIFY_HEAP
  set->ObjectVerify();
#endif
}

TEST(Insertion) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfElements());

  // Add a new key.
  Handle<Smi> key1(Smi::FromInt(1), isolate);
  CHECK(!set->HasKey(isolate, key1));
  set = SmallOrderedHashSet::Add(set, key1);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));

  // Add existing key.
  set = SmallOrderedHashSet::Add(set, key1);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));

  Handle<String> key2 = factory->NewStringFromAsciiChecked("foo");
  CHECK(!set->HasKey(isolate, key2));
  set = SmallOrderedHashSet::Add(set, key2);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));

  set = SmallOrderedHashSet::Add(set, key2);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));

  Handle<Symbol> key3 = factory->NewSymbol();
  CHECK(!set->HasKey(isolate, key3));
  set = SmallOrderedHashSet::Add(set, key3);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(3, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  set = SmallOrderedHashSet::Add(set, key3);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(3, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));

  Handle<Object> key4 = factory->NewHeapNumber(42.0);
  CHECK(!set->HasKey(isolate, key4));
  set = SmallOrderedHashSet::Add(set, key4);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(4, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));
  CHECK(set->HasKey(isolate, key4));

  set = SmallOrderedHashSet::Add(set, key4);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(4, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
  CHECK(set->HasKey(isolate, key3));
  CHECK(set->HasKey(isolate, key4));
}

TEST(DuplicateHashCode) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  Handle<JSObject> key1 = factory->NewJSObjectWithNullProto();
  set = SmallOrderedHashSet::Add(set, key1);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(1, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));

  Handle<Name> hash_code_symbol = isolate->factory()->hash_code_symbol();
  Handle<Smi> hash =
      Handle<Smi>::cast(JSObject::GetDataProperty(key1, hash_code_symbol));

  Handle<JSObject> key2 = factory->NewJSObjectWithNullProto();
  LookupIterator it(key2, hash_code_symbol, key2, LookupIterator::OWN);
  CHECK(key2->AddDataProperty(
                &it, hash, NONE, v8::internal::AccessCheckInfo::THROW_ON_ERROR,
                v8::internal::AccessCheckInfo::CERTAINLY_NOT_STORE_FROM_KEYED)
            .IsJust());

  set = SmallOrderedHashSet::Add(set, key2);
  Verify(set);
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(2, set->NumberOfElements());
  CHECK(set->HasKey(isolate, key1));
  CHECK(set->HasKey(isolate, key2));
}

TEST(Grow) {
  LocalContext context;
  Isolate* isolate = GetIsolateFrom(&context);
  Factory* factory = isolate->factory();
  HandleScope scope(isolate);

  Handle<SmallOrderedHashSet> set = factory->NewSmallOrderedHashSet();
  std::vector<Handle<Object>> keys;
  for (size_t i = 0; i < 4; i++) {
    Handle<Smi> key(Smi::FromInt(static_cast<int>(i)), isolate);
    keys.push_back(key);
  }

  for (size_t i = 0; i < 4; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 4; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(4, set->NumberOfElements());
  CHECK_EQ(2, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);

  for (int i = 4; i < 8; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 4; i < 8; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 8; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(8, set->NumberOfElements());
  CHECK_EQ(4, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);

  for (int i = 8; i < 16; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 8; i < 16; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 16; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(16, set->NumberOfElements());
  CHECK_EQ(8, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);

  for (int i = 16; i < 32; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 16; i < 32; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 32; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(32, set->NumberOfElements());
  CHECK_EQ(16, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);

  for (int i = 32; i < 64; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 32; i < 64; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 64; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(64, set->NumberOfElements());
  CHECK_EQ(32, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);

  for (int i = 64; i < 128; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 64; i < 128; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 128; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(128, set->NumberOfElements());
  CHECK_EQ(64, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);

  for (int i = 128; i < 254; i++) {
    Handle<Smi> key(Smi::FromInt(i), isolate);
    keys.push_back(key);
  }

  for (size_t i = 128; i < 254; i++) {
    set = SmallOrderedHashSet::Add(set, keys[i]);
    Verify(set);
  }

  for (size_t i = 0; i < 254; i++) {
    CHECK(set->HasKey(isolate, keys[i]));
    Verify(set);
  }

  CHECK_EQ(254, set->NumberOfElements());
  CHECK_EQ(127, set->NumberOfBuckets());
  CHECK_EQ(0, set->NumberOfDeletedElements());
  Verify(set);
}
