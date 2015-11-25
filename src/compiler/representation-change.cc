// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/representation-change.h"

#include <sstream>

#include "src/base/bits.h"
#include "src/code-factory.h"
#include "src/compiler/machine-operator.h"

namespace v8 {
namespace internal {
namespace compiler {

const char* Truncation::description() const {
  switch (kind()) {
    case TruncationKind::kNone:
      return "no-value-use";
    case TruncationKind::kBool:
      return "truncate-to-bool";
    case TruncationKind::kWord32:
      return "truncate-to-word32";
    case TruncationKind::kWord64:
      return "truncate-to-word64";
    case TruncationKind::kFloat32:
      return "truncate-to-float32";
    case TruncationKind::kFloat64:
      return "truncate-to-float64";
    case TruncationKind::kAny:
      return "no-truncation";
  }
  UNREACHABLE();
  return nullptr;
}


// Partial order for truncations:
//
//  kWord64       kAny
//     ^            ^
//     \            |
//      \         kFloat64  <--+
//       \        ^    ^       |
//        \       /    |       |
//         kWord32  kFloat32  kBool
//               ^     ^      ^
//               \     |      /
//                \    |     /
//                 \   |    /
//                  \  |   /
//                   \ |  /
//                   kNone

// static
Truncation::TruncationKind Truncation::Generalize(TruncationKind rep1,
                                                  TruncationKind rep2) {
  if (LessGeneral(rep1, rep2)) return rep2;
  if (LessGeneral(rep2, rep1)) return rep1;
  // Handle the generalization of float64-representable values.
  if (LessGeneral(rep1, TruncationKind::kFloat64) &&
      LessGeneral(rep2, TruncationKind::kFloat64)) {
    return TruncationKind::kFloat64;
  }
  // All other combinations are illegal.
  FATAL("Tried to combine incompatible representations");
  return TruncationKind::kNone;
}


// static
bool Truncation::LessGeneral(TruncationKind rep1, TruncationKind rep2) {
  switch (rep1) {
    case TruncationKind::kNone:
      return true;
    case TruncationKind::kBool:
      return rep2 == TruncationKind::kBool || rep2 == TruncationKind::kAny;
    case TruncationKind::kWord32:
      return rep2 == TruncationKind::kWord32 ||
             rep2 == TruncationKind::kWord64 ||
             rep2 == TruncationKind::kFloat64 || rep2 == TruncationKind::kAny;
    case TruncationKind::kWord64:
      return rep2 == TruncationKind::kWord64;
    case TruncationKind::kFloat32:
      return rep2 == TruncationKind::kFloat32 ||
             rep2 == TruncationKind::kFloat64 || rep2 == TruncationKind::kAny;
    case TruncationKind::kFloat64:
      return rep2 == TruncationKind::kFloat64 || rep2 == TruncationKind::kAny;
    case TruncationKind::kAny:
      return rep2 == TruncationKind::kAny;
  }
  UNREACHABLE();
  return false;
}


namespace {

// TODO(titzer): should Word64 also be implicitly convertable to others?
bool IsWord(MachineTypeUnion type) {
  return (type & (kRepWord8 | kRepWord16 | kRepWord32)) != 0;
}

}  // namespace


// Changes representation from {output_type} to {use_rep}. The {truncation}
// parameter is only used for sanity checking - if the changer cannot figure
// out signedness for the word32->float64 conversion, then we check that the
// uses truncate to word32 (so they do not care about signedness).
Node* RepresentationChanger::GetRepresentationFor(Node* node,
                                                  MachineTypeUnion output_type,
                                                  MachineTypeUnion use_rep,
                                                  Truncation truncation) {
  DCHECK((use_rep & kRepMask) == use_rep);
  if (!base::bits::IsPowerOfTwo32(output_type & kRepMask)) {
    // There should be only one output representation.
    return TypeError(node, output_type, use_rep);
  }
  if (use_rep == (output_type & kRepMask)) {
    // Representations are the same. That's a no-op.
    return node;
  }
  if (IsWord(use_rep) && IsWord(output_type)) {
    // Both are words less than or equal to 32-bits.
    // Since loads of integers from memory implicitly sign or zero extend the
    // value to the full machine word size and stores implicitly truncate,
    // no representation change is necessary.
    return node;
  }
  if (use_rep & kRepTagged) {
    return GetTaggedRepresentationFor(node, output_type);
  } else if (use_rep & kRepFloat32) {
    return GetFloat32RepresentationFor(node, output_type, truncation);
  } else if (use_rep & kRepFloat64) {
    return GetFloat64RepresentationFor(node, output_type, truncation);
  } else if (use_rep & kRepBit) {
    return GetBitRepresentationFor(node, output_type);
  } else if (IsWord(use_rep)) {
    return GetWord32RepresentationFor(node, output_type);
  } else if (use_rep & kRepWord64) {
    return GetWord64RepresentationFor(node, output_type);
  } else {
    return node;
  }
}


Node* RepresentationChanger::GetTaggedRepresentationFor(
    Node* node, MachineTypeUnion output_type) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kNumberConstant:
    case IrOpcode::kHeapConstant:
      return node;  // No change necessary.
    case IrOpcode::kInt32Constant:
      if (output_type & kTypeUint32) {
        uint32_t value = static_cast<uint32_t>(OpParameter<int32_t>(node));
        return jsgraph()->Constant(static_cast<double>(value));
      } else if (output_type & kTypeInt32) {
        int32_t value = OpParameter<int32_t>(node);
        return jsgraph()->Constant(value);
      } else if (output_type & kRepBit) {
        return OpParameter<int32_t>(node) == 0 ? jsgraph()->FalseConstant()
                                               : jsgraph()->TrueConstant();
      } else {
        return TypeError(node, output_type, kRepTagged);
      }
    case IrOpcode::kFloat64Constant:
      return jsgraph()->Constant(OpParameter<double>(node));
    case IrOpcode::kFloat32Constant:
      return jsgraph()->Constant(OpParameter<float>(node));
    default:
      break;
  }
  // Select the correct X -> Tagged operator.
  const Operator* op;
  if (output_type & kRepBit) {
    op = simplified()->ChangeBitToBool();
  } else if (IsWord(output_type)) {
    if (output_type & kTypeUint32) {
      op = simplified()->ChangeUint32ToTagged();
    } else if (output_type & kTypeInt32) {
      op = simplified()->ChangeInt32ToTagged();
    } else {
      return TypeError(node, output_type, kRepTagged);
    }
  } else if (output_type & kRepFloat32) {  // float32 -> float64 -> tagged
    node = InsertChangeFloat32ToFloat64(node);
    op = simplified()->ChangeFloat64ToTagged();
  } else if (output_type & kRepFloat64) {
    op = simplified()->ChangeFloat64ToTagged();
  } else {
    return TypeError(node, output_type, kRepTagged);
  }
  return jsgraph()->graph()->NewNode(op, node);
}


Node* RepresentationChanger::GetFloat32RepresentationFor(
    Node* node, MachineTypeUnion output_type, Truncation truncation) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kFloat64Constant:
    case IrOpcode::kNumberConstant:
      return jsgraph()->Float32Constant(
          DoubleToFloat32(OpParameter<double>(node)));
    case IrOpcode::kInt32Constant:
      if (output_type & kTypeUint32) {
        uint32_t value = static_cast<uint32_t>(OpParameter<int32_t>(node));
        return jsgraph()->Float32Constant(static_cast<float>(value));
      } else {
        int32_t value = OpParameter<int32_t>(node);
        return jsgraph()->Float32Constant(static_cast<float>(value));
      }
    case IrOpcode::kFloat32Constant:
      return node;  // No change necessary.
    default:
      break;
  }
  // Select the correct X -> Float32 operator.
  const Operator* op;
  if (output_type & kRepBit) {
    return TypeError(node, output_type, kRepFloat32);
  } else if (IsWord(output_type)) {
    if (output_type & kTypeUint32) {
      op = machine()->ChangeUint32ToFloat64();
    } else {
      // Either the output is int32 or the uses only care about the
      // low 32 bits (so we can pick int32 safely).
      DCHECK(output_type & kTypeInt32 || truncation.TruncatesToWord32());
      op = machine()->ChangeInt32ToFloat64();
    }
    // int32 -> float64 -> float32
    node = jsgraph()->graph()->NewNode(op, node);
    op = machine()->TruncateFloat64ToFloat32();
  } else if (output_type & kRepTagged) {
    op = simplified()->ChangeTaggedToFloat64();  // tagged -> float64 -> float32
    node = jsgraph()->graph()->NewNode(op, node);
    op = machine()->TruncateFloat64ToFloat32();
  } else if (output_type & kRepFloat64) {
    op = machine()->TruncateFloat64ToFloat32();
  } else {
    return TypeError(node, output_type, kRepFloat32);
  }
  return jsgraph()->graph()->NewNode(op, node);
}


Node* RepresentationChanger::GetFloat64RepresentationFor(
    Node* node, MachineTypeUnion output_type, Truncation truncation) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kNumberConstant:
      return jsgraph()->Float64Constant(OpParameter<double>(node));
    case IrOpcode::kInt32Constant:
      if (output_type & kTypeUint32) {
        uint32_t value = static_cast<uint32_t>(OpParameter<int32_t>(node));
        return jsgraph()->Float64Constant(static_cast<double>(value));
      } else {
        int32_t value = OpParameter<int32_t>(node);
        return jsgraph()->Float64Constant(value);
      }
    case IrOpcode::kFloat64Constant:
      return node;  // No change necessary.
    case IrOpcode::kFloat32Constant:
      return jsgraph()->Float64Constant(OpParameter<float>(node));
    default:
      break;
  }
  // Select the correct X -> Float64 operator.
  const Operator* op;
  if (output_type & kRepBit) {
    return TypeError(node, output_type, kRepFloat64);
  } else if (IsWord(output_type)) {
    if (output_type & kTypeUint32) {
      op = machine()->ChangeUint32ToFloat64();
    } else {
      // Either the output is int32 or the uses only care about the
      // low 32 bits (so we can pick int32 safely).
      DCHECK(output_type & kTypeInt32 || truncation.TruncatesToWord32());
      op = machine()->ChangeInt32ToFloat64();
    }
  } else if (output_type & kRepTagged) {
    op = simplified()->ChangeTaggedToFloat64();
  } else if (output_type & kRepFloat32) {
    op = machine()->ChangeFloat32ToFloat64();
  } else {
    return TypeError(node, output_type, kRepFloat64);
  }
  return jsgraph()->graph()->NewNode(op, node);
}


Node* RepresentationChanger::MakeTruncatedInt32Constant(double value) {
  return jsgraph()->Int32Constant(DoubleToInt32(value));
}


Node* RepresentationChanger::GetWord32RepresentationFor(
    Node* node, MachineTypeUnion output_type) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kInt32Constant:
      return node;  // No change necessary.
    case IrOpcode::kFloat32Constant:
      return MakeTruncatedInt32Constant(OpParameter<float>(node));
    case IrOpcode::kNumberConstant:
    case IrOpcode::kFloat64Constant:
      return MakeTruncatedInt32Constant(OpParameter<double>(node));
    default:
      break;
  }
  // Select the correct X -> Word32 operator.
  const Operator* op;
  Type* type = NodeProperties::GetType(node);

  if (output_type & kRepBit) {
    return node;  // Sloppy comparison -> word32
  } else if (output_type & kRepFloat64) {
    if (output_type & kTypeUint32 || type->Is(Type::Unsigned32())) {
      op = machine()->ChangeFloat64ToUint32();
    } else if (output_type & kTypeInt32 || type->Is(Type::Signed32())) {
      op = machine()->ChangeFloat64ToInt32();
    } else {
      op = machine()->TruncateFloat64ToInt32(TruncationMode::kJavaScript);
    }
  } else if (output_type & kRepFloat32) {
    node = InsertChangeFloat32ToFloat64(node);  // float32 -> float64 -> int32
    if (output_type & kTypeUint32 || type->Is(Type::Unsigned32())) {
      op = machine()->ChangeFloat64ToUint32();
    } else if (output_type & kTypeInt32 || type->Is(Type::Signed32())) {
      op = machine()->ChangeFloat64ToInt32();
    } else {
      op = machine()->TruncateFloat64ToInt32(TruncationMode::kJavaScript);
    }
  } else if (output_type & kRepTagged) {
    if (output_type & kTypeUint32 || type->Is(Type::Unsigned32())) {
      op = simplified()->ChangeTaggedToUint32();
    } else if (output_type & kTypeInt32 || type->Is(Type::Signed32())) {
      op = simplified()->ChangeTaggedToInt32();
    } else {
      node = InsertChangeTaggedToFloat64(node);
      op = machine()->TruncateFloat64ToInt32(TruncationMode::kJavaScript);
    }
  } else {
    return TypeError(node, output_type, kRepWord32);
  }
  return jsgraph()->graph()->NewNode(op, node);
}


Node* RepresentationChanger::GetBitRepresentationFor(
    Node* node, MachineTypeUnion output_type) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      Handle<HeapObject> value = OpParameter<Handle<HeapObject>>(node);
      DCHECK(value.is_identical_to(factory()->true_value()) ||
             value.is_identical_to(factory()->false_value()));
      return jsgraph()->Int32Constant(
          value.is_identical_to(factory()->true_value()) ? 1 : 0);
    }
    default:
      break;
  }
  // Select the correct X -> Bit operator.
  const Operator* op;
  if (output_type & kRepTagged) {
    op = simplified()->ChangeBoolToBit();
  } else {
    return TypeError(node, output_type, kRepBit);
  }
  return jsgraph()->graph()->NewNode(op, node);
}


Node* RepresentationChanger::GetWord64RepresentationFor(
    Node* node, MachineTypeUnion output_type) {
  if (output_type & kRepBit) {
    return node;  // Sloppy comparison -> word64
  }
  // Can't really convert Word64 to anything else. Purported to be internal.
  return TypeError(node, output_type, kRepWord64);
}


const Operator* RepresentationChanger::Int32OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kNumberAdd:
      return machine()->Int32Add();
    case IrOpcode::kNumberSubtract:
      return machine()->Int32Sub();
    case IrOpcode::kNumberMultiply:
      return machine()->Int32Mul();
    case IrOpcode::kNumberDivide:
      return machine()->Int32Div();
    case IrOpcode::kNumberModulus:
      return machine()->Int32Mod();
    case IrOpcode::kNumberBitwiseOr:
      return machine()->Word32Or();
    case IrOpcode::kNumberBitwiseXor:
      return machine()->Word32Xor();
    case IrOpcode::kNumberBitwiseAnd:
      return machine()->Word32And();
    case IrOpcode::kNumberEqual:
      return machine()->Word32Equal();
    case IrOpcode::kNumberLessThan:
      return machine()->Int32LessThan();
    case IrOpcode::kNumberLessThanOrEqual:
      return machine()->Int32LessThanOrEqual();
    default:
      UNREACHABLE();
      return NULL;
  }
}


const Operator* RepresentationChanger::Uint32OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kNumberAdd:
      return machine()->Int32Add();
    case IrOpcode::kNumberSubtract:
      return machine()->Int32Sub();
    case IrOpcode::kNumberMultiply:
      return machine()->Int32Mul();
    case IrOpcode::kNumberDivide:
      return machine()->Uint32Div();
    case IrOpcode::kNumberModulus:
      return machine()->Uint32Mod();
    case IrOpcode::kNumberEqual:
      return machine()->Word32Equal();
    case IrOpcode::kNumberLessThan:
      return machine()->Uint32LessThan();
    case IrOpcode::kNumberLessThanOrEqual:
      return machine()->Uint32LessThanOrEqual();
    default:
      UNREACHABLE();
      return NULL;
  }
}


const Operator* RepresentationChanger::Float64OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kNumberAdd:
      return machine()->Float64Add();
    case IrOpcode::kNumberSubtract:
      return machine()->Float64Sub();
    case IrOpcode::kNumberMultiply:
      return machine()->Float64Mul();
    case IrOpcode::kNumberDivide:
      return machine()->Float64Div();
    case IrOpcode::kNumberModulus:
      return machine()->Float64Mod();
    case IrOpcode::kNumberEqual:
      return machine()->Float64Equal();
    case IrOpcode::kNumberLessThan:
      return machine()->Float64LessThan();
    case IrOpcode::kNumberLessThanOrEqual:
      return machine()->Float64LessThanOrEqual();
    default:
      UNREACHABLE();
      return NULL;
  }
}


MachineType RepresentationChanger::TypeFromUpperBound(Type* type) {
  CHECK(!type->Is(Type::None()));
  if (type->Is(Type::Signed32())) return kTypeInt32;
  if (type->Is(Type::Unsigned32())) return kTypeUint32;
  if (type->Is(Type::Number())) return kTypeNumber;
  if (type->Is(Type::Boolean())) return kTypeBool;
  return kTypeAny;
}


Node* RepresentationChanger::TypeError(Node* node, MachineTypeUnion output_type,
                                       MachineTypeUnion use) {
  type_error_ = true;
  if (!testing_type_errors_) {
    std::ostringstream out_str;
    out_str << static_cast<MachineType>(output_type);

    std::ostringstream use_str;
    use_str << static_cast<MachineType>(use);

    V8_Fatal(__FILE__, __LINE__,
             "RepresentationChangerError: node #%d:%s of "
             "%s cannot be changed to %s",
             node->id(), node->op()->mnemonic(), out_str.str().c_str(),
             use_str.str().c_str());
  }
  return node;
}


Node* RepresentationChanger::InsertChangeFloat32ToFloat64(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->ChangeFloat32ToFloat64(), node);
}


Node* RepresentationChanger::InsertChangeTaggedToFloat64(Node* node) {
  return jsgraph()->graph()->NewNode(simplified()->ChangeTaggedToFloat64(),
                                     node);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
