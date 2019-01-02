// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/profiler/profiler-listener.h"

#include "src/deoptimizer.h"
#include "src/objects-inl.h"
#include "src/profiler/cpu-profiler.h"
#include "src/profiler/profile-generator-inl.h"
#include "src/reloc-info.h"
#include "src/snapshot/embedded-data.h"
#include "src/source-position-table.h"
#include "src/wasm/wasm-code-manager.h"

namespace v8 {
namespace internal {

ProfilerListener::ProfilerListener(Isolate* isolate,
                                   CodeEventObserver* observer)
    : isolate_(isolate), observer_(observer) {}

ProfilerListener::~ProfilerListener() = default;

void ProfilerListener::CallbackEvent(Name name, Address entry_point) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = entry_point;
  rec->entry = NewCodeEntry(CodeEventListener::CALLBACK_TAG, GetName(name));
  rec->instruction_size = 1;
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeCreateEvent(CodeEventListener::LogEventsAndTags tag,
                                       AbstractCode code, const char* name) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = code->InstructionStart();
  rec->entry = NewCodeEntry(tag, GetName(name), CodeEntry::kEmptyResourceName,
                            CpuProfileNode::kNoLineNumberInfo,
                            CpuProfileNode::kNoColumnNumberInfo, nullptr,
                            code->InstructionStart());
  RecordInliningInfo(rec->entry, code);
  rec->instruction_size = code->InstructionSize();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeCreateEvent(CodeEventListener::LogEventsAndTags tag,
                                       AbstractCode code, Name name) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = code->InstructionStart();
  rec->entry = NewCodeEntry(tag, GetName(name), CodeEntry::kEmptyResourceName,
                            CpuProfileNode::kNoLineNumberInfo,
                            CpuProfileNode::kNoColumnNumberInfo, nullptr,
                            code->InstructionStart());
  RecordInliningInfo(rec->entry, code);
  rec->instruction_size = code->InstructionSize();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeCreateEvent(CodeEventListener::LogEventsAndTags tag,
                                       AbstractCode code,
                                       SharedFunctionInfo shared,
                                       Name script_name) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = code->InstructionStart();
  rec->entry = NewCodeEntry(tag, GetName(shared->DebugName()),
                            GetName(InferScriptName(script_name, shared)),
                            CpuProfileNode::kNoLineNumberInfo,
                            CpuProfileNode::kNoColumnNumberInfo, nullptr,
                            code->InstructionStart());
  RecordInliningInfo(rec->entry, code);
  rec->entry->FillFunctionInfo(shared);
  rec->instruction_size = code->InstructionSize();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeCreateEvent(CodeEventListener::LogEventsAndTags tag,
                                       AbstractCode abstract_code,
                                       SharedFunctionInfo shared,
                                       Name script_name, int line, int column) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = abstract_code->InstructionStart();
  std::unique_ptr<SourcePositionTable> line_table;
  if (shared->script()->IsScript()) {
    Script script = Script::cast(shared->script());
    line_table.reset(new SourcePositionTable());
    for (SourcePositionTableIterator it(abstract_code->source_position_table());
         !it.done(); it.Advance()) {
      // TODO(alph,tebbi) Skipping inlined positions for now, because they might
      // refer to a different script.
      if (it.source_position().InliningId() != SourcePosition::kNotInlined)
        continue;
      int position = it.source_position().ScriptOffset();
      int line_number = script->GetLineNumber(position) + 1;
      line_table->SetPosition(it.code_offset(), line_number);
    }
  }
  rec->entry =
      NewCodeEntry(tag, GetName(shared->DebugName()),
                   GetName(InferScriptName(script_name, shared)), line, column,
                   std::move(line_table), abstract_code->InstructionStart());
  RecordInliningInfo(rec->entry, abstract_code);
  rec->entry->FillFunctionInfo(shared);
  rec->instruction_size = abstract_code->InstructionSize();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeCreateEvent(CodeEventListener::LogEventsAndTags tag,
                                       const wasm::WasmCode* code,
                                       wasm::WasmName name) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = code->instruction_start();
  rec->entry = NewCodeEntry(
      tag, GetName(name.start()), CodeEntry::kWasmResourceNamePrefix,
      CpuProfileNode::kNoLineNumberInfo, CpuProfileNode::kNoColumnNumberInfo,
      nullptr, code->instruction_start());
  rec->instruction_size = code->instructions().length();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeMoveEvent(AbstractCode from, AbstractCode to) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_MOVE);
  CodeMoveEventRecord* rec = &evt_rec.CodeMoveEventRecord_;
  rec->from_instruction_start = from->InstructionStart();
  rec->to_instruction_start = to->InstructionStart();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeDisableOptEvent(AbstractCode code,
                                           SharedFunctionInfo shared) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_DISABLE_OPT);
  CodeDisableOptEventRecord* rec = &evt_rec.CodeDisableOptEventRecord_;
  rec->instruction_start = code->InstructionStart();
  rec->bailout_reason = GetBailoutReason(shared->disable_optimization_reason());
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::CodeDeoptEvent(Code code, DeoptimizeKind kind,
                                      Address pc, int fp_to_sp_delta) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_DEOPT);
  CodeDeoptEventRecord* rec = &evt_rec.CodeDeoptEventRecord_;
  Deoptimizer::DeoptInfo info = Deoptimizer::GetDeoptInfo(code, pc);
  rec->instruction_start = code->InstructionStart();
  rec->deopt_reason = DeoptimizeReasonToString(info.deopt_reason);
  rec->deopt_id = info.deopt_id;
  rec->pc = pc;
  rec->fp_to_sp_delta = fp_to_sp_delta;

  // When a function is deoptimized, we store the deoptimized frame information
  // for the use of GetDeoptInfos().
  AttachDeoptInlinedFrames(code, rec);
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::GetterCallbackEvent(Name name, Address entry_point) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = entry_point;
  rec->entry =
      NewCodeEntry(CodeEventListener::CALLBACK_TAG, GetConsName("get ", name));
  rec->instruction_size = 1;
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::RegExpCodeCreateEvent(AbstractCode code, String source) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = code->InstructionStart();
  rec->entry = NewCodeEntry(
      CodeEventListener::REG_EXP_TAG, GetConsName("RegExp: ", source),
      CodeEntry::kEmptyResourceName, CpuProfileNode::kNoLineNumberInfo,
      CpuProfileNode::kNoColumnNumberInfo, nullptr, code->InstructionStart());
  rec->instruction_size = code->InstructionSize();
  DispatchCodeEvent(evt_rec);
}

void ProfilerListener::SetterCallbackEvent(Name name, Address entry_point) {
  CodeEventsContainer evt_rec(CodeEventRecord::CODE_CREATION);
  CodeCreateEventRecord* rec = &evt_rec.CodeCreateEventRecord_;
  rec->instruction_start = entry_point;
  rec->entry =
      NewCodeEntry(CodeEventListener::CALLBACK_TAG, GetConsName("set ", name));
  rec->instruction_size = 1;
  DispatchCodeEvent(evt_rec);
}

Name ProfilerListener::InferScriptName(Name name, SharedFunctionInfo info) {
  if (name->IsString() && String::cast(name)->length()) return name;
  if (!info->script()->IsScript()) return name;
  Object source_url = Script::cast(info->script())->source_url();
  return source_url->IsName() ? Name::cast(source_url) : name;
}

void ProfilerListener::RecordInliningInfo(CodeEntry* entry,
                                          AbstractCode abstract_code) {
  if (!abstract_code->IsCode()) return;
  Code code = abstract_code->GetCode();
  if (code->kind() != Code::OPTIMIZED_FUNCTION) return;

  // Needed for InliningStack().
  HandleScope scope(isolate_);
  int last_inlining_id = -2;
  for (SourcePositionTableIterator it(abstract_code->source_position_table());
       !it.done(); it.Advance()) {
    int code_offset = it.code_offset();

    // Save space by not duplicating repeated entries that map to the same
    // inlining ID. We might get multiple source positions per inlining ID, but
    // they all map to the same line. This automatically collapses adjacent
    // inlining stacks (or empty stacks) that are exactly the same.
    if (it.source_position().InliningId() == last_inlining_id) continue;
    last_inlining_id = it.source_position().InliningId();

    // Only look at positions for inlined calls.
    if (it.source_position().InliningId() == SourcePosition::kNotInlined) {
      entry->AddInlineStack(code_offset, std::vector<InlineEntry>());
      continue;
    }

    std::vector<SourcePositionInfo> stack =
        it.source_position().InliningStack(handle(code, isolate_));
    std::vector<InlineEntry> inline_stack;
    for (SourcePositionInfo& pos_info : stack) {
      if (pos_info.position.ScriptOffset() == kNoSourcePosition) continue;
      if (pos_info.script.is_null()) continue;

      int line_number =
          pos_info.script->GetLineNumber(pos_info.position.ScriptOffset()) + 1;

      const char* resource_name =
          (pos_info.script->name()->IsName())
              ? GetName(Name::cast(pos_info.script->name()))
              : CodeEntry::kEmptyResourceName;

      // We need the start line number and column number of the function for
      // kLeafNodeLineNumbers mode. Creating a SourcePositionInfo is a handy way
      // of getting both easily.
      SourcePositionInfo start_pos_info(
          SourcePosition(pos_info.shared->StartPosition()), pos_info.shared);

      std::unique_ptr<CodeEntry> inline_entry = base::make_unique<CodeEntry>(
          entry->tag(), GetName(pos_info.shared->DebugName()), resource_name,
          start_pos_info.line + 1, start_pos_info.column + 1, nullptr,
          code->InstructionStart());
      inline_entry->FillFunctionInfo(*pos_info.shared);
      inline_stack.push_back(InlineEntry{std::move(inline_entry), line_number});
    }
    if (!inline_stack.empty()) {
      entry->AddInlineStack(code_offset, std::move(inline_stack));
    }
  }
}

void ProfilerListener::AttachDeoptInlinedFrames(Code code,
                                                CodeDeoptEventRecord* rec) {
  int deopt_id = rec->deopt_id;
  SourcePosition last_position = SourcePosition::Unknown();
  int mask = RelocInfo::ModeMask(RelocInfo::DEOPT_ID) |
             RelocInfo::ModeMask(RelocInfo::DEOPT_SCRIPT_OFFSET) |
             RelocInfo::ModeMask(RelocInfo::DEOPT_INLINING_ID);

  rec->deopt_frames = nullptr;
  rec->deopt_frame_count = 0;

  for (RelocIterator it(code, mask); !it.done(); it.next()) {
    RelocInfo* info = it.rinfo();
    if (info->rmode() == RelocInfo::DEOPT_SCRIPT_OFFSET) {
      int script_offset = static_cast<int>(info->data());
      it.next();
      DCHECK(it.rinfo()->rmode() == RelocInfo::DEOPT_INLINING_ID);
      int inlining_id = static_cast<int>(it.rinfo()->data());
      last_position = SourcePosition(script_offset, inlining_id);
      continue;
    }
    if (info->rmode() == RelocInfo::DEOPT_ID) {
      if (deopt_id != static_cast<int>(info->data())) continue;
      DCHECK(last_position.IsKnown());

      // SourcePosition::InliningStack allocates a handle for the SFI of each
      // frame. These don't escape this function, but quickly add up. This
      // scope limits their lifetime.
      HandleScope scope(isolate_);
      std::vector<SourcePositionInfo> stack =
          last_position.InliningStack(handle(code, isolate_));
      CpuProfileDeoptFrame* deopt_frames =
          new CpuProfileDeoptFrame[stack.size()];

      int deopt_frame_count = 0;
      for (SourcePositionInfo& pos_info : stack) {
        if (pos_info.position.ScriptOffset() == kNoSourcePosition) continue;
        if (pos_info.script.is_null()) continue;
        int script_id = pos_info.script->id();
        size_t offset = static_cast<size_t>(pos_info.position.ScriptOffset());
        deopt_frames[deopt_frame_count++] = {script_id, offset};
      }
      rec->deopt_frames = deopt_frames;
      rec->deopt_frame_count = deopt_frame_count;
      break;
    }
  }
}

CodeEntry* ProfilerListener::NewCodeEntry(
    CodeEventListener::LogEventsAndTags tag, const char* name,
    const char* resource_name, int line_number, int column_number,
    std::unique_ptr<SourcePositionTable> line_info, Address instruction_start) {
  return new CodeEntry(tag, name, resource_name, line_number, column_number,
                       std::move(line_info), instruction_start);
}

}  // namespace internal
}  // namespace v8
