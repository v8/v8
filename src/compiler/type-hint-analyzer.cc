// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/type-hint-analyzer.h"

#include "src/assembler.h"
#include "src/compiler/type-hints.h"
#include "src/ic/ic-state.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// TODO(bmeurer): This detour via types is ugly.
BinaryOperationHints::Hint ToHint(Type* type) {
  if (type->Is(Type::None())) return BinaryOperationHints::kNone;
  if (type->Is(Type::SignedSmall())) return BinaryOperationHints::kSignedSmall;
  if (type->Is(Type::Signed32())) return BinaryOperationHints::kSigned32;
  if (type->Is(Type::Number())) return BinaryOperationHints::kNumber;
  if (type->Is(Type::String())) return BinaryOperationHints::kString;
  return BinaryOperationHints::kAny;
}

}  // namespace


bool TypeHintAnalysis::GetBinaryOperationHints(
    TypeFeedbackId id, BinaryOperationHints* hints) const {
  auto i = infos_.find(id);
  if (i == infos_.end()) return false;
  Handle<Code> code = i->second;
  DCHECK_EQ(Code::BINARY_OP_IC, code->kind());
  BinaryOpICState state(code->GetIsolate(), code->extra_ic_state());
  *hints = BinaryOperationHints(ToHint(state.GetLeftType()),
                                ToHint(state.GetRightType()),
                                ToHint(state.GetResultType()));
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
      case Code::BINARY_OP_IC: {
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
  return new (zone()) TypeHintAnalysis(infos);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
