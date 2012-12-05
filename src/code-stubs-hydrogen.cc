// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "code-stubs.h"
#include "hydrogen.h"
#include "lithium.h"

namespace v8 {
namespace internal {


Handle<Code> HydrogenCodeStub::CodeFromGraph(HGraph* graph) {
  graph->OrderBlocks();
  graph->AssignDominators();
  graph->CollectPhis();
  graph->InsertRepresentationChanges();
  graph->EliminateRedundantBoundsChecks();
  LChunk* chunk = LChunk::NewChunk(graph);
  ASSERT(chunk != NULL);
  Handle<Code> stub = chunk->Codegen(Code::COMPILED_STUB);
  return stub;
}


class CodeStubGraphBuilderBase : public HGraphBuilder {
 public:
  CodeStubGraphBuilderBase(Isolate* isolate, HydrogenCodeStub* stub)
      : HGraphBuilder(&info_), info_(stub, isolate) {}
  virtual bool BuildGraph();

 protected:
  virtual void BuildCodeStub() = 0;
  HParameter* GetParameter(int parameter) { return parameters_[parameter]; }
  HydrogenCodeStub* stub() { return info_.code_stub(); }

 private:
  SmartArrayPointer<HParameter*> parameters_;
  CompilationInfoWithZone info_;
};


bool CodeStubGraphBuilderBase::BuildGraph() {
  if (FLAG_trace_hydrogen) {
    PrintF("-----------------------------------------------------------\n");
    PrintF("Compiling stub using hydrogen\n");
    HTracer::Instance()->TraceCompilation(&info_);
  }
  HBasicBlock* next_block = graph()->CreateBasicBlock();
  next_block->SetInitialEnvironment(graph()->start_environment());
  HGoto* jump = new(zone()) HGoto(next_block);
  graph()->entry_block()->Finish(jump);
  set_current_block(next_block);

  int major_key = stub()->MajorKey();
  CodeStubInterfaceDescriptor* descriptor =
      info_.isolate()->code_stub_interface_descriptor(major_key);
  if (descriptor->register_param_count_ < 0) {
    stub()->InitializeInterfaceDescriptor(info_.isolate(), descriptor);
  }
  parameters_.Reset(new HParameter*[descriptor->register_param_count_]);

  HGraph* graph = this->graph();
  Zone* zone = this->zone();
  for (int i = 0; i < descriptor->register_param_count_; ++i) {
    HParameter* param = new(zone) HParameter(i);
    AddInstruction(param);
    graph->start_environment()->Push(param);
    parameters_[i] = param;
  }
  AddSimulate(BailoutId::StubEntry());

  BuildCodeStub();

  return true;
}

template <class Stub>
class CodeStubGraphBuilder: public CodeStubGraphBuilderBase {
 public:
  explicit CodeStubGraphBuilder(Stub* stub)
      : CodeStubGraphBuilderBase(Isolate::Current(), stub) {}

 protected:
  virtual void BuildCodeStub();
  Stub* casted_stub() { return static_cast<Stub*>(stub()); }
};


template <>
void CodeStubGraphBuilder<KeyedLoadFastElementStub>::BuildCodeStub() {
  Zone* zone = this->zone();

  HInstruction* load = BuildUncheckedMonomorphicElementAccess(
      GetParameter(0), GetParameter(1), NULL, NULL,
      casted_stub()->is_js_array(), casted_stub()->elements_kind(), false);
  AddInstruction(load);

  HReturn* ret = new(zone) HReturn(load);
  current_block()->Finish(ret);
}


Handle<Code> KeyedLoadFastElementStub::GenerateCode() {
  CodeStubGraphBuilder<KeyedLoadFastElementStub> builder(this);
  return CodeFromGraph(builder.CreateGraph());
}


} }  // namespace v8::internal
