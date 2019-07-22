// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/backing-store.h"
#include "test/unittests/test-utils.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class BackingStoreTest : public TestWithIsolate {};

TEST_F(BackingStoreTest, GrowWasmMemoryInPlace) {
  auto backing_store =
      BackingStore::AllocateWasmMemory(isolate(), 1, 2, SharedFlag::kNotShared);
  CHECK(backing_store);
  EXPECT_TRUE(backing_store->is_wasm_memory());
  EXPECT_EQ(1 * wasm::kWasmPageSize, backing_store->byte_length());
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_capacity());

  bool success = backing_store->GrowWasmMemoryInPlace(isolate(), 2);
  EXPECT_TRUE(success);
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_length());
}

TEST_F(BackingStoreTest, GrowWasmMemoryInPlace_neg) {
  auto backing_store =
      BackingStore::AllocateWasmMemory(isolate(), 1, 2, SharedFlag::kNotShared);
  CHECK(backing_store);
  EXPECT_TRUE(backing_store->is_wasm_memory());
  EXPECT_EQ(1 * wasm::kWasmPageSize, backing_store->byte_length());
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_capacity());

  bool success = backing_store->GrowWasmMemoryInPlace(isolate(), 3);
  EXPECT_FALSE(success);
  EXPECT_EQ(1 * wasm::kWasmPageSize, backing_store->byte_length());
}

TEST_F(BackingStoreTest, GrowSharedWasmMemoryInPlace) {
  auto backing_store =
      BackingStore::AllocateWasmMemory(isolate(), 2, 3, SharedFlag::kShared);
  CHECK(backing_store);
  EXPECT_TRUE(backing_store->is_wasm_memory());
  EXPECT_EQ(2 * wasm::kWasmPageSize, backing_store->byte_length());
  EXPECT_EQ(3 * wasm::kWasmPageSize, backing_store->byte_capacity());

  bool success = backing_store->GrowWasmMemoryInPlace(isolate(), 3);
  EXPECT_TRUE(success);
  EXPECT_EQ(3 * wasm::kWasmPageSize, backing_store->byte_length());
}

TEST_F(BackingStoreTest, CopyWasmMemory) {
  auto bs1 =
      BackingStore::AllocateWasmMemory(isolate(), 1, 2, SharedFlag::kNotShared);
  CHECK(bs1);
  EXPECT_TRUE(bs1->is_wasm_memory());
  EXPECT_EQ(1 * wasm::kWasmPageSize, bs1->byte_length());
  EXPECT_EQ(2 * wasm::kWasmPageSize, bs1->byte_capacity());

  auto bs2 = bs1->CopyWasmMemory(isolate(), 3);
  EXPECT_TRUE(bs2->is_wasm_memory());
  EXPECT_EQ(3 * wasm::kWasmPageSize, bs2->byte_length());
  EXPECT_EQ(3 * wasm::kWasmPageSize, bs2->byte_capacity());
}

}  // namespace internal
}  // namespace v8
