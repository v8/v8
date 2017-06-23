// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/debug-coverage.h"

#include "src/ast/ast.h"
#include "src/base/hashmap.h"
#include "src/deoptimizer.h"
#include "src/frames-inl.h"
#include "src/isolate.h"
#include "src/objects.h"
#include "src/objects/debug-objects-inl.h"

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

bool CompareCoverageBlock(const CoverageBlock& a, const CoverageBlock& b) {
  DCHECK(a.start != kNoSourcePosition);
  DCHECK(b.start != kNoSourcePosition);
  if (a.start == b.start) return a.end > b.end;
  return a.start < b.start;
}

bool HaveSameSourceRange(const CoverageBlock& lhs, const CoverageBlock& rhs) {
  return lhs.start == rhs.start && lhs.end == rhs.end;
}

void MergeDuplicateSingletons(std::vector<CoverageBlock>& blocks) {
  int from = 1;
  int to = 1;
  while (from < static_cast<int>(blocks.size())) {
    CoverageBlock& prev_block = blocks[to - 1];
    CoverageBlock& this_block = blocks[from];

    // Identical ranges should only occur through singleton ranges. Consider the
    // ranges for `for (.) break;`: continuation ranges for both the `break` and
    // `for` statements begin after the trailing semicolon.
    // Such ranges are merged and keep the maximal execution count.
    if (HaveSameSourceRange(prev_block, this_block)) {
      DCHECK_EQ(kNoSourcePosition, this_block.end);  // Singleton range.
      prev_block.count = std::max(prev_block.count, this_block.count);
      from++;  // Do not advance {to} cursor.
      continue;
    }

    DCHECK(!HaveSameSourceRange(prev_block, this_block));

    // Copy if necessary.
    if (from != to) blocks[to] = blocks[from];

    from++;
    to++;
  }
  blocks.resize(to);
}

std::vector<CoverageBlock> GetSortedBlockData(Isolate* isolate,
                                              SharedFunctionInfo* shared) {
  DCHECK(FLAG_block_coverage);
  DCHECK(shared->HasCoverageInfo());

  CoverageInfo* coverage_info =
      CoverageInfo::cast(shared->GetDebugInfo()->coverage_info());

  std::vector<CoverageBlock> result;
  if (coverage_info->SlotCount() == 0) return result;

  for (int i = 0; i < coverage_info->SlotCount(); i++) {
    const int start_pos = coverage_info->StartSourcePosition(i);
    const int until_pos = coverage_info->EndSourcePosition(i);
    const int count = coverage_info->BlockCount(i);

    DCHECK(start_pos != kNoSourcePosition);
    result.emplace_back(start_pos, until_pos, count);
  }

  // Sort according to the block nesting structure.
  std::sort(result.begin(), result.end(), CompareCoverageBlock);

  // Remove duplicate singleton ranges, keeping the max count.
  MergeDuplicateSingletons(result);

  // TODO(jgruber): Merge consecutive ranges with identical counts, remove empty
  // ranges.

  return result;
}

void ResetAllBlockCounts(SharedFunctionInfo* shared) {
  DCHECK(FLAG_block_coverage);
  DCHECK(shared->HasCoverageInfo());

  CoverageInfo* coverage_info =
      CoverageInfo::cast(shared->GetDebugInfo()->coverage_info());

  for (int i = 0; i < coverage_info->SlotCount(); i++) {
    coverage_info->ResetBlockCount(i);
  }
}

// Rewrite position singletons (produced by unconditional control flow
// like return statements, and by continuation counters) into source
// ranges that end at the next sibling range or the end of the parent
// range, whichever comes first.
void RewritePositionSingletonsToRanges(CoverageFunction* function) {
  std::vector<SourceRange> nesting_stack;
  nesting_stack.emplace_back(function->start, function->end);

  const int blocks_count = static_cast<int>(function->blocks.size());
  for (int i = 0; i < blocks_count; i++) {
    CoverageBlock& block = function->blocks[i];

    if (block.start >= function->end) {
      // Continuation singletons past the end of the source file.
      DCHECK_EQ(block.end, kNoSourcePosition);
      nesting_stack.resize(1);
      break;
    }

    while (nesting_stack.back().end <= block.start) {
      nesting_stack.pop_back();
    }

    const SourceRange& parent_range = nesting_stack.back();

    DCHECK_NE(block.start, kNoSourcePosition);
    DCHECK_LE(block.end, parent_range.end);

    if (block.end == kNoSourcePosition) {
      // The current block ends at the next sibling block (if it exists) or the
      // end of the parent block otherwise.
      if (i < blocks_count - 1 &&
          function->blocks[i + 1].start < parent_range.end) {
        block.end = function->blocks[i + 1].start;
      } else {
        block.end = parent_range.end;
      }
    }

    if (i < blocks_count - 1) {
      nesting_stack.emplace_back(block.start, block.end);
    }
  }

  DCHECK_EQ(1, nesting_stack.size());
}
}  // anonymous namespace

Coverage* Coverage::CollectPrecise(Isolate* isolate) {
  DCHECK(!isolate->is_best_effort_code_coverage());
  Coverage* result = Collect(isolate, isolate->code_coverage_mode());
  if (isolate->is_precise_binary_code_coverage()) {
    // We do not have to hold onto feedback vectors for invocations we already
    // reported. So we can reset the list.
    isolate->SetCodeCoverageList(*ArrayList::New(isolate, 0));
  }
  return result;
}

Coverage* Coverage::CollectBestEffort(Isolate* isolate) {
  return Collect(isolate, v8::debug::Coverage::kBestEffort);
}

Coverage* Coverage::Collect(Isolate* isolate,
                            v8::debug::Coverage::Mode collectionMode) {
  SharedToCounterMap counter_map;

  const bool reset_count = collectionMode != v8::debug::Coverage::kBestEffort;

  switch (isolate->code_coverage_mode()) {
    case v8::debug::Coverage::kBlockCount:
    case v8::debug::Coverage::kPreciseBinary:
    case v8::debug::Coverage::kPreciseCount: {
      // Feedback vectors are already listed to prevent losing them to GC.
      DCHECK(isolate->factory()->code_coverage_list()->IsArrayList());
      Handle<ArrayList> list =
          Handle<ArrayList>::cast(isolate->factory()->code_coverage_list());
      for (int i = 0; i < list->Length(); i++) {
        FeedbackVector* vector = FeedbackVector::cast(list->Get(i));
        SharedFunctionInfo* shared = vector->shared_function_info();
        DCHECK(shared->IsSubjectToDebugging());
        uint32_t count = static_cast<uint32_t>(vector->invocation_count());
        if (reset_count) vector->clear_invocation_count();
        counter_map.Add(shared, count);
      }
      break;
    }
    case v8::debug::Coverage::kBestEffort: {
      DCHECK(!isolate->factory()->code_coverage_list()->IsArrayList());
      DCHECK_EQ(v8::debug::Coverage::kBestEffort, collectionMode);
      HeapIterator heap_iterator(isolate->heap());
      while (HeapObject* current_obj = heap_iterator.next()) {
        if (!current_obj->IsFeedbackVector()) continue;
        FeedbackVector* vector = FeedbackVector::cast(current_obj);
        SharedFunctionInfo* shared = vector->shared_function_info();
        if (!shared->IsSubjectToDebugging()) continue;
        uint32_t count = static_cast<uint32_t>(vector->invocation_count());
        counter_map.Add(shared, count);
      }
      break;
    }
  }

  // Iterate shared function infos of every script and build a mapping
  // between source ranges and invocation counts.
  Coverage* result = new Coverage();
  Script::Iterator scripts(isolate);
  while (Script* script = scripts.Next()) {
    if (!script->IsUserJavaScript()) continue;

    // Create and add new script data.
    Handle<Script> script_handle(script, isolate);
    result->emplace_back(script_handle);
    std::vector<CoverageFunction>* functions = &result->back().functions;

    std::vector<SharedFunctionInfo*> sorted;

    {
      // Sort functions by start position, from outer to inner functions.
      SharedFunctionInfo::ScriptIterator infos(script_handle);
      while (SharedFunctionInfo* info = infos.Next()) {
        sorted.push_back(info);
      }
      std::sort(sorted.begin(), sorted.end(), CompareSharedFunctionInfo);
    }

    // Stack to track nested functions, referring function by index.
    std::vector<size_t> nesting;

    // Use sorted list to reconstruct function nesting.
    for (SharedFunctionInfo* info : sorted) {
      int start = StartPosition(info);
      int end = info->end_position();
      uint32_t count = counter_map.Get(info);
      // Find the correct outer function based on start position.
      while (!nesting.empty() && functions->at(nesting.back()).end <= start) {
        nesting.pop_back();
      }
      if (count != 0) {
        switch (collectionMode) {
          case v8::debug::Coverage::kBlockCount:
          case v8::debug::Coverage::kPreciseCount:
            break;
          case v8::debug::Coverage::kPreciseBinary:
            count = info->has_reported_binary_coverage() ? 0 : 1;
            info->set_has_reported_binary_coverage(true);
            break;
          case v8::debug::Coverage::kBestEffort:
            count = 1;
            break;
        }
      }
      // Only include a function range if it has a non-0 count, or
      // if it is directly nested inside a function with non-0 count.
      if (count != 0 ||
          (!nesting.empty() && functions->at(nesting.back()).count != 0)) {
        Handle<String> name(info->DebugName(), isolate);
        nesting.push_back(functions->size());
        functions->emplace_back(start, end, count, name);

        if (FLAG_block_coverage && info->HasCoverageInfo()) {
          CoverageFunction* function = &functions->back();
          function->has_block_coverage = true;
          function->blocks = GetSortedBlockData(isolate, info);
          RewritePositionSingletonsToRanges(function);
          // TODO(jgruber): Filter empty block ranges with empty parent ranges.
          // We should probably unify handling of function & block ranges.
          if (reset_count) ResetAllBlockCounts(info);
        }
      }
    }

    // Remove entries for scripts that have no coverage.
    if (functions->empty()) result->pop_back();
  }
  return result;
}

void Coverage::SelectMode(Isolate* isolate, debug::Coverage::Mode mode) {
  switch (mode) {
    case debug::Coverage::kBestEffort:
      if (FLAG_block_coverage) isolate->debug()->RemoveAllCoverageInfos();
      isolate->SetCodeCoverageList(isolate->heap()->undefined_value());
      break;
    case debug::Coverage::kBlockCount:
    case debug::Coverage::kPreciseBinary:
    case debug::Coverage::kPreciseCount: {
      HandleScope scope(isolate);
      // Remove all optimized function. Optimized and inlined functions do not
      // increment invocation count.
      Deoptimizer::DeoptimizeAll(isolate);
      // Collect existing feedback vectors.
      std::vector<Handle<FeedbackVector>> vectors;
      {
        HeapIterator heap_iterator(isolate->heap());
        while (HeapObject* current_obj = heap_iterator.next()) {
          if (current_obj->IsSharedFunctionInfo()) {
            SharedFunctionInfo* shared = SharedFunctionInfo::cast(current_obj);
            shared->set_has_reported_binary_coverage(false);
          } else if (current_obj->IsFeedbackVector()) {
            FeedbackVector* vector = FeedbackVector::cast(current_obj);
            SharedFunctionInfo* shared = vector->shared_function_info();
            if (!shared->IsSubjectToDebugging()) continue;
            vectors.emplace_back(vector, isolate);
          }
        }
      }
      // Add collected feedback vectors to the root list lest we lose them to
      // GC.
      Handle<ArrayList> list =
          ArrayList::New(isolate, static_cast<int>(vectors.size()));
      for (const auto& vector : vectors) list = ArrayList::Add(list, vector);
      isolate->SetCodeCoverageList(*list);
      break;
    }
  }
  isolate->set_code_coverage_mode(mode);
}

}  // namespace internal
}  // namespace v8
