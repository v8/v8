// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/sampling-heap-profiler.h"

#include <stdint.h>
#include <memory>
#include "src/api.h"
#include "src/base/utils/random-number-generator.h"
#include "src/frames-inl.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/profiler/strings-storage.h"

namespace v8 {
namespace internal {

// We sample with a Poisson process, with constant average sampling interval.
// This follows the exponential probability distribution with parameter
// λ = 1/rate where rate is the average number of bytes between samples.
//
// Let u be a uniformly distributed random number between 0 and 1, then
// next_sample = (- ln u) / λ
intptr_t SamplingAllocationObserver::GetNextSampleInterval(uint64_t rate) {
  if (FLAG_sampling_heap_profiler_suppress_randomness) {
    return rate;
  }
  double u = random_->NextDouble();
  double next = (-std::log(u)) * rate;
  return next < kPointerSize
             ? kPointerSize
             : (next > INT_MAX ? INT_MAX : static_cast<intptr_t>(next));
}

SamplingHeapProfiler::SamplingHeapProfiler(Heap* heap, StringsStorage* names,
                                           uint64_t rate, int stack_depth)
    : isolate_(heap->isolate()),
      heap_(heap),
      new_space_observer_(new SamplingAllocationObserver(
          heap_, rate, rate, this, heap->isolate()->random_number_generator())),
      other_spaces_observer_(new SamplingAllocationObserver(
          heap_, rate, rate, this, heap->isolate()->random_number_generator())),
      names_(names),
      samples_(),
      stack_depth_(stack_depth) {
  heap->new_space()->AddAllocationObserver(new_space_observer_.get());
  AllSpaces spaces(heap);
  for (Space* space = spaces.next(); space != NULL; space = spaces.next()) {
    if (space != heap->new_space()) {
      space->AddAllocationObserver(other_spaces_observer_.get());
    }
  }
}


SamplingHeapProfiler::~SamplingHeapProfiler() {
  heap_->new_space()->RemoveAllocationObserver(new_space_observer_.get());
  AllSpaces spaces(heap_);
  for (Space* space = spaces.next(); space != NULL; space = spaces.next()) {
    if (space != heap_->new_space()) {
      space->RemoveAllocationObserver(other_spaces_observer_.get());
    }
  }

  // Clear samples and drop all the weak references we are keeping.
  std::set<SampledAllocation*>::iterator it;
  for (it = samples_.begin(); it != samples_.end(); ++it) {
    delete *it;
  }
  std::set<SampledAllocation*> empty;
  samples_.swap(empty);
}


void SamplingHeapProfiler::SampleObject(Address soon_object, size_t size) {
  DisallowHeapAllocation no_allocation;

  HandleScope scope(isolate_);
  HeapObject* heap_object = HeapObject::FromAddress(soon_object);
  Handle<Object> obj(heap_object, isolate_);

  // Mark the new block as FreeSpace to make sure the heap is iterable while we
  // are taking the sample.
  heap()->CreateFillerObjectAt(soon_object, static_cast<int>(size));

  Local<v8::Value> loc = v8::Utils::ToLocal(obj);

  SampledAllocation* sample =
      new SampledAllocation(this, isolate_, loc, size, stack_depth_);
  samples_.insert(sample);
}


void SamplingHeapProfiler::SampledAllocation::OnWeakCallback(
    const WeakCallbackInfo<SampledAllocation>& data) {
  SampledAllocation* sample = data.GetParameter();
  sample->sampling_heap_profiler_->samples_.erase(sample);
  delete sample;
}


SamplingHeapProfiler::FunctionInfo::FunctionInfo(SharedFunctionInfo* shared,
                                                 StringsStorage* names)
    : name_(names->GetFunctionName(shared->DebugName())),
      script_name_(""),
      script_id_(v8::UnboundScript::kNoScriptId),
      start_position_(shared->start_position()) {
  if (shared->script()->IsScript()) {
    Script* script = Script::cast(shared->script());
    script_id_ = script->id();
    if (script->name()->IsName()) {
      Name* name = Name::cast(script->name());
      script_name_ = names->GetName(name);
    }
  }
}


SamplingHeapProfiler::SampledAllocation::SampledAllocation(
    SamplingHeapProfiler* sampling_heap_profiler, Isolate* isolate,
    Local<Value> local, size_t size, int max_frames)
    : sampling_heap_profiler_(sampling_heap_profiler),
      global_(reinterpret_cast<v8::Isolate*>(isolate), local),
      size_(size) {
  global_.SetWeak(this, OnWeakCallback, WeakCallbackType::kParameter);

  StackTraceFrameIterator it(isolate);
  int frames_captured = 0;
  while (!it.done() && frames_captured < max_frames) {
    JavaScriptFrame* frame = it.frame();
    SharedFunctionInfo* shared = frame->function()->shared();
    stack_.push_back(new FunctionInfo(shared, sampling_heap_profiler->names()));

    frames_captured++;
    it.Advance();
  }

  if (frames_captured == 0) {
    const char* name = nullptr;
    switch (isolate->current_vm_state()) {
      case GC:
        name = "(GC)";
        break;
      case COMPILER:
        name = "(COMPILER)";
        break;
      case OTHER:
        name = "(V8 API)";
        break;
      case EXTERNAL:
        name = "(EXTERNAL)";
        break;
      case IDLE:
        name = "(IDLE)";
        break;
      case JS:
        name = "(JS)";
        break;
    }
    stack_.push_back(new FunctionInfo(name));
  }
}

v8::AllocationProfile::Node* SamplingHeapProfiler::AllocateNode(
    AllocationProfile* profile, const std::map<int, Script*>& scripts,
    FunctionInfo* function_info) {
  DCHECK(function_info->get_name());
  DCHECK(function_info->get_script_name());

  int line = v8::AllocationProfile::kNoLineNumberInfo;
  int column = v8::AllocationProfile::kNoColumnNumberInfo;

  if (function_info->get_script_id() != v8::UnboundScript::kNoScriptId) {
    // Cannot use std::map<T>::at because it is not available on android.
    auto non_const_scripts = const_cast<std::map<int, Script*>&>(scripts);
    Handle<Script> script(non_const_scripts[function_info->get_script_id()]);

    line =
        1 + Script::GetLineNumber(script, function_info->get_start_position());
    column = 1 + Script::GetColumnNumber(script,
                                         function_info->get_start_position());
  }

  profile->nodes().push_back(v8::AllocationProfile::Node(
      {ToApiHandle<v8::String>(isolate_->factory()->InternalizeUtf8String(
           function_info->get_name())),
       ToApiHandle<v8::String>(isolate_->factory()->InternalizeUtf8String(
           function_info->get_script_name())),
       function_info->get_script_id(), function_info->get_start_position(),
       line, column, std::vector<v8::AllocationProfile::Node*>(),
       std::vector<v8::AllocationProfile::Allocation>()}));

  return &profile->nodes().back();
}

v8::AllocationProfile::Node* SamplingHeapProfiler::FindOrAddChildNode(
    AllocationProfile* profile, const std::map<int, Script*>& scripts,
    v8::AllocationProfile::Node* parent, FunctionInfo* function_info) {
  for (v8::AllocationProfile::Node* child : parent->children) {
    if (child->script_id == function_info->get_script_id() &&
        child->start_position == function_info->get_start_position())
      return child;
  }
  v8::AllocationProfile::Node* child =
      AllocateNode(profile, scripts, function_info);
  parent->children.push_back(child);
  return child;
}

v8::AllocationProfile::Node* SamplingHeapProfiler::AddStack(
    AllocationProfile* profile, const std::map<int, Script*>& scripts,
    const std::vector<FunctionInfo*>& stack) {
  v8::AllocationProfile::Node* node = profile->GetRootNode();

  // We need to process the stack in reverse order as the top of the stack is
  // the first element in the list.
  for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
    FunctionInfo* function_info = *it;
    node = FindOrAddChildNode(profile, scripts, node, function_info);
  }
  return node;
}


v8::AllocationProfile* SamplingHeapProfiler::GetAllocationProfile() {
  // To resolve positions to line/column numbers, we will need to look up
  // scripts. Build a map to allow fast mapping from script id to script.
  std::map<int, Script*> scripts;
  {
    Script::Iterator iterator(isolate_);
    Script* script;
    while ((script = iterator.Next())) {
      scripts[script->id()] = script;
    }
  }

  auto profile = new v8::internal::AllocationProfile();

  // Create the root node.
  FunctionInfo function_info("(root)");
  AllocateNode(profile, scripts, &function_info);

  for (SampledAllocation* allocation : samples_) {
    v8::AllocationProfile::Node* node =
        AddStack(profile, scripts, allocation->get_stack());
    node->allocations.push_back({allocation->get_size(), 1});
  }

  return profile;
}


}  // namespace internal
}  // namespace v8
