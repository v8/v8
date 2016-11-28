// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/inspector/wasm-translation.h"

#include <algorithm>

#include "src/debug/debug-interface.h"
#include "src/inspector/protocol/Debugger.h"
#include "src/inspector/script-breakpoint.h"
#include "src/inspector/string-util.h"
#include "src/inspector/v8-debugger-agent-impl.h"
#include "src/inspector/v8-debugger-script.h"
#include "src/inspector/v8-debugger.h"
#include "src/inspector/v8-inspector-impl.h"

using namespace v8_inspector;
using namespace v8;

namespace {
int GetScriptId(Isolate *isolate, Local<Object> script_wrapper) {
  Local<Value> script_id = script_wrapper
                               ->Get(isolate->GetCurrentContext(),
                                     toV8StringInternalized(isolate, "id"))
                               .ToLocalChecked();
  DCHECK(script_id->IsInt32());
  return script_id->Int32Value(isolate->GetCurrentContext()).FromJust();
}

String16 GetScriptName(Isolate *isolate, Local<Object> script_wrapper) {
  Local<Value> script_name = script_wrapper
                                 ->Get(isolate->GetCurrentContext(),
                                       toV8StringInternalized(isolate, "name"))
                                 .ToLocalChecked();
  DCHECK(script_name->IsString());
  return toProtocolString(script_name.As<String>());
}

}  // namespace

class WasmTranslation::TranslatorImpl {
 public:
  struct TransLocation {
    WasmTranslation *translation;
    String16 script_id;
    int line;
    int column;
    int context_group_id;
    TransLocation(WasmTranslation *translation, String16 script_id, int line,
                  int column, int context_group_id)
        : translation(translation),
          script_id(script_id),
          line(line),
          column(column),
          context_group_id(context_group_id) {}
  };

  virtual void Translate(TransLocation *loc) = 0;
  virtual void TranslateBack(TransLocation *loc) = 0;

  class RawTranslator;
  class DisassemblingTranslator;
};

class WasmTranslation::TranslatorImpl::RawTranslator
    : public WasmTranslation::TranslatorImpl {
 public:
  void Translate(TransLocation *loc) {}
  void TranslateBack(TransLocation *loc) {}
};

class WasmTranslation::TranslatorImpl::DisassemblingTranslator
    : public WasmTranslation::TranslatorImpl {
  using OffsetTable = std::vector<std::tuple<uint32_t, int, int>>;

 public:
  DisassemblingTranslator(Isolate *isolate, Local<Object> script)
      : script_(isolate, script) {}

  void Translate(TransLocation *loc) {
    const OffsetTable &offset_table = GetOffsetTable(loc);
    DCHECK(!offset_table.empty());
    uint32_t byte_offset = static_cast<uint32_t>(loc->column);

    // Binary search for the given offset.
    unsigned left = 0;                                            // inclusive
    unsigned right = static_cast<unsigned>(offset_table.size());  // exclusive
    while (right - left > 1) {
      unsigned mid = (left + right) / 2;
      if (std::get<0>(offset_table[mid]) <= byte_offset) {
        left = mid;
      } else {
        right = mid;
      }
    }

    loc->script_id = GetFakeScriptId(loc);
    if (std::get<0>(offset_table[left]) == byte_offset) {
      loc->line = std::get<1>(offset_table[left]);
      loc->column = std::get<2>(offset_table[left]);
    } else {
      loc->line = 0;
      loc->column = 0;
    }
  }

  void TranslateBack(TransLocation *loc) {
    int func_index = GetFunctionIndexFromFakeScriptId(loc->script_id);
    const OffsetTable *reverse_table = GetReverseTable(func_index);
    if (!reverse_table) return;
    DCHECK(!reverse_table->empty());

    // Binary search for the given line and column.
    unsigned left = 0;                                              // inclusive
    unsigned right = static_cast<unsigned>(reverse_table->size());  // exclusive
    while (right - left > 1) {
      unsigned mid = (left + right) / 2;
      auto &entry = (*reverse_table)[mid];
      if (std::get<1>(entry) < loc->line ||
          (std::get<1>(entry) == loc->line &&
           std::get<2>(entry) <= loc->column)) {
        left = mid;
      } else {
        right = mid;
      }
    }

    int found_byte_offset = 0;
    // If we found an exact match, use it. Otherwise check whether the next
    // bigger entry is still in the same line. Report that one then.
    if (std::get<1>((*reverse_table)[left]) == loc->line &&
        std::get<2>((*reverse_table)[left]) == loc->column) {
      found_byte_offset = std::get<0>((*reverse_table)[left]);
    } else if (left + 1 < reverse_table->size() &&
               std::get<1>((*reverse_table)[left + 1]) == loc->line) {
      found_byte_offset = std::get<0>((*reverse_table)[left + 1]);
    }

    v8::Isolate *isolate = loc->translation->isolate_;
    loc->script_id =
        String16::fromInteger(GetScriptId(isolate, script_.Get(isolate)));
    loc->line = func_index;
    loc->column = found_byte_offset;
  }

 private:
  String16 GetFakeScriptUrl(const TransLocation *loc) {
    v8::Isolate *isolate = loc->translation->isolate_;
    String16 script_name = GetScriptName(isolate, script_.Get(isolate));
    return String16::concat("wasm://wasm/", script_name, '/', script_name, '-',
                            String16::fromInteger(loc->line));
  }

  String16 GetFakeScriptId(const TransLocation *loc) {
    return String16::concat(loc->script_id, '-',
                            String16::fromInteger(loc->line));
  }

  int GetFunctionIndexFromFakeScriptId(const String16 &fake_script_id) {
    size_t last_dash_pos = fake_script_id.reverseFind('-');
    DCHECK_GT(fake_script_id.length(), last_dash_pos);
    bool ok = true;
    int func_index = fake_script_id.substring(last_dash_pos + 1).toInteger(&ok);
    DCHECK(ok);
    return func_index;
  }

  const OffsetTable &GetOffsetTable(const TransLocation *loc) {
    int func_index = loc->line;
    auto it = offset_tables_.find(func_index);
    if (it != offset_tables_.end()) return it->second;

    v8::Isolate *isolate = loc->translation->isolate_;
    std::pair<std::string, OffsetTable> disassembly =
        DebugInterface::DisassembleWasmFunction(isolate, script_.Get(isolate),
                                                func_index);

    it = offset_tables_
             .insert(std::make_pair(func_index, std::move(disassembly.second)))
             .first;

    String16 fake_script_id = GetFakeScriptId(loc);
    String16 fake_script_url = GetFakeScriptUrl(loc);
    String16 source(disassembly.first.data(), disassembly.first.length());
    std::unique_ptr<V8DebuggerScript> fake_script(new V8DebuggerScript(
        fake_script_id, std::move(fake_script_url), source));

    loc->translation->AddFakeScript(std::move(fake_script), this,
                                    loc->context_group_id);

    return it->second;
  }

  const OffsetTable *GetReverseTable(int func_index) {
    auto it = reverse_tables_.find(func_index);
    if (it != reverse_tables_.end()) return &it->second;

    // Find offset table, copy and sort it to get reverse table.
    it = offset_tables_.find(func_index);
    if (it == offset_tables_.end()) return nullptr;

    OffsetTable reverse_table = it->second;
    // Order by line, column, then byte offset.
    auto cmp = [](std::tuple<uint32_t, int, int> el1,
                  std::tuple<uint32_t, int, int> el2) {
      if (std::get<1>(el1) != std::get<1>(el2))
        return std::get<1>(el1) < std::get<1>(el2);
      if (std::get<2>(el1) != std::get<2>(el2))
        return std::get<2>(el1) < std::get<2>(el2);
      return std::get<0>(el1) < std::get<0>(el2);
    };
    std::sort(reverse_table.begin(), reverse_table.end(), cmp);

    auto inserted = reverse_tables_.insert(
        std::make_pair(func_index, std::move(reverse_table)));
    DCHECK(inserted.second);
    return &inserted.first->second;
  }

  Global<Object> script_;

  // We assume to only disassemble a subset of the functions, so store them in a
  // map instead of an array.
  std::unordered_map<int, const OffsetTable> offset_tables_;
  std::unordered_map<int, const OffsetTable> reverse_tables_;
};

WasmTranslation::WasmTranslation(v8::Isolate *isolate, V8Debugger *debugger)
    : isolate_(isolate), debugger_(debugger), mode_(Disassemble) {}

WasmTranslation::~WasmTranslation() { Clear(); }

void WasmTranslation::AddScript(Local<Object> script_wrapper) {
  int script_id = GetScriptId(isolate_, script_wrapper);
  DCHECK_EQ(0U, wasm_translators_.count(script_id));
  std::unique_ptr<TranslatorImpl> impl;
  switch (mode_) {
    case Raw:
      impl.reset(new TranslatorImpl::RawTranslator());
      break;
    case Disassemble:
      impl.reset(new TranslatorImpl::DisassemblingTranslator(isolate_,
                                                             script_wrapper));
      break;
  }
  DCHECK(impl);
  wasm_translators_.insert(std::make_pair(script_id, std::move(impl)));
}

void WasmTranslation::Clear() {
  wasm_translators_.clear();
  fake_scripts_.clear();
}

// Translation "forward" (to artificial scripts).
bool WasmTranslation::TranslateWasmScriptLocationToProtocolLocation(
    String16 *script_id, int *line_number, int *column_number,
    int context_group_id) {
  DCHECK(script_id && line_number && column_number);
  bool ok = true;
  int script_id_int = script_id->toInteger(&ok);
  if (!ok) return false;

  auto it = wasm_translators_.find(script_id_int);
  if (it == wasm_translators_.end()) return false;
  TranslatorImpl *translator = it->second.get();

  TranslatorImpl::TransLocation trans_loc(this, std::move(*script_id),
                                          *line_number, *column_number,
                                          context_group_id);
  translator->Translate(&trans_loc);

  *script_id = std::move(trans_loc.script_id);
  *line_number = trans_loc.line;
  *column_number = trans_loc.column;

  return true;
}

// Translation "backward" (from artificial to real scripts).
bool WasmTranslation::TranslateProtocolLocationToWasmScriptLocation(
    String16 *script_id, int *line_number, int *column_number) {
  auto it = fake_scripts_.find(*script_id);
  if (it == fake_scripts_.end()) return false;
  TranslatorImpl *translator = it->second;

  TranslatorImpl::TransLocation trans_loc(this, std::move(*script_id),
                                          *line_number, *column_number, -1);
  translator->TranslateBack(&trans_loc);

  *script_id = std::move(trans_loc.script_id);
  *line_number = trans_loc.line;
  *column_number = trans_loc.column;

  return true;
}

void WasmTranslation::AddFakeScript(
    std::unique_ptr<V8DebuggerScript> fake_script, TranslatorImpl *translator,
    int context_group_id) {
  bool inserted =
      fake_scripts_.insert(std::make_pair(fake_script->scriptId(), translator))
          .second;
  DCHECK(inserted);
  USE(inserted);
  V8DebuggerAgentImpl *agent =
      debugger_->inspector()->enabledDebuggerAgentForGroup(context_group_id);
  agent->didParseSource(std::move(fake_script), true);
}
