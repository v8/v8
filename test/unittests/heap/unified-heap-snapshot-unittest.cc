// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>

#include "include/cppgc/allocation.h"
#include "include/cppgc/cross-thread-persistent.h"
#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/name-provider.h"
#include "include/cppgc/persistent.h"
#include "include/cppgc/platform.h"
#include "include/v8-profiler.h"
#include "src/api/api-inl.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/objects/objects-inl.h"
#include "src/profiler/heap-snapshot-generator-inl.h"
#include "src/profiler/heap-snapshot-generator.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8 {
namespace internal {

namespace {

class UnifiedHeapSnapshotTest : public TestWithHeapInternals {
 public:
  UnifiedHeapSnapshotTest()
      : saved_incremental_marking_wrappers_(FLAG_incremental_marking_wrappers) {
    FLAG_incremental_marking_wrappers = false;
    cppgc::InitializeProcess(V8::GetCurrentPlatform()->GetPageAllocator());
    cpp_heap_ = std::make_unique<CppHeap>(
        v8_isolate(), std::vector<std::unique_ptr<cppgc::CustomSpaceBase>>());
    heap()->SetEmbedderHeapTracer(&cpp_heap());
  }

  ~UnifiedHeapSnapshotTest() override {
    heap()->SetEmbedderHeapTracer(nullptr);
    FLAG_incremental_marking_wrappers = saved_incremental_marking_wrappers_;
    cppgc::ShutdownProcess();
  }

  CppHeap& cpp_heap() const { return *cpp_heap_.get(); }

  cppgc::AllocationHandle& allocation_handle() const {
    return cpp_heap().object_allocator();
  }

  const v8::HeapSnapshot* TakeHeapSnapshot() {
    v8::HeapProfiler* heap_profiler = v8_isolate()->GetHeapProfiler();
    return heap_profiler->TakeHeapSnapshot();
  }

 private:
  std::unique_ptr<CppHeap> cpp_heap_;
  bool saved_incremental_marking_wrappers_;
};

bool IsValidSnapshot(const v8::HeapSnapshot* snapshot, int depth = 3) {
  const HeapSnapshot* heap_snapshot =
      reinterpret_cast<const HeapSnapshot*>(snapshot);
  std::unordered_set<const HeapEntry*> visited;
  for (const HeapGraphEdge& edge : heap_snapshot->edges()) {
    visited.insert(edge.to());
  }
  size_t unretained_entries_count = 0;
  for (const HeapEntry& entry : heap_snapshot->entries()) {
    if (visited.find(&entry) == visited.end() && entry.id() != 1) {
      entry.Print("entry with no retainer", "", depth, 0);
      ++unretained_entries_count;
    }
  }
  return unretained_entries_count == 0;
}

bool ContainsRetainingPath(const v8::HeapSnapshot& snapshot,
                           const std::vector<std::string> retaining_path,
                           bool debug_retaining_path = false) {
  const HeapSnapshot& heap_snapshot =
      reinterpret_cast<const HeapSnapshot&>(snapshot);
  std::vector<HeapEntry*> haystack = {heap_snapshot.root()};
  for (size_t i = 0; i < retaining_path.size(); ++i) {
    const std::string& needle = retaining_path[i];
    std::vector<HeapEntry*> new_haystack;
    for (HeapEntry* parent : haystack) {
      for (int j = 0; j < parent->children_count(); j++) {
        HeapEntry* child = parent->child(j)->to();
        if (0 == strcmp(child->name(), needle.c_str())) {
          new_haystack.push_back(child);
        }
      }
    }
    if (new_haystack.empty()) {
      if (debug_retaining_path) {
        fprintf(stderr,
                "#\n# Could not find object with name '%s'\n#\n# Path:\n",
                needle.c_str());
        for (size_t j = 0; j < retaining_path.size(); ++j) {
          fprintf(stderr, "# - '%s'%s\n", retaining_path[j].c_str(),
                  i == j ? "\t<--- not found" : "");
        }
        fprintf(stderr, "#\n");
      }
      return false;
    }
    std::swap(haystack, new_haystack);
  }
  return true;
}

class BaseWithoutName : public cppgc::GarbageCollected<BaseWithoutName> {
 public:
  static constexpr const char kExpectedName[] =
      "v8::internal::(anonymous namespace)::BaseWithoutName";

  virtual void Trace(cppgc::Visitor* v) const {
    v->Trace(next);
    v->Trace(next2);
  }
  cppgc::Member<BaseWithoutName> next;
  cppgc::Member<BaseWithoutName> next2;
};
// static
constexpr const char BaseWithoutName::kExpectedName[];

class GCed final : public BaseWithoutName, public cppgc::NameProvider {
 public:
  static constexpr const char kExpectedName[] = "GCed";

  void Trace(cppgc::Visitor* v) const final { BaseWithoutName::Trace(v); }
  const char* GetName() const final { return "GCed"; }
};
// static
constexpr const char GCed::kExpectedName[];

constexpr const char kExpectedCppRootsName[] = "C++ roots";
constexpr const char kExpectedCppCrossThreadRootsName[] =
    "C++ cross-thread roots";

template <typename T>
constexpr const char* GetExpectedName() {
  if (std::is_base_of<cppgc::NameProvider, T>::value ||
      !cppgc::NameProvider::HideInternalNames()) {
    return T::kExpectedName;
  } else {
    return cppgc::NameProvider::kHiddenName;
  }
}

}  // namespace

TEST_F(UnifiedHeapSnapshotTest, EmptySnapshot) {
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
}

TEST_F(UnifiedHeapSnapshotTest, RetainedByCppRoot) {
  cppgc::Persistent<GCed> gced =
      cppgc::MakeGarbageCollected<GCed>(allocation_handle());
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
  EXPECT_TRUE(
      ContainsRetainingPath(*snapshot, {
                                           kExpectedCppRootsName,   // NOLINT
                                           GetExpectedName<GCed>()  // NOLINT
                                       }));
}

TEST_F(UnifiedHeapSnapshotTest, RetainedByCppCrossThreadRoot) {
  cppgc::subtle::CrossThreadPersistent<GCed> gced =
      cppgc::MakeGarbageCollected<GCed>(allocation_handle());
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
  EXPECT_TRUE(ContainsRetainingPath(
      *snapshot, {
                     kExpectedCppCrossThreadRootsName,  // NOLINT
                     GetExpectedName<GCed>()            // NOLINT
                 }));
}

TEST_F(UnifiedHeapSnapshotTest, RetainingUnnamedType) {
  cppgc::Persistent<BaseWithoutName> base_without_name =
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle());
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
  if (cppgc::NameProvider::HideInternalNames()) {
    EXPECT_FALSE(ContainsRetainingPath(
        *snapshot, {kExpectedCppRootsName, cppgc::NameProvider::kHiddenName}));
  } else {
    EXPECT_TRUE(ContainsRetainingPath(
        *snapshot, {
                       kExpectedCppRootsName,              // NOLINT
                       GetExpectedName<BaseWithoutName>()  // NOLINT
                   }));
  }
}

TEST_F(UnifiedHeapSnapshotTest, RetainingNamedThroughUnnamed) {
  cppgc::Persistent<BaseWithoutName> base_without_name =
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle());
  base_without_name->next =
      cppgc::MakeGarbageCollected<GCed>(allocation_handle());
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
  EXPECT_TRUE(ContainsRetainingPath(
      *snapshot, {
                     kExpectedCppRootsName,               // NOLINT
                     GetExpectedName<BaseWithoutName>(),  // NOLINT
                     GetExpectedName<GCed>()              // NOLINT
                 }));
}

TEST_F(UnifiedHeapSnapshotTest, PendingCallStack) {
  // Test ensures that the algorithm handles references into the current call
  // stack.
  //
  // Graph:
  //   Persistent -> BaseWithoutName (2) <-> BaseWithoutName (1) -> GCed (3)
  //
  // Visitation order is (1)->(2)->(3) which is a corner case, as when following
  // back from (2)->(1) the object in (1) is already visited and will only later
  // be marked as visible.
  auto* first =
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle());
  auto* second =
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle());
  first->next = second;
  first->next->next = first;
  auto* third = cppgc::MakeGarbageCollected<GCed>(allocation_handle());
  first->next2 = third;

  cppgc::Persistent<BaseWithoutName> holder(second);
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
  EXPECT_TRUE(
      ContainsRetainingPath(*snapshot,
                            {
                                kExpectedCppRootsName,               // NOLINT
                                GetExpectedName<BaseWithoutName>(),  // NOLINT
                                GetExpectedName<BaseWithoutName>(),  // NOLINT
                                GetExpectedName<GCed>()              // NOLINT
                            }));
}

TEST_F(UnifiedHeapSnapshotTest, ReferenceToFinishedSCC) {
  // Test ensures that the algorithm handles reference into an already finished
  // SCC that is marked as hidden whereas the current SCC would resolve to
  // visible.
  //
  // Graph:
  //   Persistent -> BaseWithoutName (1)
  //   Persistent -> BaseWithoutName (2)
  //                        + <-> BaseWithoutName (3) -> BaseWithoutName (1)
  //                        + -> GCed (4)
  //
  // Visitation order (1)->(2)->(3)->(1) which is a corner case as (3) would set
  // a dependency on (1) which is hidden. Instead (3) should set a dependency on
  // (2) as (1) resolves to hidden whereas (2) resolves to visible. The test
  // ensures that resolved hidden dependencies are ignored.
  cppgc::Persistent<BaseWithoutName> hidden_holder(
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle()));
  auto* first =
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle());
  auto* second =
      cppgc::MakeGarbageCollected<BaseWithoutName>(allocation_handle());
  first->next = second;
  second->next = *hidden_holder;
  second->next2 = first;
  first->next2 = cppgc::MakeGarbageCollected<GCed>(allocation_handle());
  cppgc::Persistent<BaseWithoutName> holder(first);
  const v8::HeapSnapshot* snapshot = TakeHeapSnapshot();
  EXPECT_TRUE(IsValidSnapshot(snapshot));
  EXPECT_TRUE(
      ContainsRetainingPath(*snapshot,
                            {
                                kExpectedCppRootsName,               // NOLINT
                                GetExpectedName<BaseWithoutName>(),  // NOLINT
                                GetExpectedName<BaseWithoutName>(),  // NOLINT
                                GetExpectedName<BaseWithoutName>(),  // NOLINT
                                GetExpectedName<GCed>()              // NOLINT
                            }));
}

}  // namespace internal
}  // namespace v8
