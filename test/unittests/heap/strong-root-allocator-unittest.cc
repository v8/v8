// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <list>
#include <vector>

#include "src/heap/heap.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using StrongRootAllocatorTest = TestWithHeapInternals;

TEST_F(StrongRootAllocatorTest, AddressRetained) {
  Global<v8::FixedArray> weak;

  StrongRootAllocator<Address> allocator(heap());
  Address* allocated = allocator.allocate(10);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    allocated[7] = h->ptr();
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_FALSE(weak.IsEmpty());

  allocator.deallocate(allocated, 10);

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, StructNotRetained) {
  Global<v8::FixedArray> weak;

  struct Wrapped {
    Address content;
  };

  StrongRootAllocator<Wrapped> allocator(heap());
  Wrapped* allocated = allocator.allocate(10);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    allocated[7].content = h->ptr();
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());

  allocator.deallocate(allocated, 10);
}

TEST_F(StrongRootAllocatorTest, VectorRetained) {
  Global<v8::FixedArray> weak;

  {
    StrongRootAllocator<Address> allocator(heap());
    std::vector<Address, StrongRootAllocator<Address>> v(10, allocator);

    {
      v8::HandleScope scope(v8_isolate());
      Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
      v[7] = h->ptr();
      Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
      weak.Reset(v8_isolate(), l);
      weak.SetWeak();
    }

    {
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
      InvokeMajorGC();
    }
    EXPECT_FALSE(weak.IsEmpty());
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, VectorOfStructNotRetained) {
  Global<v8::FixedArray> weak;

  struct Wrapped {
    Address content;
  };

  StrongRootAllocator<Wrapped> allocator(heap());
  std::vector<Wrapped, StrongRootAllocator<Wrapped>> v(10, allocator);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    v[7].content = h->ptr();
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, ListNotRetained) {
  Global<v8::FixedArray> weak;

  StrongRootAllocator<Address> allocator(heap());
  std::list<Address, StrongRootAllocator<Address>> l(allocator);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    l.push_back(h->ptr());
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

TEST_F(StrongRootAllocatorTest, SetNotRetained) {
  Global<v8::FixedArray> weak;

  StrongRootAllocator<Address> allocator(heap());
  std::set<Address, std::less<Address>, StrongRootAllocator<Address>> s(
      allocator);

  {
    v8::HandleScope scope(v8_isolate());
    Handle<FixedArray> h = factory()->NewFixedArray(10, AllocationType::kOld);
    s.insert(h->ptr());
    Local<v8::FixedArray> l = Utils::FixedArrayToLocal(h);
    weak.Reset(v8_isolate(), l);
    weak.SetWeak();
  }

  {
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());
    InvokeMajorGC();
  }
  EXPECT_TRUE(weak.IsEmpty());
}

}  // namespace internal
}  // namespace v8
