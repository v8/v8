// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stubs.h"
#include "src/compiler/graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/globals.h"
#include "src/handles.h"
#include "test/cctest/compiler/function-tester.h"

#if V8_TURBOFAN_TARGET

using namespace v8::internal;
using namespace v8::internal::compiler;


class StringLengthStubTF : public CodeStub {
 public:
  explicit StringLengthStubTF(Isolate* isolate) : CodeStub(isolate) {}

  StringLengthStubTF(uint32_t key, Isolate* isolate) : CodeStub(key, isolate) {}

  CallInterfaceDescriptor GetCallInterfaceDescriptor() OVERRIDE {
    return LoadDescriptor(isolate());
  };

  Handle<Code> GenerateCode() OVERRIDE {
    CompilationInfoWithZone info(this, isolate());
    Graph graph(info.zone());
    CallDescriptor* descriptor = Linkage::ComputeIncoming(info.zone(), &info);
    RawMachineAssembler m(isolate(), &graph, descriptor->GetMachineSignature());

    Node* str = m.Load(kMachAnyTagged, m.Parameter(0),
                       m.Int32Constant(JSValue::kValueOffset - kHeapObjectTag));
    Node* len = m.Load(kMachAnyTagged, str,
                       m.Int32Constant(String::kLengthOffset - kHeapObjectTag));
    m.Return(len);

    return Pipeline::GenerateCodeForTesting(&info, &graph, m.Export());
  }

  Major MajorKey() const OVERRIDE { return StringLength; };
  Code::Kind GetCodeKind() const OVERRIDE { return Code::HANDLER; }
  InlineCacheState GetICState() const OVERRIDE { return MONOMORPHIC; }
  ExtraICState GetExtraICState() const OVERRIDE { return Code::LOAD_IC; }
  Code::StubType GetStubType() const OVERRIDE { return Code::FAST; }

 private:
  DISALLOW_COPY_AND_ASSIGN(StringLengthStubTF);
};


TEST(RunStringLengthStubTF) {
  HandleAndZoneScope scope;
  Isolate* isolate = scope.main_isolate();
  Zone* zone = scope.main_zone();

  // Create code and an accompanying descriptor.
  StringLengthStubTF stub(isolate);
  Handle<Code> code = stub.GenerateCode();
  CompilationInfo info(&stub, isolate, zone);
  CallDescriptor* descriptor = Linkage::ComputeIncoming(zone, &info);

  // Create a function to call the code using the descriptor.
  Graph graph(zone);
  CommonOperatorBuilder common(zone);
  // FunctionTester (ab)uses a 2-argument function
  Node* start = graph.NewNode(common.Start(2));
  // Parameter 0 is the receiver
  Node* receiverParam = graph.NewNode(common.Parameter(1), start);
  Node* nameParam = graph.NewNode(common.Parameter(2), start);
  Unique<HeapObject> u = Unique<HeapObject>::CreateImmovable(code);
  Node* theCode = graph.NewNode(common.HeapConstant(u));
  Node* dummyContext = graph.NewNode(common.NumberConstant(0.0));
  Node* call = graph.NewNode(common.Call(descriptor), theCode, receiverParam,
                             nameParam, dummyContext, start, start);
  Node* ret = graph.NewNode(common.Return(), call, call, start);
  Node* end = graph.NewNode(common.End(), ret);
  graph.SetStart(start);
  graph.SetEnd(end);
  FunctionTester ft(&graph);

  // Actuall call through to the stub, verifying its result.
  const char* testString = "Und das Lamm schrie HURZ!";
  Handle<JSReceiver> receiverArg =
      Object::ToObject(isolate, ft.Val(testString)).ToHandleChecked();
  Handle<String> nameArg = ft.Val("length");
  Handle<Object> result = ft.Call(receiverArg, nameArg).ToHandleChecked();
  CHECK_EQ(static_cast<int>(strlen(testString)), Smi::cast(*result)->value());
}

#endif  // V8_TURBOFAN_TARGET
