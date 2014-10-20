// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/generic-node-inl.h"
#include "src/compiler/js-intrinsic-builder.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/simplified-operator.h"


namespace v8 {
namespace internal {
namespace compiler {

ResultAndEffect JSIntrinsicBuilder::BuildGraphFor(Runtime::FunctionId id,
                                                  const NodeVector& arguments) {
  switch (id) {
    case Runtime::kInlineIsSmi:
      return BuildGraphFor_IsSmi(arguments);
    case Runtime::kInlineIsNonNegativeSmi:
      return BuildGraphFor_IsNonNegativeSmi(arguments);
    case Runtime::kInlineIsArray:
      return BuildMapCheck(arguments[0], arguments[2], JS_ARRAY_TYPE);
    case Runtime::kInlineIsRegExp:
      return BuildMapCheck(arguments[0], arguments[2], JS_REGEXP_TYPE);
    case Runtime::kInlineIsFunction:
      return BuildMapCheck(arguments[0], arguments[2], JS_FUNCTION_TYPE);
    case Runtime::kInlineValueOf:
      return BuildGraphFor_ValueOf(arguments);
    default:
      break;
  }
  return ResultAndEffect();
}

ResultAndEffect JSIntrinsicBuilder::BuildGraphFor_IsSmi(
    const NodeVector& arguments) {
  Node* object = arguments[0];
  SimplifiedOperatorBuilder simplified(jsgraph_->zone());
  Node* condition = graph()->NewNode(simplified.ObjectIsSmi(), object);

  return ResultAndEffect(condition, arguments[2]);
}


ResultAndEffect JSIntrinsicBuilder::BuildGraphFor_IsNonNegativeSmi(
    const NodeVector& arguments) {
  Node* object = arguments[0];
  SimplifiedOperatorBuilder simplified(jsgraph_->zone());
  Node* condition =
      graph()->NewNode(simplified.ObjectIsNonNegativeSmi(), object);

  return ResultAndEffect(condition, arguments[2]);
}


/*
 * if (_isSmi(object)) {
 *   return false
 * } else {
 *   return %_GetMapInstanceType(object) == map_type
 * }
 */
ResultAndEffect JSIntrinsicBuilder::BuildMapCheck(Node* object, Node* effect,
                                                  InstanceType map_type) {
  SimplifiedOperatorBuilder simplified(jsgraph_->zone());

  Node* is_smi = graph()->NewNode(simplified.ObjectIsSmi(), object);
  Node* branch = graph()->NewNode(common()->Branch(), is_smi, graph()->start());
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);

  Node* map = graph()->NewNode(simplified.LoadField(AccessBuilder::ForMap()),
                               object, effect, if_false);

  Node* instance_type = graph()->NewNode(
      simplified.LoadField(AccessBuilder::ForMapInstanceType()), map, map,
      if_false);

  Node* has_map_type =
      graph()->NewNode(jsgraph_->machine()->Word32Equal(), instance_type,
                       jsgraph_->Int32Constant(map_type));

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);

  Node* phi =
      graph()->NewNode(common()->Phi((MachineType)(kTypeBool | kRepTagged), 2),
                       jsgraph_->FalseConstant(), has_map_type, merge);

  Node* ephi =
      graph()->NewNode(common()->EffectPhi(2), effect, instance_type, merge);

  return ResultAndEffect(phi, ephi);
}


/*
 * if (%_isSmi(object)) {
 *   return object;
 * } else if (%_GetMapInstanceType(object) == JS_VALUE_TYPE) {
 *   return %_LoadValueField(object);
 * } else {
 *   return object;
 * }
 */
ResultAndEffect JSIntrinsicBuilder::BuildGraphFor_ValueOf(
    const NodeVector& arguments) {
  Node* object = arguments[0];
  Node* effect = arguments[2];
  SimplifiedOperatorBuilder simplified(jsgraph_->zone());

  Node* is_smi = graph()->NewNode(simplified.ObjectIsSmi(), object);
  Node* branch = graph()->NewNode(common()->Branch(), is_smi, graph()->start());
  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);

  Node* map = graph()->NewNode(simplified.LoadField(AccessBuilder::ForMap()),
                               object, effect, if_false);

  Node* instance_type = graph()->NewNode(
      simplified.LoadField(AccessBuilder::ForMapInstanceType()), map, map,
      if_false);

  Node* is_value =
      graph()->NewNode(jsgraph_->machine()->Word32Equal(), instance_type,
                       jsgraph_->Constant(JS_VALUE_TYPE));

  Node* branch_is_value =
      graph()->NewNode(common()->Branch(), is_value, if_false);
  Node* is_value_true = graph()->NewNode(common()->IfTrue(), branch_is_value);
  Node* is_value_false = graph()->NewNode(common()->IfFalse(), branch_is_value);

  Node* value =
      graph()->NewNode(simplified.LoadField(AccessBuilder::ForValue()), object,
                       instance_type, is_value_true);

  Node* merge_is_value =
      graph()->NewNode(common()->Merge(2), is_value_true, is_value_false);

  Node* phi_is_value = graph()->NewNode(common()->Phi((MachineType)kTypeAny, 2),
                                        value, object, merge_is_value);


  Node* merge = graph()->NewNode(common()->Merge(2), if_true, merge_is_value);

  Node* phi = graph()->NewNode(common()->Phi((MachineType)kTypeAny, 2), object,
                               phi_is_value, merge);

  Node* ephi =
      graph()->NewNode(common()->EffectPhi(2), effect, instance_type, merge);

  return ResultAndEffect(phi, ephi);
}
}
}
}  // namespace v8::internal::compiler
