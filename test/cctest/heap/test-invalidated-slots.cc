// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/v8.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/invalidated-slots-inl.h"
#include "src/heap/invalidated-slots.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {

HEAP_TEST(InvalidatedSlots) {
  CcTest::InitializeVM();
  Heap* heap = CcTest::heap();
  Isolate* isolate = heap->isolate();
  PagedSpace* old_space = heap->old_space();
  Page* page;
  std::vector<ByteArray*> byte_arrays;
  const int kLength = 256 - ByteArray::kHeaderSize;
  const int kSize = ByteArray::SizeFor(kLength);
  CHECK_EQ(kSize, 256);
  // Fill a page with byte arrays.
  {
    AlwaysAllocateScope always_allocate(isolate);
    heap::SimulateFullSpace(old_space);
    ByteArray* byte_array;
    CHECK(heap->AllocateByteArray(kLength, TENURED).To(&byte_array));
    byte_arrays.push_back(byte_array);
    page = Page::FromAddress(byte_array->address());
    CHECK_EQ(page->area_size() % kSize, 0u);
    size_t n = page->area_size() / kSize;
    for (size_t i = 1; i < n; i++) {
      CHECK(heap->AllocateByteArray(kLength, TENURED).To(&byte_array));
      byte_arrays.push_back(byte_array);
      CHECK_EQ(page, Page::FromAddress(byte_array->address()));
    }
  }
  CHECK_NULL(page->invalidated_slots());
  {
    // Without invalidated slots on the page, the filter considers
    // all slots as valid.
    InvalidatedSlotsFilter filter(page);
    for (auto byte_array : byte_arrays) {
      Address start = byte_array->address() + ByteArray::kHeaderSize;
      Address end = byte_array->address() + kSize;
      for (Address addr = start; addr < end; addr += kPointerSize) {
        CHECK(filter.IsValid(addr));
      }
    }
  }
  // Register every second byte arrays as invalidated.
  for (size_t i = 0; i < byte_arrays.size(); i += 2) {
    page->RegisterObjectWithInvalidatedSlots(byte_arrays[i], kSize);
  }
  {
    InvalidatedSlotsFilter filter(page);
    for (size_t i = 0; i < byte_arrays.size(); i++) {
      ByteArray* byte_array = byte_arrays[i];
      Address start = byte_array->address() + ByteArray::kHeaderSize;
      Address end = byte_array->address() + kSize;
      for (Address addr = start; addr < end; addr += kPointerSize) {
        if (i % 2 == 0) {
          CHECK(!filter.IsValid(addr));
        } else {
          CHECK(filter.IsValid(addr));
        }
      }
    }
  }
  // Register the remaining byte arrays as invalidated.
  for (size_t i = 1; i < byte_arrays.size(); i += 2) {
    page->RegisterObjectWithInvalidatedSlots(byte_arrays[i], kSize);
  }
  {
    InvalidatedSlotsFilter filter(page);
    for (size_t i = 0; i < byte_arrays.size(); i++) {
      ByteArray* byte_array = byte_arrays[i];
      Address start = byte_array->address() + ByteArray::kHeaderSize;
      Address end = byte_array->address() + kSize;
      for (Address addr = start; addr < end; addr += kPointerSize) {
        CHECK(!filter.IsValid(addr));
      }
    }
  }
}

}  // namespace internal
}  // namespace v8
