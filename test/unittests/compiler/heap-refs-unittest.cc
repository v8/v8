// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/heap-refs.h"

#include <optional>

#include "src/base/platform/platform.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/js-heap-broker.h"
#include "src/heap/heap-inl.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"
#include "src/objects/field-index-inl.h"
#include "src/objects/js-objects-inl.h"
#include "test/unittests/heap/heap-utils.h"

namespace v8::internal::compiler {

using HeapRefsTest = TestWithHeapInternalsAndContext;

namespace {

class ConstantDataPropertyReader final : public v8::base::Thread {
 public:
  ConstantDataPropertyReader(Isolate* isolate, JSHeapBroker* broker,
                             OptimizedCompilationInfo* info,
                             CompilationDependencies* dependencies,
                             JSObjectRef holder, FieldIndex field_index)
      : v8::base::Thread(Options("ConstantDataPropertyReader")),
        isolate_(isolate),
        broker_(broker),
        info_(info),
        dependencies_(dependencies),
        holder_(holder),
        field_index_(field_index) {}

  void Run() override {
    LocalIsolate local_isolate(isolate_, ThreadKind::kBackground);
    broker_->AttachLocalIsolate(info_, &local_isolate);
    {
      UnparkedScope unparked_scope(local_isolate.heap());
      CurrentHeapBrokerScope current_broker(broker_);
      result_ = holder_.GetOwnFastConstantDataProperty(
          broker_, Representation::HeapObject(), field_index_, dependencies_);
    }
    broker_->DetachLocalIsolate(info_);
  }

  OptionalObjectRef result() const { return result_; }

 private:
  Isolate* const isolate_;
  JSHeapBroker* const broker_;
  OptimizedCompilationInfo* const info_;
  CompilationDependencies* const dependencies_;
  const JSObjectRef holder_;
  const FieldIndex field_index_;
  OptionalObjectRef result_;
};

}  // namespace

// A background thread must not introspect a field value that is still a
// pending allocation: its map word may not be initialized yet.
TEST_F(HeapRefsTest, GetOwnFastConstantDataPropertyOfPendingAllocation) {
  Isolate* isolate = i_isolate();
  HandleScope handle_scope(isolate);
  Zone zone(isolate->allocator(), ZONE_NAME);

  DirectHandle<JSObject> holder = Cast<JSObject>(RunJS("({p: {}})"));
  FieldIndex field_index =
      FieldIndex::ForDescriptor(holder->map(), InternalIndex(0));
  CHECK(field_index.is_inobject());

  OptimizedCompilationInfo info("test", &zone, CodeKind::TURBOFAN_JS);
  JSHeapBroker broker(isolate, &zone);
  JSHeapBrokerScopeForTesting broker_scope(&broker, isolate, &zone);
  CurrentHeapBrokerScope current_broker(&broker);
  CompilationDependencies dependencies(&broker, &zone);

  std::optional<JSObjectRef> holder_ref;
  {
    CompilationHandleScope compilation_scope(isolate, &info);
    heap()->PublishMainThreadPendingAllocations();
    broker.InitializeAndStartSerializing(isolate->native_context());
    holder_ref = MakeRef(&broker, *holder);
    broker.StopSerializing();
  }
  heap()->PublishMainThreadPendingAllocations();

  // Store an allocated but not yet initialized object into the field.
  AlwaysAllocateScopeForTesting always_allocate(heap());
  const int size = FixedArray::SizeFor(0);
  Tagged<HeapObject> pending =
      AllocateRawForTest(size, AllocationType::kYoung).ToObjectChecked();
  CHECK(heap()->IsPendingAllocation(pending));
  memset(reinterpret_cast<void*>(pending.address()), 0xcc, size);
  Tagged<Object> old_value =
      TaggedField<Object>::Relaxed_Load(*holder, field_index.offset());
  TaggedField<Object>::Relaxed_Store(*holder, field_index.offset(), pending);

  ConstantDataPropertyReader reader(isolate, &broker, &info, &dependencies,
                                    *holder_ref, field_index);
  CHECK(reader.Start());
  reader.Join();
  EXPECT_FALSE(reader.result().has_value());

  TaggedField<Object>::Relaxed_Store(*holder, field_index.offset(), old_value);
  heap()->CreateFillerObjectAt(pending.address(), size);
}

}  // namespace v8::internal::compiler
