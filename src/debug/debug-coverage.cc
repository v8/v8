// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-coverage.h"

#include "src/base/hashmap.h"
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
};

class ScriptDataBuilder {
 public:
  void Add(int end_position, uint32_t count) {
    DCHECK(entries_.empty() || entries_.back().end_position <= end_position);
    if (entries_.empty()) {
      if (end_position > 0) entries_.push_back({end_position, count});
    } else if (entries_.back().count == count) {
      // Extend last range.
      entries_.back().end_position = end_position;
    } else if (entries_.back().end_position < end_position) {
      // Add new range.
      entries_.push_back({end_position, count});
    }
  }
  std::vector<Coverage::RangeEntry> Finish() {
    std::vector<Coverage::RangeEntry> result;
    std::swap(result, entries_);
    return result;
  }

 private:
  std::vector<Coverage::RangeEntry> entries_;
};

std::vector<Coverage::ScriptData> Coverage::Collect(Isolate* isolate) {
  SharedToCounterMap counter_map;
  // Iterate the heap to find all feedback vectors and accumulate the
  // invocation counts into the map for each shared function info.
  HeapIterator heap_iterator(isolate->heap());
  HeapObject* current_obj;
  while ((current_obj = heap_iterator.next())) {
    if (!current_obj->IsFeedbackVector()) continue;
    FeedbackVector* vector = FeedbackVector::cast(current_obj);
    SharedFunctionInfo* shared = vector->shared_function_info();
    if (!shared->IsSubjectToDebugging()) continue;
    uint32_t count = static_cast<uint32_t>(vector->invocation_count());
    counter_map.Add(shared, count);
  }

  // Make sure entries in the counter map is not invalidated by GC.
  DisallowHeapAllocation no_gc;

  // Stack to track nested functions.
  struct FunctionNode {
    FunctionNode(int s, int e, uint32_t c) : start(s), end(e), count(c) {}
    int start;
    int end;
    uint32_t count;
  };
  std::vector<FunctionNode> stack;

  // Iterate shared function infos of every script and build a mapping
  // between source ranges and invocation counts.
  std::vector<Coverage::ScriptData> result;
  Script::Iterator scripts(isolate);
  while (Script* script = scripts.Next()) {
    // Dismiss non-user scripts.
    if (script->type() != Script::TYPE_NORMAL) continue;
    DCHECK(stack.empty());
    int script_end = String::cast(script->source())->length();
    // If not rooted, the top-level function is likely no longer alive. Set the
    // outer-most count to 1 to indicate that the script has run at least once.
    stack.push_back({0, script_end, 1});
    ScriptDataBuilder builder;
    // Iterate through the list of shared function infos, reconstruct the
    // nesting, and compute the ranges covering different invocation counts.
    HandleScope scope(isolate);
    SharedFunctionInfo::ScriptIterator infos(Handle<Script>(script, isolate));
    while (SharedFunctionInfo* info = infos.Next()) {
      int start = info->function_token_position();
      if (start == kNoSourcePosition) start = info->start_position();
      int end = info->end_position();
      uint32_t count = counter_map.Get(info);
      // The shared function infos are sorted by start.
      DCHECK_LE(stack.back().start, start);
      // If the start are the same, the outer function comes before the inner.
      DCHECK(stack.back().start < start || stack.back().end >= end);
      // Drop the stack to the outer function.
      while (start > stack.back().end) {
        // Write out rest of function being dropped.
        builder.Add(stack.back().end, stack.back().count);
        stack.pop_back();
      }
      // Write out outer function up to the start of new function.
      builder.Add(start, stack.back().count);
      // New nested function.
      DCHECK_LE(end, stack.back().end);
      stack.emplace_back(start, end, count);
    }

    // Drop the stack to the script level.
    while (!stack.empty()) {
      // Write out rest of function being dropped.
      builder.Add(stack.back().end, stack.back().count);
      stack.pop_back();
    }
    result.emplace_back(script->id(), builder.Finish());
  }
  return result;
}

}  // namespace internal
}  // namespace v8
