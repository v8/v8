// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SHARED_FUNCTION_INFO_INL_H_
#define V8_OBJECTS_SHARED_FUNCTION_INFO_INL_H_

#include "src/heap/heap-inl.h"
#include "src/objects/shared-function-info.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

TYPE_CHECKER(SharedFunctionInfo, SHARED_FUNCTION_INFO_TYPE)
CAST_ACCESSOR(SharedFunctionInfo)
DEFINE_DEOPT_ELEMENT_ACCESSORS(SharedFunctionInfo, Object)

ACCESSORS(SharedFunctionInfo, name, Object, kNameOffset)
ACCESSORS(SharedFunctionInfo, construct_stub, Code, kConstructStubOffset)
ACCESSORS(SharedFunctionInfo, feedback_metadata, FeedbackMetadata,
          kFeedbackMetadataOffset)
SMI_ACCESSORS(SharedFunctionInfo, function_literal_id, kFunctionLiteralIdOffset)
#if V8_SFI_HAS_UNIQUE_ID
SMI_ACCESSORS(SharedFunctionInfo, unique_id, kUniqueIdOffset)
#endif
ACCESSORS(SharedFunctionInfo, instance_class_name, Object,
          kInstanceClassNameOffset)
ACCESSORS(SharedFunctionInfo, function_data, Object, kFunctionDataOffset)
ACCESSORS(SharedFunctionInfo, script, Object, kScriptOffset)
ACCESSORS(SharedFunctionInfo, debug_info, Object, kDebugInfoOffset)
ACCESSORS(SharedFunctionInfo, function_identifier, Object,
          kFunctionIdentifierOffset)

BOOL_ACCESSORS(SharedFunctionInfo, start_position_and_type, is_named_expression,
               kIsNamedExpressionBit)
BOOL_ACCESSORS(SharedFunctionInfo, start_position_and_type, is_toplevel,
               kIsTopLevelBit)

#if V8_HOST_ARCH_32_BIT
SMI_ACCESSORS(SharedFunctionInfo, length, kLengthOffset)
SMI_ACCESSORS(SharedFunctionInfo, internal_formal_parameter_count,
              kFormalParameterCountOffset)
SMI_ACCESSORS(SharedFunctionInfo, expected_nof_properties,
              kExpectedNofPropertiesOffset)
SMI_ACCESSORS(SharedFunctionInfo, start_position_and_type,
              kStartPositionAndTypeOffset)
SMI_ACCESSORS(SharedFunctionInfo, end_position, kEndPositionOffset)
SMI_ACCESSORS(SharedFunctionInfo, function_token_position,
              kFunctionTokenPositionOffset)
SMI_ACCESSORS(SharedFunctionInfo, compiler_hints, kCompilerHintsOffset)
SMI_ACCESSORS(SharedFunctionInfo, opt_count_and_bailout_reason,
              kOptCountAndBailoutReasonOffset)
SMI_ACCESSORS(SharedFunctionInfo, counters, kCountersOffset)
SMI_ACCESSORS(SharedFunctionInfo, ast_node_count, kAstNodeCountOffset)
SMI_ACCESSORS(SharedFunctionInfo, profiler_ticks, kProfilerTicksOffset)

#else

#if V8_TARGET_LITTLE_ENDIAN
#define PSEUDO_SMI_LO_ALIGN 0
#define PSEUDO_SMI_HI_ALIGN kIntSize
#else
#define PSEUDO_SMI_LO_ALIGN kIntSize
#define PSEUDO_SMI_HI_ALIGN 0
#endif

#define PSEUDO_SMI_ACCESSORS_LO(holder, name, offset)                          \
  STATIC_ASSERT(holder::offset % kPointerSize == PSEUDO_SMI_LO_ALIGN);         \
  int holder::name() const {                                                   \
    int value = READ_INT_FIELD(this, offset);                                  \
    DCHECK(kHeapObjectTag == 1);                                               \
    DCHECK((value & kHeapObjectTag) == 0);                                     \
    return value >> 1;                                                         \
  }                                                                            \
  void holder::set_##name(int value) {                                         \
    DCHECK(kHeapObjectTag == 1);                                               \
    DCHECK((value & 0xC0000000) == 0xC0000000 || (value & 0xC0000000) == 0x0); \
    WRITE_INT_FIELD(this, offset, (value << 1) & ~kHeapObjectTag);             \
  }

#define PSEUDO_SMI_ACCESSORS_HI(holder, name, offset)                  \
  STATIC_ASSERT(holder::offset % kPointerSize == PSEUDO_SMI_HI_ALIGN); \
  INT_ACCESSORS(holder, name, offset)

PSEUDO_SMI_ACCESSORS_LO(SharedFunctionInfo, length, kLengthOffset)
PSEUDO_SMI_ACCESSORS_HI(SharedFunctionInfo, internal_formal_parameter_count,
                        kFormalParameterCountOffset)

PSEUDO_SMI_ACCESSORS_LO(SharedFunctionInfo, expected_nof_properties,
                        kExpectedNofPropertiesOffset)

PSEUDO_SMI_ACCESSORS_LO(SharedFunctionInfo, end_position, kEndPositionOffset)
PSEUDO_SMI_ACCESSORS_HI(SharedFunctionInfo, start_position_and_type,
                        kStartPositionAndTypeOffset)

PSEUDO_SMI_ACCESSORS_LO(SharedFunctionInfo, function_token_position,
                        kFunctionTokenPositionOffset)
PSEUDO_SMI_ACCESSORS_HI(SharedFunctionInfo, compiler_hints,
                        kCompilerHintsOffset)

PSEUDO_SMI_ACCESSORS_LO(SharedFunctionInfo, opt_count_and_bailout_reason,
                        kOptCountAndBailoutReasonOffset)
PSEUDO_SMI_ACCESSORS_HI(SharedFunctionInfo, counters, kCountersOffset)

PSEUDO_SMI_ACCESSORS_LO(SharedFunctionInfo, ast_node_count, kAstNodeCountOffset)
PSEUDO_SMI_ACCESSORS_HI(SharedFunctionInfo, profiler_ticks,
                        kProfilerTicksOffset)

#endif

AbstractCode* SharedFunctionInfo::abstract_code() {
  if (HasBytecodeArray()) {
    return AbstractCode::cast(bytecode_array());
  } else {
    return AbstractCode::cast(code());
  }
}

BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, allows_lazy_compilation,
               kAllowLazyCompilation)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, uses_arguments,
               kUsesArguments)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, has_duplicate_parameters,
               kHasDuplicateParameters)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, asm_function, kIsAsmFunction)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, is_declaration,
               kIsDeclaration)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, marked_for_tier_up,
               kMarkedForTierUp)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints,
               has_concurrent_optimization_job, kHasConcurrentOptimizationJob)

BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, needs_home_object,
               kNeedsHomeObject)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, native, kNative)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, force_inline, kForceInline)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, must_use_ignition_turbo,
               kMustUseIgnitionTurbo)
BOOL_ACCESSORS(SharedFunctionInfo, compiler_hints, is_asm_wasm_broken,
               kIsAsmWasmBroken)

BOOL_GETTER(SharedFunctionInfo, compiler_hints, optimization_disabled,
            kOptimizationDisabled)

void SharedFunctionInfo::set_optimization_disabled(bool disable) {
  set_compiler_hints(
      BooleanBit::set(compiler_hints(), kOptimizationDisabled, disable));
}

LanguageMode SharedFunctionInfo::language_mode() {
  STATIC_ASSERT(LANGUAGE_END == 2);
  return construct_language_mode(
      BooleanBit::get(compiler_hints(), kStrictModeFunction));
}

void SharedFunctionInfo::set_language_mode(LanguageMode language_mode) {
  STATIC_ASSERT(LANGUAGE_END == 2);
  // We only allow language mode transitions that set the same language mode
  // again or go up in the chain:
  DCHECK(is_sloppy(this->language_mode()) || is_strict(language_mode));
  int hints = compiler_hints();
  hints = BooleanBit::set(hints, kStrictModeFunction, is_strict(language_mode));
  set_compiler_hints(hints);
}

FunctionKind SharedFunctionInfo::kind() const {
  return FunctionKindBits::decode(compiler_hints());
}

void SharedFunctionInfo::set_kind(FunctionKind kind) {
  DCHECK(IsValidFunctionKind(kind));
  int hints = compiler_hints();
  hints = FunctionKindBits::update(hints, kind);
  set_compiler_hints(hints);
}

BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints,
               name_should_print_as_anonymous, kNameShouldPrintAsAnonymous)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, is_anonymous_expression,
               kIsAnonymousExpression)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, deserialized, kDeserialized)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, has_no_side_effect,
               kHasNoSideEffect)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, computed_has_no_side_effect,
               kComputedHasNoSideEffect)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, debug_is_blackboxed,
               kDebugIsBlackboxed)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, computed_debug_is_blackboxed,
               kComputedDebugIsBlackboxed)
BOOL_ACCESSORS(SharedFunctionInfo, debugger_hints, has_reported_binary_coverage,
               kHasReportedBinaryCoverage)

void SharedFunctionInfo::DontAdaptArguments() {
  DCHECK(code()->kind() == Code::BUILTIN || code()->kind() == Code::STUB);
  set_internal_formal_parameter_count(kDontAdaptArgumentsSentinel);
}

int SharedFunctionInfo::start_position() const {
  return start_position_and_type() >> kStartPositionShift;
}

void SharedFunctionInfo::set_start_position(int start_position) {
  set_start_position_and_type(
      (start_position << kStartPositionShift) |
      (start_position_and_type() & ~kStartPositionMask));
}

Code* SharedFunctionInfo::code() const {
  return Code::cast(READ_FIELD(this, kCodeOffset));
}

void SharedFunctionInfo::set_code(Code* value, WriteBarrierMode mode) {
  DCHECK(value->kind() != Code::OPTIMIZED_FUNCTION);
  // If the SharedFunctionInfo has bytecode we should never mark it for lazy
  // compile, since the bytecode is never flushed.
  DCHECK(value != GetIsolate()->builtins()->builtin(Builtins::kCompileLazy) ||
         !HasBytecodeArray());
  WRITE_FIELD(this, kCodeOffset, value);
  CONDITIONAL_WRITE_BARRIER(value->GetHeap(), this, kCodeOffset, value, mode);
}

void SharedFunctionInfo::ReplaceCode(Code* value) {
#ifdef DEBUG
  Code::VerifyRecompiledCode(code(), value);
#endif  // DEBUG

  set_code(value);
}

bool SharedFunctionInfo::IsInterpreted() const {
  return code()->is_interpreter_trampoline_builtin();
}

bool SharedFunctionInfo::HasBaselineCode() const {
  return code()->kind() == Code::FUNCTION;
}

ScopeInfo* SharedFunctionInfo::scope_info() const {
  return reinterpret_cast<ScopeInfo*>(READ_FIELD(this, kScopeInfoOffset));
}

void SharedFunctionInfo::set_scope_info(ScopeInfo* value,
                                        WriteBarrierMode mode) {
  WRITE_FIELD(this, kScopeInfoOffset, reinterpret_cast<Object*>(value));
  CONDITIONAL_WRITE_BARRIER(GetHeap(), this, kScopeInfoOffset,
                            reinterpret_cast<Object*>(value), mode);
}

ACCESSORS(SharedFunctionInfo, outer_scope_info, HeapObject,
          kOuterScopeInfoOffset)

bool SharedFunctionInfo::is_compiled() const {
  Builtins* builtins = GetIsolate()->builtins();
  DCHECK(code() != builtins->builtin(Builtins::kCompileOptimizedConcurrent));
  DCHECK(code() != builtins->builtin(Builtins::kCompileOptimized));
  return code() != builtins->builtin(Builtins::kCompileLazy);
}

int SharedFunctionInfo::GetLength() const {
  DCHECK(is_compiled());
  DCHECK(HasLength());
  return length();
}

bool SharedFunctionInfo::HasLength() const {
  DCHECK_IMPLIES(length() < 0, length() == kInvalidLength);
  return length() != kInvalidLength;
}

bool SharedFunctionInfo::has_simple_parameters() {
  return scope_info()->HasSimpleParameters();
}

bool SharedFunctionInfo::HasDebugInfo() const {
  bool has_debug_info = !debug_info()->IsSmi();
  DCHECK_EQ(debug_info()->IsStruct(), has_debug_info);
  DCHECK(!has_debug_info || HasDebugCode());
  return has_debug_info;
}

bool SharedFunctionInfo::HasDebugCode() const {
  if (HasBaselineCode()) return code()->has_debug_break_slots();
  return HasBytecodeArray();
}

bool SharedFunctionInfo::IsApiFunction() {
  return function_data()->IsFunctionTemplateInfo();
}

FunctionTemplateInfo* SharedFunctionInfo::get_api_func_data() {
  DCHECK(IsApiFunction());
  return FunctionTemplateInfo::cast(function_data());
}

void SharedFunctionInfo::set_api_func_data(FunctionTemplateInfo* data) {
  DCHECK(function_data()->IsUndefined(GetIsolate()));
  set_function_data(data);
}

bool SharedFunctionInfo::HasBytecodeArray() const {
  return function_data()->IsBytecodeArray();
}

BytecodeArray* SharedFunctionInfo::bytecode_array() const {
  DCHECK(HasBytecodeArray());
  return BytecodeArray::cast(function_data());
}

void SharedFunctionInfo::set_bytecode_array(BytecodeArray* bytecode) {
  DCHECK(function_data()->IsUndefined(GetIsolate()));
  set_function_data(bytecode);
}

void SharedFunctionInfo::ClearBytecodeArray() {
  DCHECK(function_data()->IsUndefined(GetIsolate()) || HasBytecodeArray());
  set_function_data(GetHeap()->undefined_value());
}

bool SharedFunctionInfo::HasAsmWasmData() const {
  return function_data()->IsFixedArray();
}

FixedArray* SharedFunctionInfo::asm_wasm_data() const {
  DCHECK(HasAsmWasmData());
  return FixedArray::cast(function_data());
}

void SharedFunctionInfo::set_asm_wasm_data(FixedArray* data) {
  DCHECK(function_data()->IsUndefined(GetIsolate()) || HasAsmWasmData());
  set_function_data(data);
}

void SharedFunctionInfo::ClearAsmWasmData() {
  DCHECK(function_data()->IsUndefined(GetIsolate()) || HasAsmWasmData());
  set_function_data(GetHeap()->undefined_value());
}

bool SharedFunctionInfo::HasBuiltinFunctionId() {
  return function_identifier()->IsSmi();
}

BuiltinFunctionId SharedFunctionInfo::builtin_function_id() {
  DCHECK(HasBuiltinFunctionId());
  return static_cast<BuiltinFunctionId>(
      Smi::cast(function_identifier())->value());
}

void SharedFunctionInfo::set_builtin_function_id(BuiltinFunctionId id) {
  set_function_identifier(Smi::FromInt(id));
}

bool SharedFunctionInfo::HasInferredName() {
  return function_identifier()->IsString();
}

String* SharedFunctionInfo::inferred_name() {
  if (HasInferredName()) {
    return String::cast(function_identifier());
  }
  Isolate* isolate = GetIsolate();
  DCHECK(function_identifier()->IsUndefined(isolate) || HasBuiltinFunctionId());
  return isolate->heap()->empty_string();
}

void SharedFunctionInfo::set_inferred_name(String* inferred_name) {
  DCHECK(function_identifier()->IsUndefined(GetIsolate()) || HasInferredName());
  set_function_identifier(inferred_name);
}

int SharedFunctionInfo::ic_age() { return ICAgeBits::decode(counters()); }

void SharedFunctionInfo::set_ic_age(int ic_age) {
  set_counters(ICAgeBits::update(counters(), ic_age));
}

int SharedFunctionInfo::deopt_count() {
  return DeoptCountBits::decode(counters());
}

void SharedFunctionInfo::set_deopt_count(int deopt_count) {
  set_counters(DeoptCountBits::update(counters(), deopt_count));
}

void SharedFunctionInfo::increment_deopt_count() {
  int value = counters();
  int deopt_count = DeoptCountBits::decode(value);
  // Saturate the deopt count when incrementing, rather than overflowing.
  if (deopt_count < DeoptCountBits::kMax) {
    set_counters(DeoptCountBits::update(value, deopt_count + 1));
  }
}

int SharedFunctionInfo::opt_reenable_tries() {
  return OptReenableTriesBits::decode(counters());
}

void SharedFunctionInfo::set_opt_reenable_tries(int tries) {
  set_counters(OptReenableTriesBits::update(counters(), tries));
}

int SharedFunctionInfo::opt_count() {
  return OptCountBits::decode(opt_count_and_bailout_reason());
}

void SharedFunctionInfo::set_opt_count(int opt_count) {
  set_opt_count_and_bailout_reason(
      OptCountBits::update(opt_count_and_bailout_reason(), opt_count));
}

BailoutReason SharedFunctionInfo::disable_optimization_reason() {
  return static_cast<BailoutReason>(
      DisabledOptimizationReasonBits::decode(opt_count_and_bailout_reason()));
}

bool SharedFunctionInfo::has_deoptimization_support() {
  Code* code = this->code();
  return code->kind() == Code::FUNCTION && code->has_deoptimization_support();
}

void SharedFunctionInfo::TryReenableOptimization() {
  int tries = opt_reenable_tries();
  set_opt_reenable_tries((tries + 1) & OptReenableTriesBits::kMax);
  // We reenable optimization whenever the number of tries is a large
  // enough power of 2.
  if (tries >= 16 && (((tries - 1) & tries) == 0)) {
    set_optimization_disabled(false);
    set_deopt_count(0);
  }
}

void SharedFunctionInfo::set_disable_optimization_reason(BailoutReason reason) {
  set_opt_count_and_bailout_reason(DisabledOptimizationReasonBits::update(
      opt_count_and_bailout_reason(), reason));
}

bool SharedFunctionInfo::IsUserJavaScript() {
  Object* script_obj = script();
  if (script_obj->IsUndefined(GetIsolate())) return false;
  Script* script = Script::cast(script_obj);
  return script->IsUserJavaScript();
}

bool SharedFunctionInfo::IsSubjectToDebugging() {
  return IsUserJavaScript() && !HasAsmWasmData();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SHARED_FUNCTION_INFO_INL_H_
