// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/load-elimination.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class LoadEliminationTest : public TypedGraphTest {
 public:
  LoadEliminationTest() : TypedGraphTest(3), simplified_(zone()) {}
  ~LoadEliminationTest() override {}

 protected:
  void Run() {
    LoadElimination load_elimination(graph(), zone());
    load_elimination.Run();
  }

  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  SimplifiedOperatorBuilder simplified_;
};

TEST_F(LoadEliminationTest, LoadFieldAndLoadField) {
  Node* object = Parameter(Type::Any(), 0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess access = {kTaggedBase,
                        kPointerSize,
                        MaybeHandle<Name>(),
                        Type::Any(),
                        MachineType::AnyTagged(),
                        kNoWriteBarrier};
  Node* load1 = effect = graph()->NewNode(simplified()->LoadField(access),
                                          object, effect, control);
  Node* load2 = effect = graph()->NewNode(simplified()->LoadField(access),
                                          object, effect, control);
  control = graph()->NewNode(common()->Return(), load2, effect, control);
  graph()->end()->ReplaceInput(0, control);

  Run();

  EXPECT_THAT(graph()->end(), IsEnd(IsReturn(load1, load1, graph()->start())));
}

TEST_F(LoadEliminationTest, StoreFieldAndLoadField) {
  Node* object = Parameter(Type::Any(), 0);
  Node* value = Parameter(Type::Any(), 1);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  FieldAccess access = {kTaggedBase,
                        kPointerSize,
                        MaybeHandle<Name>(),
                        Type::Any(),
                        MachineType::AnyTagged(),
                        kNoWriteBarrier};
  Node* store = effect = graph()->NewNode(simplified()->StoreField(access),
                                          object, value, effect, control);
  Node* load = effect = graph()->NewNode(simplified()->LoadField(access),
                                         object, effect, control);
  control = graph()->NewNode(common()->Return(), load, effect, control);
  graph()->end()->ReplaceInput(0, control);

  Run();

  EXPECT_THAT(graph()->end(), IsEnd(IsReturn(value, store, graph()->start())));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
