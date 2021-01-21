// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/heap/unified-heap-utils.h"

#include "include/cppgc/platform.h"
#include "include/v8-cppgc.h"
#include "src/api/api-inl.h"
#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/heap.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

UnifiedHeapTest::UnifiedHeapTest() {
  isolate()->heap()->ConfigureCppHeap(std::make_unique<CppHeapCreateParams>());
}

void UnifiedHeapTest::CollectGarbageWithEmbedderStack(
    cppgc::Heap::SweepingType sweeping_type) {
  heap()->SetEmbedderStackStateForNextFinalization(
      EmbedderHeapTracer::EmbedderStackState::kMayContainHeapPointers);
  CollectGarbage(OLD_SPACE);
  if (sweeping_type == cppgc::Heap::SweepingType::kAtomic) {
    cpp_heap().AsBase().sweeper().FinishIfRunning();
  }
}

void UnifiedHeapTest::CollectGarbageWithoutEmbedderStack(
    cppgc::Heap::SweepingType sweeping_type) {
  heap()->SetEmbedderStackStateForNextFinalization(
      EmbedderHeapTracer::EmbedderStackState::kNoHeapPointers);
  CollectGarbage(OLD_SPACE);
  if (sweeping_type == cppgc::Heap::SweepingType::kAtomic) {
    cpp_heap().AsBase().sweeper().FinishIfRunning();
  }
}

CppHeap& UnifiedHeapTest::cpp_heap() const {
  return *CppHeap::From(isolate()->heap()->cpp_heap());
}

cppgc::AllocationHandle& UnifiedHeapTest::allocation_handle() {
  return cpp_heap().object_allocator();
}

// static
v8::Local<v8::Object> WrapperHelper::CreateWrapper(
    v8::Local<v8::Context> context, void* wrappable_object,
    const char* class_name) {
  v8::EscapableHandleScope scope(context->GetIsolate());
  v8::Local<v8::FunctionTemplate> function_t =
      v8::FunctionTemplate::New(context->GetIsolate());
  if (strlen(class_name) != 0) {
    function_t->SetClassName(
        v8::String::NewFromUtf8(v8::Isolate::GetCurrent(), class_name)
            .ToLocalChecked());
  }
  v8::Local<v8::ObjectTemplate> instance_t = function_t->InstanceTemplate();
  instance_t->SetInternalFieldCount(2);
  v8::Local<v8::Function> function =
      function_t->GetFunction(context).ToLocalChecked();
  v8::Local<v8::Object> instance =
      function->NewInstance(context).ToLocalChecked();
  instance->SetAlignedPointerInInternalField(0, wrappable_object);
  instance->SetAlignedPointerInInternalField(1, wrappable_object);
  CHECK(!instance.IsEmpty());
  i::Handle<i::JSReceiver> js_obj = v8::Utils::OpenHandle(*instance);
  CHECK_EQ(i::JS_API_OBJECT_TYPE, js_obj->map().instance_type());
  return scope.Escape(instance);
}

// static
void WrapperHelper::ResetWrappableConnection(v8::Local<v8::Object> api_object) {
  api_object->SetAlignedPointerInInternalField(0, nullptr);
  api_object->SetAlignedPointerInInternalField(1, nullptr);
}

// static
void WrapperHelper::SetWrappableConnection(v8::Local<v8::Object> api_object,
                                           void* field1, void* field2) {
  api_object->SetAlignedPointerInInternalField(0, field1);
  api_object->SetAlignedPointerInInternalField(1, field2);
}

}  // namespace internal
}  // namespace v8
