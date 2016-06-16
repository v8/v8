// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/type-hint-analyzer.h"

#include "src/assembler.h"
#include "src/code-stubs.h"
#include "src/compiler/type-hints.h"
#include "src/ic/ic-state.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// TODO(bmeurer): This detour via types is ugly.
BinaryOperationHints::Hint ToBinaryOperationHint(Type* type) {
  if (type->Is(Type::None())) return BinaryOperationHints::kNone;
  if (type->Is(Type::SignedSmall())) return BinaryOperationHints::kSignedSmall;
  if (type->Is(Type::Signed32())) return BinaryOperationHints::kSigned32;
  if (type->Is(Type::Number())) return BinaryOperationHints::kNumberOrUndefined;
  if (type->Is(Type::String())) return BinaryOperationHints::kString;
  return BinaryOperationHints::kAny;
}

CompareOperationHints::Hint ToCompareOperationHint(
    CompareICState::State state) {
  switch (state) {
    case CompareICState::UNINITIALIZED:
      return CompareOperationHints::kNone;
    case CompareICState::BOOLEAN:
      return CompareOperationHints::kBoolean;
    case CompareICState::SMI:
      return CompareOperationHints::kSignedSmall;
    case CompareICState::NUMBER:
      return CompareOperationHints::kNumber;
    case CompareICState::STRING:
      return CompareOperationHints::kString;
    case CompareICState::INTERNALIZED_STRING:
      return CompareOperationHints::kInternalizedString;
    case CompareICState::UNIQUE_NAME:
      return CompareOperationHints::kUniqueName;
    case CompareICState::RECEIVER:
    case CompareICState::KNOWN_RECEIVER:
      return CompareOperationHints::kReceiver;
    case CompareICState::GENERIC:
      return CompareOperationHints::kAny;
  }
  UNREACHABLE();
  return CompareOperationHints::kAny;
}

}  // namespace

bool TypeHintAnalysis::GetBinaryOperationHints(
    TypeFeedbackId id, BinaryOperationHints* hints) const {
  auto i = infos_.find(id);
  if (i == infos_.end()) return false;
  Handle<Code> code = i->second;
  DCHECK_EQ(Code::BINARY_OP_IC, code->kind());
  BinaryOpICState state(code->GetIsolate(), code->extra_ic_state());
  *hints = BinaryOperationHints(ToBinaryOperationHint(state.GetLeftType()),
                                ToBinaryOperationHint(state.GetRightType()),
                                ToBinaryOperationHint(state.GetResultType()));
  return true;
}

bool TypeHintAnalysis::GetCompareOperationHints(
    TypeFeedbackId id, CompareOperationHints* hints) const {
  auto i = infos_.find(id);
  if (i == infos_.end()) return false;
  Handle<Code> code = i->second;
  DCHECK_EQ(Code::COMPARE_IC, code->kind());

  Handle<Map> map;
  Map* raw_map = code->FindFirstMap();
  if (raw_map != nullptr) Map::TryUpdate(handle(raw_map)).ToHandle(&map);

  CompareICStub stub(code->stub_key(), code->GetIsolate());
  *hints = CompareOperationHints(ToCompareOperationHint(stub.left()),
                                 ToCompareOperationHint(stub.right()),
                                 ToCompareOperationHint(stub.state()));
  return true;
}

bool TypeHintAnalysis::GetToBooleanHints(TypeFeedbackId id,
                                         ToBooleanHints* hints) const {
  auto i = infos_.find(id);
  if (i == infos_.end()) return false;
  Handle<Code> code = i->second;
  DCHECK_EQ(Code::TO_BOOLEAN_IC, code->kind());
  ToBooleanICStub stub(code->GetIsolate(), code->extra_ic_state());
// TODO(bmeurer): Replace ToBooleanICStub::Types with ToBooleanHints.
#define ASSERT_COMPATIBLE(NAME, Name)         \
  STATIC_ASSERT(1 << ToBooleanICStub::NAME == \
                static_cast<int>(ToBooleanHint::k##Name))
  ASSERT_COMPATIBLE(UNDEFINED, Undefined);
  ASSERT_COMPATIBLE(BOOLEAN, Boolean);
  ASSERT_COMPATIBLE(NULL_TYPE, Null);
  ASSERT_COMPATIBLE(SMI, SmallInteger);
  ASSERT_COMPATIBLE(SPEC_OBJECT, Receiver);
  ASSERT_COMPATIBLE(STRING, String);
  ASSERT_COMPATIBLE(SYMBOL, Symbol);
  ASSERT_COMPATIBLE(HEAP_NUMBER, HeapNumber);
  ASSERT_COMPATIBLE(SIMD_VALUE, SimdValue);
#undef ASSERT_COMPATIBLE
  *hints = ToBooleanHints(stub.types().ToIntegral());
  return true;
}

TypeHintAnalysis* TypeHintAnalyzer::Analyze(Handle<Code> code) {
  DisallowHeapAllocation no_gc;
  TypeHintAnalysis::Infos infos(zone());
  Isolate* const isolate = code->GetIsolate();
  int const mask = RelocInfo::ModeMask(RelocInfo::CODE_TARGET_WITH_ID);
  for (RelocIterator it(*code, mask); !it.done(); it.next()) {
    RelocInfo* rinfo = it.rinfo();
    Address target_address = rinfo->target_address();
    Code* target = Code::GetCodeFromTargetAddress(target_address);
    switch (target->kind()) {
      case Code::BINARY_OP_IC:
      case Code::COMPARE_IC:
      case Code::TO_BOOLEAN_IC: {
        // Add this feedback to the {infos}.
        TypeFeedbackId id(static_cast<unsigned>(rinfo->data()));
        infos.insert(std::make_pair(id, handle(target, isolate)));
        break;
      }
      default:
        // Ignore the remaining code objects.
        break;
    }
  }
  return new (zone()) TypeHintAnalysis(infos, zone());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
