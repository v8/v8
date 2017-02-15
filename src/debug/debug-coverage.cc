// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-coverage.h"

#include "src/base/hashmap.h"
#include "src/deoptimizer.h"
#include "src/isolate.h"
#include "src/objects-inl.h"
#include "src/objects.h"

namespace v8 {
namespace internal {

class SharedToCounterMap
    : public base::TemplateHashMapImpl<SharedFunctionInfo*, uint32_t,
                                       base::KeyEqualityMatcher<void*>,
                                       base::DefaultAllocationPolicy> {
 public:
  typedef base::TemplateHashMapEntry<SharedFunctionInfo*, uint32_t> Entry;
  inline void Add(SharedFunctionInfo* key, uint32_t count) {
    Entry* entry = LookupOrInsert(key, Hash(key), []() { return 0; });
    uint32_t old_count = entry->value;
    if (UINT32_MAX - count < old_count) {
      entry->value = UINT32_MAX;
    } else {
      entry->value = old_count + count;
    }
  }

  inline uint32_t Get(SharedFunctionInfo* key) {
    Entry* entry = Lookup(key, Hash(key));
    if (entry == nullptr) return 0;
    return entry->value;
  }

 private:
  static uint32_t Hash(SharedFunctionInfo* key) {
    return static_cast<uint32_t>(reinterpret_cast<intptr_t>(key));
  }

  DisallowHeapAllocation no_gc;
};

namespace {
int StartPosition(SharedFunctionInfo* info) {
  int start = info->function_token_position();
  if (start == kNoSourcePosition) start = info->start_position();
  return start;
}

bool CompareSharedFunctionInfo(SharedFunctionInfo* a, SharedFunctionInfo* b) {
  int a_start = StartPosition(a);
  int b_start = StartPosition(b);
  if (a_start == b_start) return a->end_position() > b->end_position();
  return a_start < b_start;
}
}  // anonymous namespace

std::vector<Coverage::ScriptData> Coverage::Collect(Isolate* isolate) {
  SharedToCounterMap counter_map;

  // Feed invocation count into the counter map.
  if (isolate->IsCodeCoverageEnabled()) {
    // Feedback vectors are already listed to prevent losing them to GC.
    Handle<ArrayList> list =
        Handle<ArrayList>::cast(isolate->factory()->code_coverage_list());
    for (int i = 0; i < list->Length(); i++) {
      FeedbackVector* vector = FeedbackVector::cast(list->Get(i));
      SharedFunctionInfo* shared = vector->shared_function_info();
      DCHECK(shared->IsSubjectToDebugging());
      uint32_t count = static_cast<uint32_t>(vector->invocation_count());
      counter_map.Add(shared, count);
    }
  } else {
    // Iterate the heap to find all feedback vectors and accumulate the
    // invocation counts into the map for each shared function info.
    HeapIterator heap_iterator(isolate->heap());
    while (HeapObject* current_obj = heap_iterator.next()) {
      if (!current_obj->IsFeedbackVector()) continue;
      FeedbackVector* vector = FeedbackVector::cast(current_obj);
      SharedFunctionInfo* shared = vector->shared_function_info();
      if (!shared->IsSubjectToDebugging()) continue;
      uint32_t count = static_cast<uint32_t>(vector->invocation_count());
      counter_map.Add(shared, count);
    }
  }

  // Iterate shared function infos of every script and build a mapping
  // between source ranges and invocation counts.
  std::vector<Coverage::ScriptData> result;
  Script::Iterator scripts(isolate);
  while (Script* script = scripts.Next()) {
    // Dismiss non-user scripts.
    if (script->type() != Script::TYPE_NORMAL) continue;

    // Create and add new script data.
    int source_length = String::cast(script->source())->length();
    result.emplace_back(Handle<Script>(script, isolate), source_length);

    std::vector<SharedFunctionInfo*> sorted;

    {
      // Collect a list of shared function infos sorted by start position.
      // Shared function infos are usually already sorted. Except for classes.
      // If the start position is the same, sort from outer to inner function.
      HandleScope scope(isolate);
      SharedFunctionInfo::ScriptIterator infos(Handle<Script>(script, isolate));
      while (SharedFunctionInfo* info = infos.Next()) sorted.push_back(info);
      std::sort(sorted.begin(), sorted.end(), CompareSharedFunctionInfo);
    }

    std::vector<Range*> stack;
    stack.push_back(&result.back().toplevel);

    // Use sorted list to reconstruct function nesting.
    for (SharedFunctionInfo* info : sorted) {
      int start = StartPosition(info);
      int end = info->end_position();
      uint32_t count = counter_map.Get(info);
      if (info->is_toplevel()) {
        // Top-level function is available.
        DCHECK_EQ(1, stack.size());
        result.back().toplevel.start = start;
        result.back().toplevel.end = end;
        result.back().toplevel.count = count;
      } else {
        // The shared function infos are sorted by start.
        DCHECK_LE(stack.back()->start, start);
        // Drop the stack to the outer function.
        while (start >= stack.back()->end) stack.pop_back();
        Range* outer = stack.back();
        // New nested function.
        DCHECK_LE(end, outer->end);
        outer->inner.emplace_back(start, end, count);
        Range& nested = outer->inner.back();
        String* name = info->DebugName();
        nested.name.resize(name->length());
        String::WriteToFlat(name, nested.name.data(), 0, name->length());
        stack.push_back(&nested);
      }
    }
  }
  return result;
}

void Coverage::EnablePrecise(Isolate* isolate) {
  HandleScope scope(isolate);
  // Remove all optimized function. Optimized and inlined functions do not
  // increment invocation count.
  Deoptimizer::DeoptimizeAll(isolate);
  // Collect existing feedback vectors.
  std::vector<Handle<FeedbackVector>> vectors;
  {
    HeapIterator heap_iterator(isolate->heap());
    while (HeapObject* current_obj = heap_iterator.next()) {
      if (!current_obj->IsFeedbackVector()) continue;
      FeedbackVector* vector = FeedbackVector::cast(current_obj);
      SharedFunctionInfo* shared = vector->shared_function_info();
      if (!shared->IsSubjectToDebugging()) continue;
      vector->clear_invocation_count();
      vectors.emplace_back(vector, isolate);
    }
  }
  // Add collected feedback vectors to the root list lest we lose them to GC.
  Handle<ArrayList> list =
      ArrayList::New(isolate, static_cast<int>(vectors.size()));
  for (const auto& vector : vectors) list = ArrayList::Add(list, vector);
  isolate->SetCodeCoverageList(*list);
}

void Coverage::DisablePrecise(Isolate* isolate) {
  isolate->SetCodeCoverageList(isolate->heap()->undefined_value());
}

}  // namespace internal
}  // namespace v8
