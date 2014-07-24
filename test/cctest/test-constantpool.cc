// Copyright 2013 the V8 project authors. All rights reserved.

// Test constant pool array code.

#include "src/v8.h"

#include "src/factory.h"
#include "src/objects.h"
#include "test/cctest/cctest.h"

using namespace v8::internal;

static ConstantPoolArray::Type kTypes[] = { ConstantPoolArray::INT64,
                                            ConstantPoolArray::CODE_PTR,
                                            ConstantPoolArray::HEAP_PTR,
                                            ConstantPoolArray::INT32 };
static ConstantPoolArray::LayoutSection kSmall =
    ConstantPoolArray::SMALL_SECTION;
static ConstantPoolArray::LayoutSection kExtended =
    ConstantPoolArray::EXTENDED_SECTION;

Code* DummyCode(LocalContext* context) {
  CompileRun("function foo() {};");
  i::Handle<i::JSFunction> fun = v8::Utils::OpenHandle(
      *v8::Local<v8::Function>::Cast(
          (*context)->Global()->Get(v8_str("foo"))));
  return fun->code();
}


TEST(ConstantPoolSmall) {
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(context->GetIsolate());

  // Check construction.
  ConstantPoolArray::NumberOfEntries small(3, 1, 2, 1);
  Handle<ConstantPoolArray> array = factory->NewConstantPoolArray(small);

  int expected_counts[] = { 3, 1, 2, 1 };
  int expected_first_idx[] = { 0, 3, 4, 6 };
  int expected_last_idx[] = { 2, 3, 5, 6 };
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(expected_counts[i], array->number_of_entries(kTypes[i], kSmall));
    CHECK_EQ(expected_first_idx[i], array->first_index(kTypes[i], kSmall));
    CHECK_EQ(expected_last_idx[i], array->last_index(kTypes[i], kSmall));
  }
  CHECK(!array->is_extended_layout());

  // Check getters and setters.
  int64_t big_number = V8_2PART_UINT64_C(0x12345678, 9ABCDEF0);
  Handle<Object> object = factory->NewHeapNumber(4.0);
  Code* code = DummyCode(&context);
  array->set(0, big_number);
  array->set(1, 0.5);
  array->set(2, 3e-24);
  array->set(3, code->entry());
  array->set(4, code);
  array->set(5, *object);
  array->set(6, 50);
  CHECK_EQ(big_number, array->get_int64_entry(0));
  CHECK_EQ(0.5, array->get_int64_entry_as_double(1));
  CHECK_EQ(3e-24, array->get_int64_entry_as_double(2));
  CHECK_EQ(code->entry(), array->get_code_ptr_entry(3));
  CHECK_EQ(code, array->get_heap_ptr_entry(4));
  CHECK_EQ(*object, array->get_heap_ptr_entry(5));
  CHECK_EQ(50, array->get_int32_entry(6));

  // Check pointers are updated on GC.
  Object* old_ptr = array->get_heap_ptr_entry(5);
  CHECK_EQ(*object, old_ptr);
  heap->CollectGarbage(NEW_SPACE);
  Object* new_ptr = array->get_heap_ptr_entry(5);
  CHECK_NE(*object, old_ptr);
  CHECK_EQ(*object, new_ptr);
}


TEST(ConstantPoolExtended) {
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(context->GetIsolate());

  // Check construction.
  ConstantPoolArray::NumberOfEntries small(1, 2, 3, 4);
  ConstantPoolArray::NumberOfEntries extended(5, 6, 7, 8);
  Handle<ConstantPoolArray> array =
      factory->NewExtendedConstantPoolArray(small, extended);

  // Check small section.
  int small_counts[] = { 1, 2, 3, 4 };
  int small_first_idx[] = { 0, 1, 3, 6 };
  int small_last_idx[] = { 0, 2, 5, 9 };
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(small_counts[i], array->number_of_entries(kTypes[i], kSmall));
    CHECK_EQ(small_first_idx[i], array->first_index(kTypes[i], kSmall));
    CHECK_EQ(small_last_idx[i], array->last_index(kTypes[i], kSmall));
  }

  // Check extended layout.
  CHECK(array->is_extended_layout());
  int extended_counts[] = { 5, 6, 7, 8 };
  int extended_first_idx[] = { 10, 15, 21, 28 };
  int extended_last_idx[] = { 14, 20, 27, 35 };
  for (int i = 0; i < 4; i++) {
    CHECK_EQ(extended_counts[i],
             array->number_of_entries(kTypes[i], kExtended));
    CHECK_EQ(extended_first_idx[i], array->first_index(kTypes[i], kExtended));
    CHECK_EQ(extended_last_idx[i], array->last_index(kTypes[i], kExtended));
  }

  // Check small and large section's don't overlap.
  int64_t small_section_int64 = V8_2PART_UINT64_C(0x56781234, DEF09ABC);
  Code* small_section_code_ptr = DummyCode(&context);
  Handle<Object> small_section_heap_ptr = factory->NewHeapNumber(4.0);
  int32_t small_section_int32 = 0xab12cd45;

  int64_t extended_section_int64 = V8_2PART_UINT64_C(0x12345678, 9ABCDEF0);
  Code* extended_section_code_ptr = DummyCode(&context);
  Handle<Object> extended_section_heap_ptr = factory->NewHeapNumber(4.0);
  int32_t extended_section_int32 = 0xef67ab89;

  for (int i = array->first_index(ConstantPoolArray::INT64, kSmall);
       i <= array->last_index(ConstantPoolArray::INT32, kSmall); i++) {
    if (i <= array->last_index(ConstantPoolArray::INT64, kSmall)) {
      array->set(i, small_section_int64);
    } else if (i <= array->last_index(ConstantPoolArray::CODE_PTR, kSmall)) {
      array->set(i, small_section_code_ptr->entry());
    } else if (i <= array->last_index(ConstantPoolArray::HEAP_PTR, kSmall)) {
      array->set(i, *small_section_heap_ptr);
    } else {
      CHECK(i <= array->last_index(ConstantPoolArray::INT32, kSmall));
      array->set(i, small_section_int32);
    }
  }
  for (int i = array->first_index(ConstantPoolArray::INT64, kExtended);
       i <= array->last_index(ConstantPoolArray::INT32, kExtended); i++) {
    if (i <= array->last_index(ConstantPoolArray::INT64, kExtended)) {
      array->set(i, extended_section_int64);
    } else if (i <= array->last_index(ConstantPoolArray::CODE_PTR, kExtended)) {
      array->set(i, extended_section_code_ptr->entry());
    } else if (i <= array->last_index(ConstantPoolArray::HEAP_PTR, kExtended)) {
      array->set(i, *extended_section_heap_ptr);
    } else {
      CHECK(i <= array->last_index(ConstantPoolArray::INT32, kExtended));
      array->set(i, extended_section_int32);
    }
  }

  for (int i = array->first_index(ConstantPoolArray::INT64, kSmall);
       i <= array->last_index(ConstantPoolArray::INT32, kSmall); i++) {
    if (i <= array->last_index(ConstantPoolArray::INT64, kSmall)) {
      CHECK_EQ(small_section_int64, array->get_int64_entry(i));
    } else if (i <= array->last_index(ConstantPoolArray::CODE_PTR, kSmall)) {
      CHECK_EQ(small_section_code_ptr->entry(), array->get_code_ptr_entry(i));
    } else if (i <= array->last_index(ConstantPoolArray::HEAP_PTR, kSmall)) {
      CHECK_EQ(*small_section_heap_ptr, array->get_heap_ptr_entry(i));
    } else {
      CHECK(i <= array->last_index(ConstantPoolArray::INT32, kSmall));
      CHECK_EQ(small_section_int32, array->get_int32_entry(i));
    }
  }
  for (int i = array->first_index(ConstantPoolArray::INT64, kExtended);
       i <= array->last_index(ConstantPoolArray::INT32, kExtended); i++) {
    if (i <= array->last_index(ConstantPoolArray::INT64, kExtended)) {
      CHECK_EQ(extended_section_int64, array->get_int64_entry(i));
    } else if (i <= array->last_index(ConstantPoolArray::CODE_PTR, kExtended)) {
      CHECK_EQ(extended_section_code_ptr->entry(),
               array->get_code_ptr_entry(i));
    } else if (i <= array->last_index(ConstantPoolArray::HEAP_PTR, kExtended)) {
      CHECK_EQ(*extended_section_heap_ptr, array->get_heap_ptr_entry(i));
    } else {
      CHECK(i <= array->last_index(ConstantPoolArray::INT32, kExtended));
      CHECK_EQ(extended_section_int32, array->get_int32_entry(i));
    }
  }
  // Check pointers are updated on GC in extended section.
  int index = array->first_index(ConstantPoolArray::HEAP_PTR, kExtended);
  Object* old_ptr = array->get_heap_ptr_entry(index);
  CHECK_EQ(*extended_section_heap_ptr, old_ptr);
  heap->CollectGarbage(NEW_SPACE);
  Object* new_ptr = array->get_heap_ptr_entry(index);
  CHECK_NE(*extended_section_heap_ptr, old_ptr);
  CHECK_EQ(*extended_section_heap_ptr, new_ptr);
}


static void CheckIterator(Handle<ConstantPoolArray> array,
                          ConstantPoolArray::Type type,
                          int expected_indexes[],
                          int count) {
  int i = 0;
  ConstantPoolArray::Iterator iter(*array, type);
  while (!iter.is_finished()) {
    CHECK_EQ(expected_indexes[i++], iter.next_index());
  }
  CHECK_EQ(count, i);
}


TEST(ConstantPoolIteratorSmall) {
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(context->GetIsolate());

  ConstantPoolArray::NumberOfEntries small(1, 5, 2, 0);
  Handle<ConstantPoolArray> array = factory->NewConstantPoolArray(small);

  int expected_int64_indexs[] = { 0 };
  CheckIterator(array, ConstantPoolArray::INT64, expected_int64_indexs, 1);
  int expected_code_indexs[] = { 1, 2, 3, 4, 5 };
  CheckIterator(array, ConstantPoolArray::CODE_PTR, expected_code_indexs, 5);
  int expected_heap_indexs[] = { 6, 7 };
  CheckIterator(array, ConstantPoolArray::HEAP_PTR, expected_heap_indexs, 2);
  int expected_int32_indexs[1];
  CheckIterator(array, ConstantPoolArray::INT32, expected_int32_indexs, 0);
}


TEST(ConstantPoolIteratorExtended) {
  LocalContext context;
  Isolate* isolate = CcTest::i_isolate();
  Factory* factory = isolate->factory();
  v8::HandleScope scope(context->GetIsolate());

  ConstantPoolArray::NumberOfEntries small(1, 0, 0, 4);
  ConstantPoolArray::NumberOfEntries extended(5, 0, 3, 0);
  Handle<ConstantPoolArray> array =
      factory->NewExtendedConstantPoolArray(small, extended);

  int expected_int64_indexs[] = { 0, 5, 6, 7, 8, 9 };
  CheckIterator(array, ConstantPoolArray::INT64, expected_int64_indexs, 6);
  int expected_code_indexs[1];
  CheckIterator(array, ConstantPoolArray::CODE_PTR, expected_code_indexs, 0);
  int expected_heap_indexs[] = { 10, 11, 12 };
  CheckIterator(array, ConstantPoolArray::HEAP_PTR, expected_heap_indexs, 3);
  int expected_int32_indexs[] = { 1, 2, 3, 4 };
  CheckIterator(array, ConstantPoolArray::INT32, expected_int32_indexs, 4);
}
