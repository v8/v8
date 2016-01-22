// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_PROFILER_SAMPLING_HEAP_PROFILER_H_
#define V8_PROFILER_SAMPLING_HEAP_PROFILER_H_

#include <deque>
#include <map>
#include <set>
#include "include/v8-profiler.h"
#include "src/heap/heap.h"
#include "src/profiler/strings-storage.h"

namespace v8 {

namespace base {
class RandomNumberGenerator;
}

namespace internal {


class AllocationProfile : public v8::AllocationProfile {
 public:
  AllocationProfile() : nodes_() {}

  Node* GetRootNode() override {
    return nodes_.size() == 0 ? nullptr : &nodes_.front();
  }

  std::deque<Node>& nodes() { return nodes_; }

 private:
  std::deque<Node> nodes_;

  DISALLOW_COPY_AND_ASSIGN(AllocationProfile);
};

class SamplingHeapProfiler : public InlineAllocationObserver {
 public:
  SamplingHeapProfiler(Heap* heap, StringsStorage* names, uint64_t rate,
                       int stack_depth);
  ~SamplingHeapProfiler();

  v8::AllocationProfile* GetAllocationProfile();

  void Step(int bytes_allocated, Address soon_object, size_t size) override;
  intptr_t GetNextStepSize() override {
    return GetNextSampleInterval(random_, rate_);
  }

  StringsStorage* names() const { return names_; }

  class FunctionInfo {
   public:
    FunctionInfo(SharedFunctionInfo* shared, StringsStorage* names);
    explicit FunctionInfo(const char* name)
        : name_(name),
          script_name_(""),
          script_id_(v8::UnboundScript::kNoScriptId),
          start_position_(0) {}

    const char* get_name() const { return name_; }
    const char* get_script_name() const { return script_name_; }
    int get_script_id() const { return script_id_; }
    int get_start_position() const { return start_position_; }

   private:
    const char* const name_;
    const char* script_name_;
    int script_id_;
    const int start_position_;
  };

  class SampledAllocation {
   public:
    SampledAllocation(SamplingHeapProfiler* sampling_heap_profiler,
                      Isolate* isolate, Local<Value> local, size_t size,
                      int max_frames);
    ~SampledAllocation() {
      for (auto info : stack_) {
        delete info;
      }
      global_.Reset();  // drop the reference.
    }
    size_t get_size() const { return size_; }
    const std::vector<FunctionInfo*>& get_stack() const { return stack_; }

   private:
    static void OnWeakCallback(const WeakCallbackInfo<SampledAllocation>& data);

    SamplingHeapProfiler* const sampling_heap_profiler_;
    Global<Value> global_;
    std::vector<FunctionInfo*> stack_;
    const size_t size_;

    DISALLOW_COPY_AND_ASSIGN(SampledAllocation);
  };

 private:
  using Node = v8::AllocationProfile::Node;

  Heap* heap() const { return heap_; }

  void SampleObject(Address soon_object, size_t size);

  static intptr_t GetNextSampleInterval(base::RandomNumberGenerator* random,
                                        uint64_t rate);

  // Methods that construct v8::AllocationProfile.
  Node* AddStack(AllocationProfile* profile,
                 const std::map<int, Script*>& scripts,
                 const std::vector<FunctionInfo*>& stack);
  Node* FindOrAddChildNode(AllocationProfile* profile,
                           const std::map<int, Script*>& scripts, Node* parent,
                           FunctionInfo* function_info);
  Node* AllocateNode(AllocationProfile* profile,
                     const std::map<int, Script*>& scripts,
                     FunctionInfo* function_info);

  Isolate* const isolate_;
  Heap* const heap_;
  base::RandomNumberGenerator* const random_;
  StringsStorage* const names_;
  std::set<SampledAllocation*> samples_;
  const uint64_t rate_;
  const int stack_depth_;
};


}  // namespace internal
}  // namespace v8

#endif  // V8_PROFILER_SAMPLING_HEAP_PROFILER_H_
