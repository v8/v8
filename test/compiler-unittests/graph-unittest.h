// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_UNITTESTS_GRAPH_UNITTEST_H_
#define V8_COMPILER_UNITTESTS_GRAPH_UNITTEST_H_

#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "test/compiler-unittests/common-operator-unittest.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace v8 {
namespace internal {

// Forward declarations.
class HeapObject;
template <class T>
class PrintableUnique;

namespace compiler {

class GraphTest : public CommonOperatorTest {
 public:
  explicit GraphTest(int parameters = 1);
  virtual ~GraphTest();

 protected:
  Graph* graph() { return &graph_; }

 private:
  Graph graph_;
};


using ::testing::Matcher;


Matcher<Node*> IsBranch(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& control_matcher);
Matcher<Node*> IsMerge(const Matcher<Node*>& control0_matcher,
                       const Matcher<Node*>& control1_matcher);
Matcher<Node*> IsIfTrue(const Matcher<Node*>& control_matcher);
Matcher<Node*> IsIfFalse(const Matcher<Node*>& control_matcher);
Matcher<Node*> IsControlEffect(const Matcher<Node*>& control_matcher);
Matcher<Node*> IsValueEffect(const Matcher<Node*>& value_matcher);
Matcher<Node*> IsFinish(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher);
Matcher<Node*> IsExternalConstant(
    const Matcher<ExternalReference>& value_matcher);
Matcher<Node*> IsHeapConstant(
    const Matcher<PrintableUnique<HeapObject> >& value_matcher);
Matcher<Node*> IsInt32Constant(const Matcher<int32_t>& value_matcher);
Matcher<Node*> IsNumberConstant(const Matcher<double>& value_matcher);
Matcher<Node*> IsPhi(const Matcher<Node*>& value0_matcher,
                     const Matcher<Node*>& value1_matcher,
                     const Matcher<Node*>& merge_matcher);
Matcher<Node*> IsProjection(const Matcher<int32_t>& index_matcher,
                            const Matcher<Node*>& base_matcher);
Matcher<Node*> IsCall(const Matcher<CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& value3_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher);

Matcher<Node*> IsLoad(const Matcher<MachineType>& type_matcher,
                      const Matcher<Node*>& base_matcher,
                      const Matcher<Node*>& index_matcher,
                      const Matcher<Node*>& effect_matcher);
Matcher<Node*> IsStore(const Matcher<MachineType>& type_matcher,
                       const Matcher<WriteBarrierKind>& write_barrier_matcher,
                       const Matcher<Node*>& base_matcher,
                       const Matcher<Node*>& index_matcher,
                       const Matcher<Node*>& value_matcher,
                       const Matcher<Node*>& effect_matcher,
                       const Matcher<Node*>& control_matcher);
Matcher<Node*> IsWord32And(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord32Sar(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord32Ror(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord32Equal(const Matcher<Node*>& lhs_matcher,
                             const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord64And(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord64Shl(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord64Sar(const Matcher<Node*>& lhs_matcher,
                           const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsWord64Equal(const Matcher<Node*>& lhs_matcher,
                             const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsInt32AddWithOverflow(const Matcher<Node*>& lhs_matcher,
                                      const Matcher<Node*>& rhs_matcher);
Matcher<Node*> IsChangeFloat64ToInt32(const Matcher<Node*>& input_matcher);
Matcher<Node*> IsChangeInt32ToFloat64(const Matcher<Node*>& input_matcher);
Matcher<Node*> IsChangeInt32ToInt64(const Matcher<Node*>& input_matcher);
Matcher<Node*> IsChangeUint32ToUint64(const Matcher<Node*>& input_matcher);
Matcher<Node*> IsTruncateInt64ToInt32(const Matcher<Node*>& input_matcher);

}  //  namespace compiler
}  //  namespace internal
}  //  namespace v8

#endif  // V8_COMPILER_UNITTESTS_GRAPH_UNITTEST_H_
