// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "src/v8.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/graph-builder-tester.h"

#include "src/compiler/node-matchers.h"
#include "src/compiler/representation-change.h"
#include "src/compiler/typer.h"

using namespace v8::internal;
using namespace v8::internal::compiler;

namespace v8 {  // for friendiness.
namespace internal {
namespace compiler {

class RepresentationChangerTester : public HandleAndZoneScope,
                                    public GraphAndBuilders {
 public:
  explicit RepresentationChangerTester(int num_parameters = 0)
      : GraphAndBuilders(main_zone()),
        typer_(main_zone()),
        jsgraph_(main_graph_, &main_common_, &typer_),
        changer_(&jsgraph_, &main_simplified_, &main_machine_, main_isolate()) {
    Node* s = graph()->NewNode(common()->Start(num_parameters));
    graph()->SetStart(s);
  }

  Typer typer_;
  JSGraph jsgraph_;
  RepresentationChanger changer_;

  Isolate* isolate() { return main_isolate(); }
  Graph* graph() { return main_graph_; }
  CommonOperatorBuilder* common() { return &main_common_; }
  JSGraph* jsgraph() { return &jsgraph_; }
  RepresentationChanger* changer() { return &changer_; }

  // TODO(titzer): use ValueChecker / ValueUtil
  void CheckInt32Constant(Node* n, int32_t expected) {
    ValueMatcher<int32_t> m(n);
    CHECK(m.HasValue());
    CHECK_EQ(expected, m.Value());
  }

  void CheckHeapConstant(Node* n, Object* expected) {
    ValueMatcher<Handle<Object> > m(n);
    CHECK(m.HasValue());
    CHECK_EQ(expected, *m.Value());
  }

  void CheckNumberConstant(Node* n, double expected) {
    ValueMatcher<double> m(n);
    CHECK_EQ(IrOpcode::kNumberConstant, n->opcode());
    CHECK(m.HasValue());
    CHECK_EQ(expected, m.Value());
  }

  Node* Parameter(int index = 0) {
    return graph()->NewNode(common()->Parameter(index), graph()->start());
  }

  void CheckTypeError(MachineTypeUnion from, MachineTypeUnion to) {
    changer()->testing_type_errors_ = true;
    changer()->type_error_ = false;
    Node* n = Parameter(0);
    Node* c = changer()->GetRepresentationFor(n, from, to);
    CHECK(changer()->type_error_);
    CHECK_EQ(n, c);
  }

  void CheckNop(MachineTypeUnion from, MachineTypeUnion to) {
    Node* n = Parameter(0);
    Node* c = changer()->GetRepresentationFor(n, from, to);
    CHECK_EQ(n, c);
  }
};
}
}
}  // namespace v8::internal::compiler


static const MachineType all_reps[] = {kRepBit, kRepWord32, kRepWord64,
                                       kRepFloat64, kRepTagged};


// TODO(titzer): lift this to ValueHelper
static const double double_inputs[] = {
    0.0,   -0.0,    1.0,    -1.0,        0.1,         1.4,    -1.7,
    2,     5,       6,      982983,      888,         -999.8, 3.1e7,
    -2e66, 2.3e124, -12e73, V8_INFINITY, -V8_INFINITY};


static const int32_t int32_inputs[] = {
    0,      1,                                -1,
    2,      5,                                6,
    982983, 888,                              -999,
    65535,  static_cast<int32_t>(0xFFFFFFFF), static_cast<int32_t>(0x80000000)};


static const uint32_t uint32_inputs[] = {
    0,      1,   static_cast<uint32_t>(-1),   2,     5,          6,
    982983, 888, static_cast<uint32_t>(-999), 65535, 0xFFFFFFFF, 0x80000000};


TEST(BoolToBit_constant) {
  RepresentationChangerTester r;

  Node* true_node = r.jsgraph()->TrueConstant();
  Node* true_bit =
      r.changer()->GetRepresentationFor(true_node, kRepTagged, kRepBit);
  r.CheckInt32Constant(true_bit, 1);

  Node* false_node = r.jsgraph()->FalseConstant();
  Node* false_bit =
      r.changer()->GetRepresentationFor(false_node, kRepTagged, kRepBit);
  r.CheckInt32Constant(false_bit, 0);
}


TEST(BitToBool_constant) {
  RepresentationChangerTester r;

  for (int i = -5; i < 5; i++) {
    Node* node = r.jsgraph()->Int32Constant(i);
    Node* val = r.changer()->GetRepresentationFor(node, kRepBit, kRepTagged);
    r.CheckHeapConstant(val, i == 0 ? r.isolate()->heap()->false_value()
                                    : r.isolate()->heap()->true_value());
  }
}


TEST(ToTagged_constant) {
  RepresentationChangerTester r;

  for (size_t i = 0; i < ARRAY_SIZE(double_inputs); i++) {
    Node* n = r.jsgraph()->Float64Constant(double_inputs[i]);
    Node* c = r.changer()->GetRepresentationFor(n, kRepFloat64, kRepTagged);
    r.CheckNumberConstant(c, double_inputs[i]);
  }

  for (size_t i = 0; i < ARRAY_SIZE(int32_inputs); i++) {
    Node* n = r.jsgraph()->Int32Constant(int32_inputs[i]);
    Node* c = r.changer()->GetRepresentationFor(n, kRepWord32 | kTypeInt32,
                                                kRepTagged);
    r.CheckNumberConstant(c, static_cast<double>(int32_inputs[i]));
  }

  for (size_t i = 0; i < ARRAY_SIZE(uint32_inputs); i++) {
    Node* n = r.jsgraph()->Int32Constant(uint32_inputs[i]);
    Node* c = r.changer()->GetRepresentationFor(n, kRepWord32 | kTypeUint32,
                                                kRepTagged);
    r.CheckNumberConstant(c, static_cast<double>(uint32_inputs[i]));
  }
}


static void CheckChange(IrOpcode::Value expected, MachineTypeUnion from,
                        MachineTypeUnion to) {
  RepresentationChangerTester r;

  Node* n = r.Parameter();
  Node* c = r.changer()->GetRepresentationFor(n, from, to);

  CHECK_NE(c, n);
  CHECK_EQ(expected, c->opcode());
  CHECK_EQ(n, c->InputAt(0));
}


TEST(SingleChanges) {
  CheckChange(IrOpcode::kChangeBoolToBit, kRepTagged, kRepBit);
  CheckChange(IrOpcode::kChangeBitToBool, kRepBit, kRepTagged);

  CheckChange(IrOpcode::kChangeInt32ToTagged, kRepWord32 | kTypeInt32,
              kRepTagged);
  CheckChange(IrOpcode::kChangeUint32ToTagged, kRepWord32 | kTypeUint32,
              kRepTagged);
  CheckChange(IrOpcode::kChangeFloat64ToTagged, kRepFloat64, kRepTagged);

  CheckChange(IrOpcode::kChangeTaggedToInt32, kRepTagged | kTypeInt32,
              kRepWord32);
  CheckChange(IrOpcode::kChangeTaggedToUint32, kRepTagged | kTypeUint32,
              kRepWord32);
  CheckChange(IrOpcode::kChangeTaggedToFloat64, kRepTagged, kRepFloat64);

  // Int32,Uint32 <-> Float64 are actually machine conversions.
  CheckChange(IrOpcode::kChangeInt32ToFloat64, kRepWord32 | kTypeInt32,
              kRepFloat64);
  CheckChange(IrOpcode::kChangeUint32ToFloat64, kRepWord32 | kTypeUint32,
              kRepFloat64);
  CheckChange(IrOpcode::kChangeFloat64ToInt32, kRepFloat64 | kTypeInt32,
              kRepWord32);
  CheckChange(IrOpcode::kChangeFloat64ToUint32, kRepFloat64 | kTypeUint32,
              kRepWord32);
}


TEST(SignednessInWord32) {
  RepresentationChangerTester r;

  // TODO(titzer): assume that uses of a word32 without a sign mean kTypeInt32.
  CheckChange(IrOpcode::kChangeTaggedToInt32, kRepTagged,
              kRepWord32 | kTypeInt32);
  CheckChange(IrOpcode::kChangeTaggedToUint32, kRepTagged,
              kRepWord32 | kTypeUint32);
  CheckChange(IrOpcode::kChangeInt32ToFloat64, kRepWord32, kRepFloat64);
  CheckChange(IrOpcode::kChangeFloat64ToInt32, kRepFloat64, kRepWord32);
}


TEST(Nops) {
  RepresentationChangerTester r;

  // X -> X is always a nop for any single representation X.
  for (size_t i = 0; i < ARRAY_SIZE(all_reps); i++) {
    r.CheckNop(all_reps[i], all_reps[i]);
  }

  // 32-bit or 64-bit words can be used as branch conditions (kRepBit).
  r.CheckNop(kRepWord32, kRepBit);
  r.CheckNop(kRepWord32, kRepBit | kTypeBool);
  r.CheckNop(kRepWord64, kRepBit);
  r.CheckNop(kRepWord64, kRepBit | kTypeBool);

  // 32-bit words can be used as smaller word sizes and vice versa, because
  // loads from memory implicitly sign or zero extend the value to the
  // full machine word size, and stores implicitly truncate.
  r.CheckNop(kRepWord32, kRepWord8);
  r.CheckNop(kRepWord32, kRepWord16);
  r.CheckNop(kRepWord32, kRepWord32);
  r.CheckNop(kRepWord8, kRepWord32);
  r.CheckNop(kRepWord16, kRepWord32);

  // kRepBit (result of comparison) is implicitly a wordish thing.
  r.CheckNop(kRepBit, kRepWord8);
  r.CheckNop(kRepBit | kTypeBool, kRepWord8);
  r.CheckNop(kRepBit, kRepWord16);
  r.CheckNop(kRepBit | kTypeBool, kRepWord16);
  r.CheckNop(kRepBit, kRepWord32);
  r.CheckNop(kRepBit | kTypeBool, kRepWord32);
  r.CheckNop(kRepBit, kRepWord64);
  r.CheckNop(kRepBit | kTypeBool, kRepWord64);
}


TEST(TypeErrors) {
  RepresentationChangerTester r;

  // Floats cannot be implicitly converted to/from comparison conditions.
  r.CheckTypeError(kRepFloat64, kRepBit);
  r.CheckTypeError(kRepFloat64, kRepBit | kTypeBool);
  r.CheckTypeError(kRepBit, kRepFloat64);
  r.CheckTypeError(kRepBit | kTypeBool, kRepFloat64);

  // Word64 is internal and shouldn't be implicitly converted.
  r.CheckTypeError(kRepWord64, kRepTagged | kTypeBool);
  r.CheckTypeError(kRepWord64, kRepTagged);
  r.CheckTypeError(kRepWord64, kRepTagged | kTypeBool);
  r.CheckTypeError(kRepTagged, kRepWord64);
  r.CheckTypeError(kRepTagged | kTypeBool, kRepWord64);

  // Word64 / Word32 shouldn't be implicitly converted.
  r.CheckTypeError(kRepWord64, kRepWord32);
  r.CheckTypeError(kRepWord32, kRepWord64);
  r.CheckTypeError(kRepWord64, kRepWord32 | kTypeInt32);
  r.CheckTypeError(kRepWord32 | kTypeInt32, kRepWord64);
  r.CheckTypeError(kRepWord64, kRepWord32 | kTypeUint32);
  r.CheckTypeError(kRepWord32 | kTypeUint32, kRepWord64);

  for (size_t i = 0; i < ARRAY_SIZE(all_reps); i++) {
    for (size_t j = 0; j < ARRAY_SIZE(all_reps); j++) {
      if (i == j) continue;
      // Only a single from representation is allowed.
      r.CheckTypeError(all_reps[i] | all_reps[j], kRepTagged);
    }
  }
}


TEST(CompleteMatrix) {
  // TODO(titzer): test all variants in the matrix.
  // rB
  // tBrB
  // tBrT
  // rW32
  // tIrW32
  // tUrW32
  // rW64
  // tIrW64
  // tUrW64
  // rF64
  // tIrF64
  // tUrF64
  // tArF64
  // rT
  // tArT
}
