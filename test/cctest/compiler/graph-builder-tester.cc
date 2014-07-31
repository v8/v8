// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/compiler/graph-builder-tester.h"
#include "src/compiler/pipeline.h"

namespace v8 {
namespace internal {
namespace compiler {

MachineCallHelper::MachineCallHelper(Zone* zone,
                                     MachineCallDescriptorBuilder* builder)
    : CallHelper(zone->isolate()),
      call_descriptor_builder_(builder),
      parameters_(NULL),
      graph_(NULL) {}


void MachineCallHelper::InitParameters(GraphBuilder* builder,
                                       CommonOperatorBuilder* common) {
  ASSERT_EQ(NULL, parameters_);
  graph_ = builder->graph();
  if (parameter_count() == 0) return;
  parameters_ = builder->graph()->zone()->NewArray<Node*>(parameter_count());
  for (int i = 0; i < parameter_count(); ++i) {
    parameters_[i] = builder->NewNode(common->Parameter(i));
  }
}


byte* MachineCallHelper::Generate() {
  ASSERT(parameter_count() == 0 || parameters_ != NULL);
  if (code_.is_null()) {
    Zone* zone = graph_->zone();
    CompilationInfo info(zone->isolate(), zone);
    Linkage linkage(&info, call_descriptor_builder_->BuildCallDescriptor(zone));
    Pipeline pipeline(&info);
    code_ = pipeline.GenerateCodeForMachineGraph(&linkage, graph_);
  }
  return code_.ToHandleChecked()->entry();
}


void MachineCallHelper::VerifyParameters(
    int parameter_count, MachineRepresentation* parameter_types) {
  CHECK_EQ(this->parameter_count(), parameter_count);
  const MachineRepresentation* expected_types =
      call_descriptor_builder_->parameter_types();
  for (int i = 0; i < parameter_count; i++) {
    CHECK_EQ(expected_types[i], parameter_types[i]);
  }
}


Node* MachineCallHelper::Parameter(int offset) {
  ASSERT_NE(NULL, parameters_);
  ASSERT(0 <= offset && offset < parameter_count());
  return parameters_[offset];
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
