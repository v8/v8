// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <json/json.h>

#include "src/profiler/heap-profiler.h"
#include "src/profiler/heap-snapshot-generator.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8::internal {

template <typename TMixin>
class WithHeapSnapshot : public TMixin {
 public:
  HeapSnapshot* TakeHeapSnapshot() {
    HeapProfiler* heap_profiler = TMixin::isolate()->heap()->heap_profiler();
    v8::HeapProfiler::HeapSnapshotOptions options;
    return heap_profiler->TakeSnapshot(options);
  }

  const Json::Value TakeHeapSnapshotJson() {
    HeapProfiler* heap_profiler = TMixin::isolate()->heap()->heap_profiler();
    v8::HeapProfiler::HeapSnapshotOptions options;
    std::string raw_json = heap_profiler->TakeSnapshotToString(options);

    Json::Value root;
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    CHECK(reader->parse(raw_json.c_str(), raw_json.c_str() + raw_json.size(),
                        &root, nullptr));
    return root;
  }

 protected:
  void TestMergedWrapperNode(v8::HeapProfiler::HeapSnapshotMode snapshot_mode);
};

using HeapSnapshotTest = WithHeapSnapshot<TestWithIsolate>;

TEST_F(HeapSnapshotTest, Empty) {
  Json::Value root = TakeHeapSnapshotJson();

  Json::Value meta = root["snapshot"]["meta"];
  CHECK_EQ(meta["node_fields"].size(), meta["node_types"].size());
  CHECK_EQ(meta["edge_fields"].size(), meta["edge_types"].size());
}

TEST_F(HeapSnapshotTest, StringTableRootsAreWeak) {
  HeapSnapshot* snapshot = TakeHeapSnapshot();
  const HeapEntry* string_table_entry =
      snapshot->gc_subroot(Root::kStringTable);
  ASSERT_NE(nullptr, string_table_entry);
  ASSERT_GT(string_table_entry->children_count(), 0);
  // All edges from the string table should be weak.
  for (int i = 0; i < string_table_entry->children_count(); ++i) {
    const HeapGraphEdge* edge = string_table_entry->child(i);
    EXPECT_EQ(HeapGraphEdge::kWeak, edge->type());
  }
}

}  // namespace v8::internal
