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


static LChunk* OptimizeGraph(HGraph* graph) {
  AssertNoAllocation no_gc;
  NoHandleAllocation no_handles;
  NoHandleDereference no_deref;

  ASSERT(graph != NULL);
  SmartArrayPointer<char> bailout_reason;
  if (!graph->Optimize(&bailout_reason)) {
    FATAL(bailout_reason.is_empty() ? "unknown" : *bailout_reason);
  }
  LChunk* chunk = LChunk::NewChunk(graph);
  if (chunk == NULL) {
    FATAL(graph->info()->bailout_reason());
  }
  return chunk;
}


class CodeStubGraphBuilderBase : public HGraphBuilder {
 public:
  CodeStubGraphBuilderBase(Isolate* isolate, HydrogenCodeStub* stub)
      : HGraphBuilder(&info_), info_(stub, isolate), context_(NULL) {}
  virtual bool BuildGraph();

 protected:
  virtual void BuildCodeStub() = 0;
  HParameter* GetParameter(int parameter) { return parameters_[parameter]; }
  CompilationInfo* info() { return &info_; }
  HydrogenCodeStub* stub() { return info_.code_stub(); }
  HContext* context() { return context_; }
  Isolate* isolate() { return info_.isolate(); }

 private:
  SmartArrayPointer<HParameter*> parameters_;
  CompilationInfoWithZone info_;
  HContext* context_;
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

  HConstant* undefined_constant = new(zone()) HConstant(
      isolate()->factory()->undefined_value(), Representation::Tagged());
  AddInstruction(undefined_constant);
  graph()->set_undefined_constant(undefined_constant);

  HGraph* graph = this->graph();
  Zone* zone = this->zone();
  for (int i = 0; i < descriptor->register_param_count_; ++i) {
    HParameter* param =
        new(zone) HParameter(i, HParameter::REGISTER_PARAMETER);
    AddInstruction(param);
    graph->start_environment()->Push(param);
    parameters_[i] = param;
  }

  context_ = new(zone) HContext();
  AddInstruction(context_);

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
      casted_stub()->is_js_array(), casted_stub()->elements_kind(),
      false, Representation::Tagged());
  AddInstruction(load);

  HReturn* ret = new(zone) HReturn(load, context());
  current_block()->Finish(ret);
}


Handle<Code> KeyedLoadFastElementStub::GenerateCode() {
  CodeStubGraphBuilder<KeyedLoadFastElementStub> builder(this);
  LChunk* chunk = OptimizeGraph(builder.CreateGraph());
  return chunk->Codegen(Code::COMPILED_STUB);
}


template <>
void CodeStubGraphBuilder<TransitionElementsKindStub>::BuildCodeStub() {
  Zone* zone = this->zone();

  HValue* js_array = GetParameter(0);
  HValue* map = GetParameter(1);

  info()->MarkAsSavesCallerDoubles();

  AddInstruction(new(zone) HTrapAllocationMemento(js_array));

  HInstruction* array_length =
      AddInstruction(new(zone) HJSArrayLength(js_array,
                                              js_array,
                                              HType::Smi()));

  Heap* heap = isolate()->heap();
  const int kMinFreeNewSpaceAfterGC =
      ((heap->InitialSemiSpaceSize() - sizeof(FixedArrayBase)) / 2) /
      kDoubleSize;

  HConstant* max_alloc_size =
      new(zone) HConstant(kMinFreeNewSpaceAfterGC, Representation::Integer32());
  AddInstruction(max_alloc_size);
  AddInstruction(new(zone) HBoundsCheck(array_length, max_alloc_size));

  current_block()->UpdateEnvironment(new(zone) HEnvironment(zone));

  IfBuilder if_builder(this);

  if_builder.BeginTrue(array_length, graph()->GetConstant0(), Token::EQ);

  // Nothing to do, just change the map.

  if_builder.BeginFalse();

  HInstruction* elements =
      AddInstruction(new(zone) HLoadElements(js_array, js_array));

  HInstruction* elements_length =
      AddInstruction(new(zone) HFixedArrayBaseLength(elements));

  ElementsKind to_kind = casted_stub()->to_kind();
  HValue* new_elements =
      BuildAllocateElements(context(), to_kind, elements_length);

  // Fast elements kinds need to be initialized in case statements below cause a
  // garbage collection.
  Factory* factory = isolate()->factory();

  ASSERT(!IsFastSmiElementsKind(to_kind));
  double nan_double = FixedDoubleArray::hole_nan_as_double();
  HValue* hole = IsFastObjectElementsKind(to_kind)
      ? AddInstruction(new(zone) HConstant(factory->the_hole_value(),
                                           Representation::Tagged()))
      : AddInstruction(new(zone) HConstant(nan_double,
                                           Representation::Double()));

  LoopBuilder builder(this, context(), LoopBuilder::kPostIncrement);

  HValue* zero = graph()->GetConstant0();
  HValue* start = IsFastElementsKind(to_kind) ? zero : array_length;
  HValue* key = builder.BeginBody(start, elements_length, Token::LT);

  AddInstruction(new(zone) HStoreKeyed(new_elements, key, hole, to_kind));
  AddSimulate(BailoutId::StubEntry(), REMOVABLE_SIMULATE);

  builder.EndBody();

  BuildCopyElements(context(), elements,
                    casted_stub()->from_kind(), new_elements,
                    to_kind, array_length);

  AddInstruction(new(zone) HStoreNamedField(js_array,
                                            factory->elements_field_symbol(),
                                            new_elements, true,
                                            JSArray::kElementsOffset));
  AddSimulate(BailoutId::StubEntry());

  if_builder.End();

  AddInstruction(new(zone) HStoreNamedField(js_array, factory->length_symbol(),
                                            map, true, JSArray::kMapOffset));
  AddSimulate(BailoutId::StubEntry());

  HReturn* ret = new(zone) HReturn(js_array, context());
  current_block()->Finish(ret);
}


Handle<Code> TransitionElementsKindStub::GenerateCode() {
  CodeStubGraphBuilder<TransitionElementsKindStub> builder(this);
  LChunk* chunk = OptimizeGraph(builder.CreateGraph());
  return chunk->Codegen(Code::COMPILED_STUB);
}


} }  // namespace v8::internal
