// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/marking-worklist.h"

#include <algorithm>
#include <map>

#include "src/objects/heap-object-inl.h"
#include "src/objects/heap-object.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/map.h"
#include "src/objects/objects-definitions.h"

namespace v8 {
namespace internal {

void MarkingWorklistsHolder::Clear() {
  shared_.Clear();
  on_hold_.Clear();
  embedder_.Clear();
}

void MarkingWorklistsHolder::Print() {
  PrintWorklist("shared", &shared_);
  PrintWorklist("on_hold", &on_hold_);
}

void MarkingWorklistsHolder::PrintWorklist(const char* worklist_name,
                                           MarkingWorklist* worklist) {
#ifdef DEBUG
  std::map<InstanceType, int> count;
  int total_count = 0;
  worklist->IterateGlobalPool([&count, &total_count](HeapObject obj) {
    ++total_count;
    count[obj.map().instance_type()]++;
  });
  std::vector<std::pair<int, InstanceType>> rank;
  rank.reserve(count.size());
  for (const auto& i : count) {
    rank.emplace_back(i.second, i.first);
  }
  std::map<InstanceType, std::string> instance_type_name;
#define INSTANCE_TYPE_NAME(name) instance_type_name[name] = #name;
  INSTANCE_TYPE_LIST(INSTANCE_TYPE_NAME)
#undef INSTANCE_TYPE_NAME
  std::sort(rank.begin(), rank.end(),
            std::greater<std::pair<int, InstanceType>>());
  PrintF("Worklist %s: %d\n", worklist_name, total_count);
  for (auto i : rank) {
    PrintF("  [%s]: %d\n", instance_type_name[i.second].c_str(), i.first);
  }
#endif
}

MarkingWorklists::MarkingWorklists(int task_id, MarkingWorklistsHolder* holder)
    : shared_(holder->shared()),
      on_hold_(holder->on_hold()),
      embedder_(holder->embedder()),
      task_id_(task_id) {}

void MarkingWorklists::FlushToGlobal() {
  shared_->FlushToGlobal(task_id_);
  on_hold_->FlushToGlobal(task_id_);
  embedder_->FlushToGlobal(task_id_);
}

bool MarkingWorklists::IsEmpty() {
  // This function checks the on_hold_ worklist, so it works only for the main
  // thread.
  DCHECK_EQ(kMainThreadTask, task_id_);
  return shared_->IsLocalEmpty(task_id_) && on_hold_->IsLocalEmpty(task_id_) &&
         shared_->IsGlobalPoolEmpty() && on_hold_->IsGlobalPoolEmpty();
}

bool MarkingWorklists::IsEmbedderEmpty() {
  return embedder_->IsLocalEmpty(task_id_) && embedder_->IsGlobalPoolEmpty();
}

void MarkingWorklists::ShareWorkIfGlobalPoolIsEmpty() {
  if (!shared_->IsLocalEmpty(task_id_) && shared_->IsGlobalPoolEmpty()) {
    shared_->FlushToGlobal(task_id_);
  }
}

void MarkingWorklists::MergeOnHold() {
  DCHECK_EQ(kMainThreadTask, task_id_);
  shared_->MergeGlobalPool(on_hold_);
}

}  // namespace internal
}  // namespace v8
