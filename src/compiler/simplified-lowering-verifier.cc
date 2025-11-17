// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-lowering-verifier.h"

#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/operation-typer.h"
#include "src/compiler/type-cache.h"

namespace v8 {
namespace internal {
namespace compiler {

bool IsNonTruncatingMachineTypeFor(const MachineType& mt, const Type& type,
                                   Zone* graph_zone) {
  if (type.IsNone()) return true;
  // TODO(nicohartmann@): Add more cases here.
  if (type.Is(Type::BigInt())) {
    if (mt.representation() == MachineRepresentation::kWord64) {
      return type.Is(Type::SignedBigInt64()) ||
             type.Is(Type::UnsignedBigInt64());
    }
    return mt.representation() == MachineRepresentation::kTaggedPointer ||
           mt.representation() == MachineRepresentation::kTagged;
  }
  switch (mt.representation()) {
    case MachineRepresentation::kBit:
      CHECK(mt.semantic() == MachineSemantic::kBool ||
            mt.semantic() == MachineSemantic::kAny);
      return type.Is(Type::Boolean()) || type.Is(Type::Range(0, 1, graph_zone));
    default:
      return true;
  }
}

void SimplifiedLoweringVerifier::CheckType(Node* node, const Type& type) {
  CHECK(NodeProperties::IsTyped(node));
  Type node_type = NodeProperties::GetType(node);
  if (!type.Is(node_type)) {
    std::ostringstream type_str;
    type.PrintTo(type_str);
    std::ostringstream node_type_str;
    node_type.PrintTo(node_type_str);

    FATAL(
        "SimplifiedLoweringVerifierError: verified type %s of node #%d:%s "
        "does not match with type %s assigned during lowering",
        type_str.str().c_str(), node->id(), node->op()->mnemonic(),
        node_type_str.str().c_str());
  }
}

void SimplifiedLoweringVerifier::CheckAndSet(Node* node, const Type& type,
                                             const Truncation& trunc) {
  DCHECK(!type.IsInvalid());

  if (NodeProperties::IsTyped(node)) {
    CheckType(node, type);
  } else {
    // We store the type inferred by the verification pass. We do not update
    // the node's type directly, because following phases might encounter
    // unsound types as long as the verification is not complete.
    SetType(node, type);
  }
  SetTruncation(node, GeneralizeTruncation(trunc, type));
}

void SimplifiedLoweringVerifier::ReportInvalidTypeCombination(
    Node* node, const std::vector<Type>& types) {
  std::ostringstream types_str;
  for (size_t i = 0; i < types.size(); ++i) {
    if (i != 0) types_str << ", ";
    types[i].PrintTo(types_str);
  }
  std::ostringstream graph_str;
  node->Print(graph_str, 2);
  FATAL(
      "SimplifiedLoweringVerifierError: invalid combination of input types %s "
      " for node #%d:%s.\n\nGraph is: %s",
      types_str.str().c_str(), node->id(), node->op()->mnemonic(),
      graph_str.str().c_str());
}

bool IsModuloTruncation(const Truncation& truncation) {
  return truncation.IsUsedAsWord32() ||
         (Is64() && truncation.IsUsedAsWord64()) ||
         Truncation::Any().IsLessGeneralThan(truncation);
}

Truncation SimplifiedLoweringVerifier::GeneralizeTruncation(
    const Truncation& truncation, const Type& type) const {
  IdentifyZeros identify_zeros = truncation.identify_zeros();
  if (!type.Maybe(Type::MinusZero())) {
    identify_zeros = IdentifyZeros::kDistinguishZeros;
  }

  switch (truncation.kind()) {
    case Truncation::TruncationKind::kAny: {
      return Truncation::Any(identify_zeros);
    }
    case Truncation::TruncationKind::kBool: {
      if (type.Is(Type::Boolean())) return Truncation::Any();
      return Truncation(Truncation::TruncationKind::kBool, identify_zeros);
    }
    case Truncation::TruncationKind::kWord32: {
      if (type.Is(Type::Signed32OrMinusZero()) ||
          type.Is(Type::Unsigned32OrMinusZero())) {
        return Truncation::Any(identify_zeros);
      }
      return Truncation(Truncation::TruncationKind::kWord32, identify_zeros);
    }
    case Truncation::TruncationKind::kWord64: {
      if (type.Is(Type::BigInt())) {
        DCHECK_EQ(identify_zeros, IdentifyZeros::kDistinguishZeros);
        if (type.Is(Type::SignedBigInt64()) ||
            type.Is(Type::UnsignedBigInt64())) {
          return Truncation::Any(IdentifyZeros::kDistinguishZeros);
        }
      } else if (type.Is(TypeCache::Get()->kSafeIntegerOrMinusZero)) {
        return Truncation::Any(identify_zeros);
      }
      return Truncation(Truncation::TruncationKind::kWord64, identify_zeros);
    }

    default:
      // TODO(nicohartmann): Support remaining truncations.
      UNREACHABLE();
  }
}

Truncation SimplifiedLoweringVerifier::JoinTruncation(const Truncation& t1,
                                                      const Truncation& t2) {
  Truncation::TruncationKind kind;
  if (Truncation::LessGeneral(t1.kind(), t2.kind())) {
    kind = t1.kind();
  } else {
    CHECK(Truncation::LessGeneral(t2.kind(), t1.kind()));
    kind = t2.kind();
  }
  IdentifyZeros identify_zeros = Truncation::LessGeneralIdentifyZeros(
                                     t1.identify_zeros(), t2.identify_zeros())
                                     ? t1.identify_zeros()
                                     : t2.identify_zeros();
  return Truncation(kind, identify_zeros);
}

void SimplifiedLoweringVerifier::VisitNode(Node* node,
                                           OperationTyper& op_typer) {
  switch (node->opcode()) {
    case IrOpcode::kStart:
    case IrOpcode::kIfTrue:
    case IrOpcode::kIfFalse:
    case IrOpcode::kMerge:
    case IrOpcode::kEnd:
    case IrOpcode::kEffectPhi:
    case IrOpcode::kCheckpoint:
    case IrOpcode::kFrameState:
    case IrOpcode::kJSStackCheck:
      break;
    case IrOpcode::kInt32Constant: {
      // NOTE: Constants require special handling as they are shared between
      // machine graphs and non-machine graphs lowered during SL. The former
      // might have assigned Type::Machine() to the constant, but to be able
      // to provide a different type for uses of constants that don't come
      // from machine graphs, the machine-uses of Int32Constants have been
      // put behind additional SLVerifierHints to provide the required
      // Type::Machine() to them, such that we can treat constants here as
      // having JS types to satisfy their non-machine uses.
      int32_t value = OpParameter<int32_t>(node->op());
      Type type = Type::Constant(value, graph_zone());
      SetType(node, type);
      SetTruncation(node, GeneralizeTruncation(Truncation::Word32(), type));
      break;
    }
    case IrOpcode::kInt64Constant:
    case IrOpcode::kFloat64Constant: {
      // Constants might be untyped, because they are cached in the graph and
      // used in different contexts such that no single type can be assigned.
      // Their type is provided by an introduced TypeGuard where necessary.
      break;
    }
    case IrOpcode::kHeapConstant:
      break;
    case IrOpcode::kCheckedFloat64ToInt32: {
      Type input_type = InputType(node, 0);
      DCHECK(input_type.Is(Type::Number()));

      const auto& p = CheckMinusZeroParametersOf(node->op());
      if (p.mode() == CheckForMinusZeroMode::kCheckForMinusZero) {
        // Remove -0 from input_type.
        input_type =
            Type::Intersect(input_type, Type::Signed32(), graph_zone());
      } else {
        input_type = Type::Intersect(input_type, Type::Signed32OrMinusZero(),
                                     graph_zone());
      }
      CheckAndSet(node, input_type, Truncation::Word32());
      break;
    }
    case IrOpcode::kCheckedTaggedToTaggedSigned: {
      Type input_type = InputType(node, 0);
      Type output_type =
          Type::Intersect(input_type, Type::SignedSmall(), graph_zone());
      Truncation output_trunc = InputTruncation(node, 0);
      CheckAndSet(node, output_type, output_trunc);
      break;
    }
    case IrOpcode::kCheckedTaggedToTaggedPointer:
      CheckAndSet(node, InputType(node, 0), InputTruncation(node, 0));
      break;
    case IrOpcode::kTruncateTaggedToBit: {
      Type input_type = InputType(node, 0);
      Truncation input_trunc = InputTruncation(node, 0);
      // Cannot have other truncation here, because identified values lead to
      // different results when converted to bit.
      CHECK(input_trunc == Truncation::Bool() ||
            input_trunc == Truncation::Any());
      Type output_type = op_typer.ToBoolean(input_type);
      CheckAndSet(node, output_type, Truncation::Bool());
      break;
    }
    case IrOpcode::kInt32Add: {
      Type left_type = InputType(node, 0);
      Type right_type = InputType(node, 1);
      Type output_type;
      if (left_type.IsNone() && right_type.IsNone()) {
        output_type = Type::None();
      } else if (left_type.Is(Type::Machine()) &&
                 right_type.Is(Type::Machine())) {
        output_type = Type::Machine();
      } else if (left_type.Is(Type::NumberOrOddball()) &&
                 right_type.Is(Type::NumberOrOddball())) {
        left_type = op_typer.ToNumber(left_type);
        right_type = op_typer.ToNumber(right_type);
        output_type = op_typer.NumberAdd(left_type, right_type);
      } else {
        ReportInvalidTypeCombination(node, {left_type, right_type});
      }
      Truncation output_trunc =
          JoinTruncation(InputTruncation(node, 0), InputTruncation(node, 1),
                         Truncation::Word32());
      CHECK(IsModuloTruncation(output_trunc));
      CheckAndSet(node, output_type, output_trunc);
      break;
    }
    case IrOpcode::kInt32Sub: {
      Type left_type = InputType(node, 0);
      Type right_type = InputType(node, 1);
      Type output_type;
      if (left_type.IsNone() && right_type.IsNone()) {
        output_type = Type::None();
      } else if (left_type.Is(Type::Machine()) &&
                 right_type.Is(Type::Machine())) {
        output_type = Type::Machine();
      } else if (left_type.Is(Type::NumberOrOddball()) &&
                 right_type.Is(Type::NumberOrOddball())) {
        left_type = op_typer.ToNumber(left_type);
        right_type = op_typer.ToNumber(right_type);
        output_type = op_typer.NumberSubtract(left_type, right_type);
      } else {
        ReportInvalidTypeCombination(node, {left_type, right_type});
      }
      Truncation output_trunc =
          JoinTruncation(InputTruncation(node, 0), InputTruncation(node, 1),
                         Truncation::Word32());
      CHECK(IsModuloTruncation(output_trunc));
      CheckAndSet(node, output_type, output_trunc);
      break;
    }
    case IrOpcode::kChangeInt31ToTaggedSigned:
    case IrOpcode::kChangeInt32ToTagged:
    case IrOpcode::kChangeFloat32ToFloat64:
    case IrOpcode::kChangeInt32ToInt64:
    case IrOpcode::kChangeUint32ToUint64:
    case IrOpcode::kChangeUint64ToTagged: {
      // These change operators do not truncate any values and can simply
      // forward input type and truncation.
      CheckAndSet(node, InputType(node, 0), InputTruncation(node, 0));
      break;
    }
    case IrOpcode::kChangeFloat64ToInt64: {
      Truncation output_trunc =
          JoinTruncation(InputTruncation(node, 0), Truncation::Word64());
      CheckAndSet(node, InputType(node, 0), output_trunc);
      break;
    }
    case IrOpcode::kInt64Add: {
      Type left_type = InputType(node, 0);
      Type right_type = InputType(node, 1);
      Type output_type;
      if (left_type.IsNone() && right_type.IsNone()) {
        // None x None -> None
        output_type = Type::None();
      } else if (left_type.Is(Type::Machine()) &&
                 right_type.Is(Type::Machine())) {
        // Machine x Machine -> Machine
        output_type = Type::Machine();
      } else if (left_type.Is(Type::BigInt()) &&
                 right_type.Is(Type::BigInt())) {
        // BigInt x BigInt -> BigInt
        output_type = op_typer.BigIntAdd(left_type, right_type);
      } else if (left_type.Is(Type::NumberOrOddball()) &&
                 right_type.Is(Type::NumberOrOddball())) {
        // Number x Number -> Number
        left_type = op_typer.ToNumber(left_type);
        right_type = op_typer.ToNumber(right_type);
        output_type = op_typer.NumberAdd(left_type, right_type);
      } else {
        // Invalid type combination.
        ReportInvalidTypeCombination(node, {left_type, right_type});
      }
      Truncation output_trunc =
          JoinTruncation(InputTruncation(node, 0), InputTruncation(node, 1),
                         Truncation::Word64());
      CHECK(IsModuloTruncation(output_trunc));
      CheckAndSet(node, output_type, output_trunc);
      break;
    }
    case IrOpcode::kInt64Sub: {
      Type left_type = InputType(node, 0);
      Type right_type = InputType(node, 1);
      Type output_type;
      if (left_type.IsNone() && right_type.IsNone()) {
        // None x None -> None
        output_type = Type::None();
      } else if (left_type.Is(Type::Machine()) &&
                 right_type.Is(Type::Machine())) {
        // Machine x Machine -> Machine
        output_type = Type::Machine();
      } else if (left_type.Is(Type::BigInt()) &&
                 right_type.Is(Type::BigInt())) {
        // BigInt x BigInt -> BigInt
        output_type = op_typer.BigIntSubtract(left_type, right_type);
      } else if (left_type.Is(Type::NumberOrOddball()) &&
                 right_type.Is(Type::NumberOrOddball())) {
        // Number x Number -> Number
        left_type = op_typer.ToNumber(left_type);
        right_type = op_typer.ToNumber(right_type);
        output_type = op_typer.NumberSubtract(left_type, right_type);
      } else {
        // Invalid type combination.
        ReportInvalidTypeCombination(node, {left_type, right_type});
      }
      Truncation output_trunc =
          JoinTruncation(InputTruncation(node, 0), InputTruncation(node, 1),
                         Truncation::Word64());
      CHECK(IsModuloTruncation(output_trunc));
      CheckAndSet(node, output_type, output_trunc);
      break;
    }
    case IrOpcode::kDeadValue: {
      CheckAndSet(node, Type::None(), Truncation::Any());
      break;
    }
    case IrOpcode::kTypeGuard: {
      Type output_type = op_typer.TypeTypeGuard(node->op(), InputType(node, 0));
      // TypeGuard has no effect on trunction, but the restricted type may help
      // generalize it.
      CheckAndSet(node, output_type, InputTruncation(node, 0));
      break;
    }
    case IrOpcode::kTruncateBigIntToWord64: {
      Type input_type = InputType(node, 0);
      CHECK(input_type.Is(Type::BigInt()));
      CHECK(Truncation::Word64().IsLessGeneralThan(InputTruncation(node, 0)));
      CheckAndSet(node, input_type, Truncation::Word64());
      break;
    }
    case IrOpcode::kChangeTaggedSignedToInt64: {
      Type input_type = InputType(node, 0);
      CHECK(input_type.Is(Type::Number()));
      Truncation output_trunc =
          JoinTruncation(InputTruncation(node, 0), Truncation::Word64());
      CheckAndSet(node, input_type, output_trunc);
      break;
    }
    case IrOpcode::kCheckBigInt: {
      Type input_type = InputType(node, 0);
      input_type = Type::Intersect(input_type, Type::BigInt(), graph_zone());
      CheckAndSet(node, input_type, InputTruncation(node, 0));
      break;
    }
    case IrOpcode::kCheckedBigIntToBigInt64: {
      Type input_type = InputType(node, 0);
      CHECK(input_type.Is(Type::BigInt()));
      input_type =
          Type::Intersect(input_type, Type::SignedBigInt64(), graph_zone());
      CheckAndSet(node, input_type, InputTruncation(node, 0));
      break;
    }
    case IrOpcode::kReturn: {
      const int return_value_count = ValueInputCountOfReturn(node->op());
      for (int i = 0; i < return_value_count; ++i) {
        Type input_type = InputType(node, 1 + i);
        Truncation input_trunc = InputTruncation(node, 1 + i);
        input_trunc = GeneralizeTruncation(input_trunc, input_type);
        // No values must be lost due to truncation.
        CHECK_EQ(input_trunc, Truncation::Any());
      }
      break;
    }
    case IrOpcode::kSLVerifierHint: {
      Type output_type = InputType(node, 0);
      Truncation output_trunc = InputTruncation(node, 0);
      const auto& p = SLVerifierHintParametersOf(node->op());

      if (const Operator* semantics = p.semantics()) {
        switch (semantics->opcode()) {
          case IrOpcode::kPlainPrimitiveToNumber:
            output_type = op_typer.ToNumber(output_type);
            break;
          default:
            UNREACHABLE();
        }
      }

      if (p.override_output_type()) {
        output_type = *p.override_output_type();
      }

      SetType(node, output_type);
      SetTruncation(node, GeneralizeTruncation(output_trunc, output_type));
      break;
    }
    case IrOpcode::kBranch: {
      CHECK_EQ(BranchParametersOf(node->op()).semantics(),
               BranchSemantics::kMachine);
      Type input_type = InputType(node, 0);
      CHECK(input_type.Is(Type::Boolean()) || input_type.Is(Type::Machine()));
      break;
    }
    case IrOpcode::kTypedStateValues: {
      const ZoneVector<MachineType>* machine_types = MachineTypesOf(node->op());
      for (int i = 0; i < static_cast<int>(machine_types->size()); ++i) {
        // Inputs must not be truncated.
        CHECK_EQ(InputTruncation(node, i), Truncation::Any());
        CHECK(IsNonTruncatingMachineTypeFor(machine_types->at(i),
                                            InputType(node, i), graph_zone()));
      }
      break;
    }
    case IrOpcode::kParameter: {
      CHECK(NodeProperties::IsTyped(node));
      SetTruncation(node, Truncation::Any());
      break;
    }
    case IrOpcode::kEnterMachineGraph:
    case IrOpcode::kExitMachineGraph: {
      // Eliminated during lowering.
      UNREACHABLE();
    }

#define OPCODE_CASE(code, ...) case IrOpcode::k##code:
      // Control operators
      OPCODE_CASE(Loop)
      OPCODE_CASE(Switch)
      OPCODE_CASE(IfSuccess)
      OPCODE_CASE(IfException)
      OPCODE_CASE(IfValue)
      OPCODE_CASE(IfDefault)
      OPCODE_CASE(Deoptimize)
      OPCODE_CASE(DeoptimizeIf)
      OPCODE_CASE(DeoptimizeUnless)
      OPCODE_CASE(TrapIf)
      OPCODE_CASE(TrapUnless)
      OPCODE_CASE(Assert)
      OPCODE_CASE(TailCall)
      OPCODE_CASE(Terminate)
      OPCODE_CASE(Throw)
      OPCODE_CASE(TraceInstruction)
      // Constant operators
      OPCODE_CASE(TaggedIndexConstant)
      OPCODE_CASE(Float32Constant)
      OPCODE_CASE(ExternalConstant)
      OPCODE_CASE(NumberConstant)
      OPCODE_CASE(PointerConstant)
      OPCODE_CASE(CompressedHeapConstant)
      OPCODE_CASE(TrustedHeapConstant)
      OPCODE_CASE(RelocatableInt32Constant)
      OPCODE_CASE(RelocatableInt64Constant)
      // Inner operators
      OPCODE_CASE(Select)
      OPCODE_CASE(Phi)
      OPCODE_CASE(InductionVariablePhi)
      OPCODE_CASE(BeginRegion)
      OPCODE_CASE(FinishRegion)
      OPCODE_CASE(StateValues)
      OPCODE_CASE(ArgumentsElementsState)
      OPCODE_CASE(ArgumentsLengthState)
      OPCODE_CASE(ObjectState)
      OPCODE_CASE(ObjectId)
      OPCODE_CASE(TypedObjectState)
      OPCODE_CASE(Call)
      OPCODE_CASE(OsrValue)
      OPCODE_CASE(LoopExit)
      OPCODE_CASE(LoopExitValue)
      OPCODE_CASE(LoopExitEffect)
      OPCODE_CASE(Projection)
      OPCODE_CASE(Retain)
      OPCODE_CASE(MapGuard)
      OPCODE_CASE(Unreachable)
      OPCODE_CASE(Dead)
      OPCODE_CASE(Plug)
      OPCODE_CASE(StaticAssert)
      // Simplified change operators
      OPCODE_CASE(ChangeTaggedSignedToInt32)
      OPCODE_CASE(ChangeTaggedToInt32)
      OPCODE_CASE(ChangeTaggedToInt64)
      OPCODE_CASE(ChangeTaggedToUint32)
      OPCODE_CASE(ChangeTaggedToFloat64)
      OPCODE_CASE(ChangeTaggedToTaggedSigned)
      OPCODE_CASE(ChangeNumberOrHoleToFloat64)
      OPCODE_CASE(ChangeInt64ToTagged)
      OPCODE_CASE(ChangeUint32ToTagged)
      OPCODE_CASE(ChangeFloat64ToTagged)
      OPCODE_CASE(ChangeFloat64ToTaggedPointer)
      OPCODE_CASE(ChangeFloat64OrUndefinedToTagged)
      OPCODE_CASE(ChangeTaggedToBit)
      OPCODE_CASE(ChangeBitToTagged)
      OPCODE_CASE(ChangeInt64ToBigInt)
      OPCODE_CASE(ChangeUint64ToBigInt)
      OPCODE_CASE(TruncateNumberOrOddballToWord32)
      OPCODE_CASE(TruncateNumberOrOddballOrHoleToWord32)
      OPCODE_CASE(TruncateTaggedToFloat64)
      OPCODE_CASE(TruncateTaggedToFloat64PreserveUndefined)
      OPCODE_CASE(TruncateTaggedPointerToBit)
      // Simplified checked operators
      OPCODE_CASE(CheckedInt32Add)
      OPCODE_CASE(CheckedInt32Sub)
      OPCODE_CASE(CheckedInt32Div)
      OPCODE_CASE(CheckedInt32Mod)
      OPCODE_CASE(CheckedUint32Div)
      OPCODE_CASE(CheckedUint32Mod)
      OPCODE_CASE(CheckedInt32Mul)
      OPCODE_CASE(CheckedAdditiveSafeIntegerAdd)
      OPCODE_CASE(CheckedAdditiveSafeIntegerSub)
      OPCODE_CASE(CheckedInt64Add)
      OPCODE_CASE(CheckedInt64Sub)
      OPCODE_CASE(CheckedInt64Mul)
      OPCODE_CASE(CheckedInt64Div)
      OPCODE_CASE(CheckedInt64Mod)
      OPCODE_CASE(CheckedInt32ToTaggedSigned)
      OPCODE_CASE(CheckedInt64ToInt32)
      OPCODE_CASE(CheckedInt64ToAdditiveSafeInteger)
      OPCODE_CASE(CheckedInt64ToTaggedSigned)
      OPCODE_CASE(CheckedUint32Bounds)
      OPCODE_CASE(CheckedUint32ToInt32)
      OPCODE_CASE(CheckedUint32ToTaggedSigned)
      OPCODE_CASE(CheckedUint64Bounds)
      OPCODE_CASE(CheckedUint64ToInt32)
      OPCODE_CASE(CheckedUint64ToInt64)
      OPCODE_CASE(CheckedUint64ToTaggedSigned)
      OPCODE_CASE(CheckedFloat64ToAdditiveSafeInteger)
      OPCODE_CASE(CheckedFloat64ToInt64)
      OPCODE_CASE(CheckedTaggedSignedToInt32)
      OPCODE_CASE(CheckedTaggedToInt32)
      OPCODE_CASE(CheckedTaggedToArrayIndex)
      OPCODE_CASE(CheckedTruncateTaggedToWord32)
      OPCODE_CASE(CheckedTaggedToFloat64)
      OPCODE_CASE(CheckedTaggedToAdditiveSafeInteger)
      OPCODE_CASE(CheckedTaggedToInt64)
      SIMPLIFIED_COMPARE_BINOP_LIST(OPCODE_CASE)
      SIMPLIFIED_NUMBER_BINOP_LIST(OPCODE_CASE)
      SIMPLIFIED_BIGINT_BINOP_LIST(OPCODE_CASE)
      SIMPLIFIED_SPECULATIVE_NUMBER_BINOP_LIST(OPCODE_CASE)
      SIMPLIFIED_NUMBER_UNOP_LIST(OPCODE_CASE)
      // Simplified unary bigint operators
      OPCODE_CASE(BigIntNegate)
      SIMPLIFIED_SPECULATIVE_NUMBER_UNOP_LIST(OPCODE_CASE)
      SIMPLIFIED_SPECULATIVE_BIGINT_UNOP_LIST(OPCODE_CASE)
      SIMPLIFIED_SPECULATIVE_BIGINT_BINOP_LIST(OPCODE_CASE)
      SIMPLIFIED_OTHER_OP_LIST(OPCODE_CASE)
      MACHINE_COMPARE_BINOP_LIST(OPCODE_CASE)
      MACHINE_UNOP_32_LIST(OPCODE_CASE)
      // Binary 32bit machine operators
      OPCODE_CASE(Word32And)
      OPCODE_CASE(Word32Or)
      OPCODE_CASE(Word32Xor)
      OPCODE_CASE(Word32Shl)
      OPCODE_CASE(Word32Shr)
      OPCODE_CASE(Word32Sar)
      OPCODE_CASE(Word32Rol)
      OPCODE_CASE(Word32Ror)
      OPCODE_CASE(Int32AddWithOverflow)
      OPCODE_CASE(Int32SubWithOverflow)
      OPCODE_CASE(Int32Mul)
      OPCODE_CASE(Int32MulWithOverflow)
      OPCODE_CASE(Int32MulHigh)
      OPCODE_CASE(Int32Div)
      OPCODE_CASE(Int32Mod)
      OPCODE_CASE(Uint32Div)
      OPCODE_CASE(Uint32Mod)
      OPCODE_CASE(Uint32MulHigh)
      // Binary 64bit machine operators
      OPCODE_CASE(Word64And)
      OPCODE_CASE(Word64Or)
      OPCODE_CASE(Word64Xor)
      OPCODE_CASE(Word64Shl)
      OPCODE_CASE(Word64Shr)
      OPCODE_CASE(Word64Sar)
      OPCODE_CASE(Word64Rol)
      OPCODE_CASE(Word64Ror)
      OPCODE_CASE(Word64RolLowerable)
      OPCODE_CASE(Word64RorLowerable)
      OPCODE_CASE(Int64AddWithOverflow)
      OPCODE_CASE(Int64SubWithOverflow)
      OPCODE_CASE(Int64Mul)
      OPCODE_CASE(Int64MulHigh)
      OPCODE_CASE(Uint64MulHigh)
      OPCODE_CASE(Int64MulWithOverflow)
      OPCODE_CASE(Int64Div)
      OPCODE_CASE(Int64Mod)
      OPCODE_CASE(Uint64Div)
      OPCODE_CASE(Uint64Mod)
      MACHINE_FLOAT32_UNOP_LIST(OPCODE_CASE)
      MACHINE_FLOAT32_BINOP_LIST(OPCODE_CASE)
      MACHINE_FLOAT64_UNOP_LIST(OPCODE_CASE)
      MACHINE_FLOAT64_BINOP_LIST(OPCODE_CASE)
      MACHINE_ATOMIC_OP_LIST(OPCODE_CASE)
      OPCODE_CASE(AbortCSADcheck)
      OPCODE_CASE(DebugBreak)
      IF_HARDWARE_SANDBOX(OPCODE_CASE, SwitchSandboxMode)
      OPCODE_CASE(Comment)
      OPCODE_CASE(Load)
      OPCODE_CASE(LoadImmutable)
      OPCODE_CASE(Store)
      OPCODE_CASE(StorePair)
      OPCODE_CASE(StoreIndirectPointer)
      OPCODE_CASE(StackSlot)
      OPCODE_CASE(Word32Popcnt)
      OPCODE_CASE(Word64Popcnt)
      OPCODE_CASE(Word64Clz)
      OPCODE_CASE(Word64Ctz)
      OPCODE_CASE(Word64ClzLowerable)
      OPCODE_CASE(Word64CtzLowerable)
      OPCODE_CASE(Word64ReverseBits)
      OPCODE_CASE(Word64ReverseBytes)
      OPCODE_CASE(Simd128ReverseBytes)
      OPCODE_CASE(Int64AbsWithOverflow)
      OPCODE_CASE(BitcastTaggedToWord)
      OPCODE_CASE(BitcastTaggedToWordForTagAndSmiBits)
      OPCODE_CASE(BitcastWordToTagged)
      OPCODE_CASE(BitcastWordToTaggedSigned)
      OPCODE_CASE(TruncateFloat64ToWord32)
      OPCODE_CASE(ChangeFloat64ToInt32)
      OPCODE_CASE(ChangeFloat64ToUint32)
      OPCODE_CASE(ChangeFloat64ToUint64)
      OPCODE_CASE(Float64SilenceNaN)
      OPCODE_CASE(TruncateFloat64ToInt64)
      OPCODE_CASE(TruncateFloat64ToUint32)
      OPCODE_CASE(TruncateFloat32ToInt32)
      OPCODE_CASE(TruncateFloat32ToUint32)
      OPCODE_CASE(TryTruncateFloat32ToInt64)
      OPCODE_CASE(TryTruncateFloat64ToInt64)
      OPCODE_CASE(TryTruncateFloat32ToUint64)
      OPCODE_CASE(TryTruncateFloat64ToUint64)
      OPCODE_CASE(TryTruncateFloat64ToInt32)
      OPCODE_CASE(TryTruncateFloat64ToUint32)
      OPCODE_CASE(ChangeInt32ToFloat64)
      OPCODE_CASE(BitcastWord32ToWord64)
      OPCODE_CASE(ChangeInt64ToFloat64)
      OPCODE_CASE(ChangeUint32ToFloat64)
      OPCODE_CASE(ChangeFloat16RawBitsToFloat64)
      OPCODE_CASE(TruncateFloat64ToFloat32)
      OPCODE_CASE(TruncateFloat64ToFloat16RawBits)
      OPCODE_CASE(TruncateInt64ToInt32)
      OPCODE_CASE(RoundFloat64ToInt32)
      OPCODE_CASE(RoundInt32ToFloat32)
      OPCODE_CASE(RoundInt64ToFloat32)
      OPCODE_CASE(RoundInt64ToFloat64)
      OPCODE_CASE(RoundUint32ToFloat32)
      OPCODE_CASE(RoundUint64ToFloat32)
      OPCODE_CASE(RoundUint64ToFloat64)
      OPCODE_CASE(BitcastFloat32ToInt32)
      OPCODE_CASE(BitcastFloat64ToInt64)
      OPCODE_CASE(BitcastInt32ToFloat32)
      OPCODE_CASE(BitcastInt64ToFloat64)
      OPCODE_CASE(Float64ExtractLowWord32)
      OPCODE_CASE(Float64ExtractHighWord32)
      OPCODE_CASE(Float64InsertLowWord32)
      OPCODE_CASE(Float64InsertHighWord32)
      OPCODE_CASE(Word32Select)
      OPCODE_CASE(Word64Select)
      OPCODE_CASE(Float32Select)
      OPCODE_CASE(Float64Select)
      OPCODE_CASE(LoadStackCheckOffset)
      OPCODE_CASE(LoadFramePointer)
      IF_WASM(OPCODE_CASE, LoadStackPointer)
      IF_WASM(OPCODE_CASE, SetStackPointer)
      OPCODE_CASE(LoadParentFramePointer)
      OPCODE_CASE(LoadRootRegister)
      OPCODE_CASE(UnalignedLoad)
      OPCODE_CASE(UnalignedStore)
      OPCODE_CASE(Int32PairAdd)
      OPCODE_CASE(Int32PairSub)
      OPCODE_CASE(Int32PairMul)
      OPCODE_CASE(Word32PairShl)
      OPCODE_CASE(Word32PairShr)
      OPCODE_CASE(Word32PairSar)
      OPCODE_CASE(ProtectedLoad)
      OPCODE_CASE(ProtectedStore)
      OPCODE_CASE(LoadTrapOnNull)
      OPCODE_CASE(StoreTrapOnNull)
      OPCODE_CASE(MemoryBarrier)
      OPCODE_CASE(SignExtendWord8ToInt32)
      OPCODE_CASE(SignExtendWord16ToInt32)
      OPCODE_CASE(SignExtendWord8ToInt64)
      OPCODE_CASE(SignExtendWord16ToInt64)
      OPCODE_CASE(SignExtendWord32ToInt64)
      OPCODE_CASE(StackPointerGreaterThan)
      JS_SIMPLE_BINOP_LIST(OPCODE_CASE)
      JS_SIMPLE_UNOP_LIST(OPCODE_CASE)
      JS_OBJECT_OP_LIST(OPCODE_CASE)
      JS_CONTEXT_OP_LIST(OPCODE_CASE)
      JS_CALL_OP_LIST(OPCODE_CASE)
      JS_CONSTRUCT_OP_LIST(OPCODE_CASE)
      OPCODE_CASE(JSAsyncFunctionEnter)
      OPCODE_CASE(JSAsyncFunctionReject)
      OPCODE_CASE(JSAsyncFunctionResolve)
      OPCODE_CASE(JSCallRuntime)
      OPCODE_CASE(JSDetachContextCell)
      OPCODE_CASE(JSForInEnumerate)
      OPCODE_CASE(JSForInNext)
      OPCODE_CASE(JSForInPrepare)
      OPCODE_CASE(JSForOfNext)
      OPCODE_CASE(JSGetIterator)
      OPCODE_CASE(JSLoadMessage)
      OPCODE_CASE(JSStoreMessage)
      OPCODE_CASE(JSLoadModule)
      OPCODE_CASE(JSStoreModule)
      OPCODE_CASE(JSGetImportMeta)
      OPCODE_CASE(JSGeneratorStore)
      OPCODE_CASE(JSGeneratorRestoreContinuation)
      OPCODE_CASE(JSGeneratorRestoreContextNoCell)
      OPCODE_CASE(JSGeneratorRestoreRegister)
      OPCODE_CASE(JSGeneratorRestoreInputOrDebugPos)
      OPCODE_CASE(JSFulfillPromise)
      OPCODE_CASE(JSPerformPromiseThen)
      OPCODE_CASE(JSPromiseResolve)
      OPCODE_CASE(JSRejectPromise)
      OPCODE_CASE(JSResolvePromise)
      OPCODE_CASE(JSObjectIsArray)
      OPCODE_CASE(JSRegExpTest)
      OPCODE_CASE(JSDebugger) {
        // TODO(nicohartmann@): These operators might need to be supported.
        break;
      }
      MACHINE_SIMD128_OP_LIST(OPCODE_CASE)
      IF_WASM(MACHINE_SIMD256_OP_LIST, OPCODE_CASE)
      IF_WASM(SIMPLIFIED_WASM_OP_LIST, OPCODE_CASE) {
        // SIMD operators should not be in the graph, yet.
        UNREACHABLE();
      }
#undef OPCODE_CASE
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
