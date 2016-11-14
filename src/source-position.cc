// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/source-position.h"
#include "src/compilation-info.h"
#include "src/objects-inl.h"

namespace v8 {
namespace internal {

std::ostream& operator<<(std::ostream& out, const SourcePositionInfo& pos) {
  Handle<SharedFunctionInfo> function;
  if (pos.function.ToHandle(&function)) {
    Handle<Script> script(Script::cast(function->script()));
    out << "<";
    if (script->name()->IsString()) {
      out << String::cast(script->name())->ToCString(DISALLOW_NULLS).get();
    } else {
      out << "unknown";
    }
    out << ":" << pos.line + 1 << ":" << pos.column + 1 << ">";
  } else {
    out << "<unknown:" << pos.position.ScriptOffset() << ">";
  }
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const std::vector<SourcePositionInfo>& stack) {
  out << stack.back();
  for (int i = static_cast<int>(stack.size()) - 2; i >= 0; --i) {
    out << " inlined at ";
    out << stack[i];
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, const SourcePosition& pos) {
  if (pos.isInlined()) {
    out << "<inlined(" << pos.InliningId() << "):";
  } else {
    out << "<not inlined:";
  }
  out << pos.ScriptOffset() << ">";
  return out;
}

SourcePositionInfo SourcePosition::Info(
    Handle<SharedFunctionInfo> function) const {
  Handle<Script> script(Script::cast(function->script()));
  SourcePositionInfo result(*this);
  Script::PositionInfo pos;
  if (Script::GetPositionInfo(script, ScriptOffset(), &pos,
                              Script::WITH_OFFSET)) {
    result.line = pos.line;
    result.column = pos.column;
  }
  result.function = function;
  return result;
}

std::vector<SourcePositionInfo> SourcePosition::InliningStack(
    CompilationInfo* cinfo) const {
  if (!isInlined()) {
    return std::vector<SourcePositionInfo>{Info(cinfo->shared_info())};
  } else {
    InliningPosition inl = cinfo->inlined_functions()[InliningId()].position;
    std::vector<SourcePositionInfo> stack = inl.position.InliningStack(cinfo);
    stack.push_back(Info(cinfo->inlined_functions()[InliningId()].shared_info));
    return stack;
  }
}

std::vector<SourcePositionInfo> SourcePosition::InliningStack(
    Handle<Code> code) const {
  Handle<DeoptimizationInputData> deopt_data(
      DeoptimizationInputData::cast(code->deoptimization_data()));
  if (!isInlined()) {
    Handle<SharedFunctionInfo> function(
        SharedFunctionInfo::cast(deopt_data->SharedFunctionInfo()));

    return std::vector<SourcePositionInfo>{Info(function)};
  } else {
    InliningPosition inl = deopt_data->InliningPositions()->get(InliningId());
    std::vector<SourcePositionInfo> stack = inl.position.InliningStack(code);
    if (inl.inlined_function_id == -1) {
      stack.push_back(SourcePositionInfo(*this));
    } else {
      Handle<SharedFunctionInfo> function(SharedFunctionInfo::cast(
          deopt_data->LiteralArray()->get(inl.inlined_function_id)));
      stack.push_back(Info(function));
    }
    return stack;
  }
}

void SourcePosition::Print(std::ostream& out,
                           SharedFunctionInfo* function) const {
  Script* script = Script::cast(function->script());
  Object* source_name = script->name();
  Script::PositionInfo pos;
  script->GetPositionInfo(ScriptOffset(), &pos, Script::WITH_OFFSET);
  out << "<";
  if (source_name->IsString()) {
    out << String::cast(source_name)
               ->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL)
               .get();
  } else {
    out << "unknown";
  }
  out << ":" << pos.line + 1 << ":" << pos.column + 1 << ">";
}

void SourcePosition::Print(std::ostream& out, Code* code) const {
  DeoptimizationInputData* deopt_data =
      DeoptimizationInputData::cast(code->deoptimization_data());
  if (!isInlined()) {
    SharedFunctionInfo* function(
        SharedFunctionInfo::cast(deopt_data->SharedFunctionInfo()));
    Print(out, function);
  } else {
    InliningPosition inl = deopt_data->InliningPositions()->get(InliningId());
    if (inl.inlined_function_id == -1) {
      out << *this;
    } else {
      SharedFunctionInfo* function = SharedFunctionInfo::cast(
          deopt_data->LiteralArray()->get(inl.inlined_function_id));
      Print(out, function);
    }
    out << " inlined at ";
    inl.position.Print(out, code);
  }
}

}  // namespace internal
}  // namespace v8
