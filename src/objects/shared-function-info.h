// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SHARED_FUNCTION_INFO_H_
#define V8_OBJECTS_SHARED_FUNCTION_INFO_H_

#include "src/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// SharedFunctionInfo describes the JSFunction information that can be
// shared by multiple instances of the function.
class SharedFunctionInfo : public HeapObject {
 public:
  // [name]: Function name.
  DECL_ACCESSORS(name, Object)

  // [code]: Function code.
  DECL_ACCESSORS(code, Code)

  // Get the abstract code associated with the function, which will either be
  // a Code object or a BytecodeArray.
  inline AbstractCode* abstract_code();

  // Tells whether or not this shared function info is interpreted.
  //
  // Note: function->IsInterpreted() does not necessarily return the same value
  // as function->shared()->IsInterpreted() because the closure might have been
  // optimized.
  inline bool IsInterpreted() const;

  inline void ReplaceCode(Code* code);
  inline bool HasBaselineCode() const;

  // Set up the link between shared function info and the script. The shared
  // function info is added to the list on the script.
  V8_EXPORT_PRIVATE static void SetScript(Handle<SharedFunctionInfo> shared,
                                          Handle<Object> script_object);

  // Layout description of the optimized code map.
  static const int kEntriesStart = 0;
  static const int kContextOffset = 0;
  static const int kCachedCodeOffset = 1;
  static const int kEntryLength = 2;
  static const int kInitialLength = kEntriesStart + kEntryLength;

  static const int kNotFound = -1;
  static const int kInvalidLength = -1;

  // Helpers for assembly code that does a backwards walk of the optimized code
  // map.
  static const int kOffsetToPreviousContext =
      FixedArray::kHeaderSize + kPointerSize * (kContextOffset - kEntryLength);
  static const int kOffsetToPreviousCachedCode =
      FixedArray::kHeaderSize +
      kPointerSize * (kCachedCodeOffset - kEntryLength);

  // [scope_info]: Scope info.
  DECL_ACCESSORS(scope_info, ScopeInfo)

  // The outer scope info for the purpose of parsing this function, or the hole
  // value if it isn't yet known.
  DECL_ACCESSORS(outer_scope_info, HeapObject)

  // [construct stub]: Code stub for constructing instances of this function.
  DECL_ACCESSORS(construct_stub, Code)

  // Sets the given code as the construct stub, and marks builtin code objects
  // as a construct stub.
  void SetConstructStub(Code* code);

  // Returns if this function has been compiled to native code yet.
  inline bool is_compiled() const;

  // [length]: The function length - usually the number of declared parameters.
  // Use up to 2^30 parameters. The value is only reliable when the function has
  // been compiled.
  inline int GetLength() const;
  inline bool HasLength() const;
  inline void set_length(int value);

  // [internal formal parameter count]: The declared number of parameters.
  // For subclass constructors, also includes new.target.
  // The size of function's frame is internal_formal_parameter_count + 1.
  inline int internal_formal_parameter_count() const;
  inline void set_internal_formal_parameter_count(int value);

  // Set the formal parameter count so the function code will be
  // called without using argument adaptor frames.
  inline void DontAdaptArguments();

  // [expected_nof_properties]: Expected number of properties for the
  // function. The value is only reliable when the function has been compiled.
  inline int expected_nof_properties() const;
  inline void set_expected_nof_properties(int value);

  // [feedback_metadata] - describes ast node feedback from full-codegen and
  // (increasingly) from crankshafted code where sufficient feedback isn't
  // available.
  DECL_ACCESSORS(feedback_metadata, FeedbackMetadata)

  // [function_literal_id] - uniquely identifies the FunctionLiteral this
  // SharedFunctionInfo represents within its script, or -1 if this
  // SharedFunctionInfo object doesn't correspond to a parsed FunctionLiteral.
  inline int function_literal_id() const;
  inline void set_function_literal_id(int value);

#if V8_SFI_HAS_UNIQUE_ID
  // [unique_id] - For --trace-maps purposes, an identifier that's persistent
  // even if the GC moves this SharedFunctionInfo.
  inline int unique_id() const;
  inline void set_unique_id(int value);
#endif

  // [instance class name]: class name for instances.
  DECL_ACCESSORS(instance_class_name, Object)

  // [function data]: This field holds some additional data for function.
  // Currently it has one of:
  //  - a FunctionTemplateInfo to make benefit the API [IsApiFunction()].
  //  - a BytecodeArray for the interpreter [HasBytecodeArray()].
  //  - a FixedArray with Asm->Wasm conversion [HasAsmWasmData()].
  DECL_ACCESSORS(function_data, Object)

  inline bool IsApiFunction();
  inline FunctionTemplateInfo* get_api_func_data();
  inline void set_api_func_data(FunctionTemplateInfo* data);
  inline bool HasBytecodeArray() const;
  inline BytecodeArray* bytecode_array() const;
  inline void set_bytecode_array(BytecodeArray* bytecode);
  inline void ClearBytecodeArray();
  inline bool HasAsmWasmData() const;
  inline FixedArray* asm_wasm_data() const;
  inline void set_asm_wasm_data(FixedArray* data);
  inline void ClearAsmWasmData();

  // [function identifier]: This field holds an additional identifier for the
  // function.
  //  - a Smi identifying a builtin function [HasBuiltinFunctionId()].
  //  - a String identifying the function's inferred name [HasInferredName()].
  // The inferred_name is inferred from variable or property
  // assignment of this function. It is used to facilitate debugging and
  // profiling of JavaScript code written in OO style, where almost
  // all functions are anonymous but are assigned to object
  // properties.
  DECL_ACCESSORS(function_identifier, Object)

  inline bool HasBuiltinFunctionId();
  inline BuiltinFunctionId builtin_function_id();
  inline void set_builtin_function_id(BuiltinFunctionId id);
  inline bool HasInferredName();
  inline String* inferred_name();
  inline void set_inferred_name(String* inferred_name);

  // [script]: Script from which the function originates.
  DECL_ACCESSORS(script, Object)

  // [start_position_and_type]: Field used to store both the source code
  // position, whether or not the function is a function expression,
  // and whether or not the function is a toplevel function. The two
  // least significants bit indicates whether the function is an
  // expression and the rest contains the source code position.
  inline int start_position_and_type() const;
  inline void set_start_position_and_type(int value);

  // The function is subject to debugging if a debug info is attached.
  inline bool HasDebugInfo() const;
  DebugInfo* GetDebugInfo() const;

  // A function has debug code if the compiled code has debug break slots.
  inline bool HasDebugCode() const;

  // [debug info]: Debug information.
  DECL_ACCESSORS(debug_info, Object)

  // Bit field containing various information collected for debugging.
  // This field is either stored on the kDebugInfo slot or inside the
  // debug info struct.
  int debugger_hints() const;
  void set_debugger_hints(int value);

  // Indicates that the function was created by the Function function.
  // Though it's anonymous, toString should treat it as if it had the name
  // "anonymous".  We don't set the name itself so that the system does not
  // see a binding for it.
  DECL_BOOLEAN_ACCESSORS(name_should_print_as_anonymous)

  // Indicates that the function is either an anonymous expression
  // or an arrow function (the name field can be set through the API,
  // which does not change this flag).
  DECL_BOOLEAN_ACCESSORS(is_anonymous_expression)

  // Indicates that the the shared function info is deserialized from cache.
  DECL_BOOLEAN_ACCESSORS(deserialized)

  // Indicates that the function cannot cause side-effects.
  DECL_BOOLEAN_ACCESSORS(has_no_side_effect)

  // Indicates that |has_no_side_effect| has been computed and set.
  DECL_BOOLEAN_ACCESSORS(computed_has_no_side_effect)

  // Indicates that the function should be skipped during stepping.
  DECL_BOOLEAN_ACCESSORS(debug_is_blackboxed)

  // Indicates that |debug_is_blackboxed| has been computed and set.
  DECL_BOOLEAN_ACCESSORS(computed_debug_is_blackboxed)

  // Indicates that the function has been reported for binary code coverage.
  DECL_BOOLEAN_ACCESSORS(has_reported_binary_coverage)

  // The function's name if it is non-empty, otherwise the inferred name.
  String* DebugName();

  // The function cannot cause any side effects.
  bool HasNoSideEffect();

  // Used for flags such as --hydrogen-filter.
  bool PassesFilter(const char* raw_filter);

  // Position of the 'function' token in the script source.
  inline int function_token_position() const;
  inline void set_function_token_position(int function_token_position);

  // Position of this function in the script source.
  inline int start_position() const;
  inline void set_start_position(int start_position);

  // End position of this function in the script source.
  inline int end_position() const;
  inline void set_end_position(int end_position);

  // Is this function a named function expression in the source code.
  DECL_BOOLEAN_ACCESSORS(is_named_expression)

  // Is this function a top-level function (scripts, evals).
  DECL_BOOLEAN_ACCESSORS(is_toplevel)

  // Bit field containing various information collected by the compiler to
  // drive optimization.
  inline int compiler_hints() const;
  inline void set_compiler_hints(int value);

  inline int ast_node_count() const;
  inline void set_ast_node_count(int count);

  inline int profiler_ticks() const;
  inline void set_profiler_ticks(int ticks);

  // Inline cache age is used to infer whether the function survived a context
  // disposal or not. In the former case we reset the opt_count.
  inline int ic_age();
  inline void set_ic_age(int age);

  // Indicates if this function can be lazy compiled.
  DECL_BOOLEAN_ACCESSORS(allows_lazy_compilation)

  // Indicates whether optimizations have been disabled for this
  // shared function info. If a function is repeatedly optimized or if
  // we cannot optimize the function we disable optimization to avoid
  // spending time attempting to optimize it again.
  DECL_BOOLEAN_ACCESSORS(optimization_disabled)

  // Indicates the language mode.
  inline LanguageMode language_mode();
  inline void set_language_mode(LanguageMode language_mode);

  // False if the function definitely does not allocate an arguments object.
  DECL_BOOLEAN_ACCESSORS(uses_arguments)

  // Indicates that this function uses a super property (or an eval that may
  // use a super property).
  // This is needed to set up the [[HomeObject]] on the function instance.
  DECL_BOOLEAN_ACCESSORS(needs_home_object)

  // True if the function has any duplicated parameter names.
  DECL_BOOLEAN_ACCESSORS(has_duplicate_parameters)

  // Indicates whether the function is a native function.
  // These needs special treatment in .call and .apply since
  // null passed as the receiver should not be translated to the
  // global object.
  DECL_BOOLEAN_ACCESSORS(native)

  // Indicate that this function should always be inlined in optimized code.
  DECL_BOOLEAN_ACCESSORS(force_inline)

  // Indicates that code for this function must be compiled through the
  // Ignition / TurboFan pipeline, and is unsupported by
  // FullCodegen / Crankshaft.
  DECL_BOOLEAN_ACCESSORS(must_use_ignition_turbo)

  // Indicates that this function is an asm function.
  DECL_BOOLEAN_ACCESSORS(asm_function)

  // Whether this function was created from a FunctionDeclaration.
  DECL_BOOLEAN_ACCESSORS(is_declaration)

  // Whether this function was marked to be tiered up.
  DECL_BOOLEAN_ACCESSORS(marked_for_tier_up)

  // Whether this function has a concurrent compilation job running.
  DECL_BOOLEAN_ACCESSORS(has_concurrent_optimization_job)

  // Indicates that asm->wasm conversion failed and should not be re-attempted.
  DECL_BOOLEAN_ACCESSORS(is_asm_wasm_broken)

  inline FunctionKind kind() const;
  inline void set_kind(FunctionKind kind);

  // Indicates whether or not the code in the shared function support
  // deoptimization.
  inline bool has_deoptimization_support();

  // Enable deoptimization support through recompiled code.
  void EnableDeoptimizationSupport(Code* recompiled);

  // Disable (further) attempted optimization of all functions sharing this
  // shared function info.
  void DisableOptimization(BailoutReason reason);

  inline BailoutReason disable_optimization_reason();

  // Lookup the bailout ID and DCHECK that it exists in the non-optimized
  // code, returns whether it asserted (i.e., always true if assertions are
  // disabled).
  bool VerifyBailoutId(BailoutId id);

  // [source code]: Source code for the function.
  bool HasSourceCode() const;
  Handle<Object> GetSourceCode();
  Handle<Object> GetSourceCodeHarmony();

  // Number of times the function was optimized.
  inline int opt_count();
  inline void set_opt_count(int opt_count);

  // Number of times the function was deoptimized.
  inline void set_deopt_count(int value);
  inline int deopt_count();
  inline void increment_deopt_count();

  // Number of time we tried to re-enable optimization after it
  // was disabled due to high number of deoptimizations.
  inline void set_opt_reenable_tries(int value);
  inline int opt_reenable_tries();

  inline void TryReenableOptimization();

  // Stores deopt_count, opt_reenable_tries and ic_age as bit-fields.
  inline void set_counters(int value);
  inline int counters() const;

  // Stores opt_count and bailout_reason as bit-fields.
  inline void set_opt_count_and_bailout_reason(int value);
  inline int opt_count_and_bailout_reason() const;

  inline void set_disable_optimization_reason(BailoutReason reason);

  // Tells whether this function should be subject to debugging.
  inline bool IsSubjectToDebugging();

  // Whether this function is defined in user-provided JavaScript code.
  inline bool IsUserJavaScript();

  // Check whether or not this function is inlineable.
  bool IsInlineable();

  // Source size of this function.
  int SourceSize();

  // Returns `false` if formal parameters include rest parameters, optional
  // parameters, or destructuring parameters.
  // TODO(caitp): make this a flag set during parsing
  inline bool has_simple_parameters();

  // Initialize a SharedFunctionInfo from a parsed function literal.
  static void InitFromFunctionLiteral(Handle<SharedFunctionInfo> shared_info,
                                      FunctionLiteral* lit);

  // Sets the expected number of properties based on estimate from parser.
  void SetExpectedNofPropertiesFromEstimate(FunctionLiteral* literal);

  // Dispatched behavior.
  DECLARE_PRINTER(SharedFunctionInfo)
  DECLARE_VERIFIER(SharedFunctionInfo)

  void ResetForNewContext(int new_ic_age);

  // Iterate over all shared function infos in a given script.
  class ScriptIterator {
   public:
    explicit ScriptIterator(Handle<Script> script);
    ScriptIterator(Isolate* isolate, Handle<FixedArray> shared_function_infos);
    SharedFunctionInfo* Next();

    // Reset the iterator to run on |script|.
    void Reset(Handle<Script> script);

   private:
    Isolate* isolate_;
    Handle<FixedArray> shared_function_infos_;
    int index_;
    DISALLOW_COPY_AND_ASSIGN(ScriptIterator);
  };

  // Iterate over all shared function infos on the heap.
  class GlobalIterator {
   public:
    explicit GlobalIterator(Isolate* isolate);
    SharedFunctionInfo* Next();

   private:
    Script::Iterator script_iterator_;
    WeakFixedArray::Iterator noscript_sfi_iterator_;
    SharedFunctionInfo::ScriptIterator sfi_iterator_;
    DisallowHeapAllocation no_gc_;
    DISALLOW_COPY_AND_ASSIGN(GlobalIterator);
  };

  DECLARE_CAST(SharedFunctionInfo)

  // Constants.
  static const int kDontAdaptArgumentsSentinel = -1;

  // Layout description.
  // Pointer fields.
  static const int kCodeOffset = HeapObject::kHeaderSize;
  static const int kNameOffset = kCodeOffset + kPointerSize;
  static const int kScopeInfoOffset = kNameOffset + kPointerSize;
  static const int kOuterScopeInfoOffset = kScopeInfoOffset + kPointerSize;
  static const int kConstructStubOffset = kOuterScopeInfoOffset + kPointerSize;
  static const int kInstanceClassNameOffset =
      kConstructStubOffset + kPointerSize;
  static const int kFunctionDataOffset =
      kInstanceClassNameOffset + kPointerSize;
  static const int kScriptOffset = kFunctionDataOffset + kPointerSize;
  static const int kDebugInfoOffset = kScriptOffset + kPointerSize;
  static const int kFunctionIdentifierOffset = kDebugInfoOffset + kPointerSize;
  static const int kFeedbackMetadataOffset =
      kFunctionIdentifierOffset + kPointerSize;
  static const int kFunctionLiteralIdOffset =
      kFeedbackMetadataOffset + kPointerSize;
#if V8_SFI_HAS_UNIQUE_ID
  static const int kUniqueIdOffset = kFunctionLiteralIdOffset + kPointerSize;
  static const int kLastPointerFieldOffset = kUniqueIdOffset;
#else
  // Just to not break the postmortrem support with conditional offsets
  static const int kUniqueIdOffset = kFunctionLiteralIdOffset;
  static const int kLastPointerFieldOffset = kFunctionLiteralIdOffset;
#endif

#if V8_HOST_ARCH_32_BIT
  // Smi fields.
  static const int kLengthOffset = kLastPointerFieldOffset + kPointerSize;
  static const int kFormalParameterCountOffset = kLengthOffset + kPointerSize;
  static const int kExpectedNofPropertiesOffset =
      kFormalParameterCountOffset + kPointerSize;
  static const int kNumLiteralsOffset =
      kExpectedNofPropertiesOffset + kPointerSize;
  static const int kStartPositionAndTypeOffset =
      kNumLiteralsOffset + kPointerSize;
  static const int kEndPositionOffset =
      kStartPositionAndTypeOffset + kPointerSize;
  static const int kFunctionTokenPositionOffset =
      kEndPositionOffset + kPointerSize;
  static const int kCompilerHintsOffset =
      kFunctionTokenPositionOffset + kPointerSize;
  static const int kOptCountAndBailoutReasonOffset =
      kCompilerHintsOffset + kPointerSize;
  static const int kCountersOffset =
      kOptCountAndBailoutReasonOffset + kPointerSize;
  static const int kAstNodeCountOffset = kCountersOffset + kPointerSize;
  static const int kProfilerTicksOffset = kAstNodeCountOffset + kPointerSize;

  // Total size.
  static const int kSize = kProfilerTicksOffset + kPointerSize;
#else
// The only reason to use smi fields instead of int fields is to allow
// iteration without maps decoding during garbage collections.
// To avoid wasting space on 64-bit architectures we use the following trick:
// we group integer fields into pairs
// The least significant integer in each pair is shifted left by 1.  By doing
// this we guarantee that LSB of each kPointerSize aligned word is not set and
// thus this word cannot be treated as pointer to HeapObject during old space
// traversal.
#if V8_TARGET_LITTLE_ENDIAN
  static const int kLengthOffset = kLastPointerFieldOffset + kPointerSize;
  static const int kFormalParameterCountOffset = kLengthOffset + kIntSize;

  static const int kExpectedNofPropertiesOffset =
      kFormalParameterCountOffset + kIntSize;
  static const int kNumLiteralsOffset = kExpectedNofPropertiesOffset + kIntSize;

  static const int kEndPositionOffset = kNumLiteralsOffset + kIntSize;
  static const int kStartPositionAndTypeOffset = kEndPositionOffset + kIntSize;

  static const int kFunctionTokenPositionOffset =
      kStartPositionAndTypeOffset + kIntSize;
  static const int kCompilerHintsOffset =
      kFunctionTokenPositionOffset + kIntSize;

  static const int kOptCountAndBailoutReasonOffset =
      kCompilerHintsOffset + kIntSize;
  static const int kCountersOffset = kOptCountAndBailoutReasonOffset + kIntSize;

  static const int kAstNodeCountOffset = kCountersOffset + kIntSize;
  static const int kProfilerTicksOffset = kAstNodeCountOffset + kIntSize;

  // Total size.
  static const int kSize = kProfilerTicksOffset + kIntSize;

#elif V8_TARGET_BIG_ENDIAN
  static const int kFormalParameterCountOffset =
      kLastPointerFieldOffset + kPointerSize;
  static const int kLengthOffset = kFormalParameterCountOffset + kIntSize;

  static const int kNumLiteralsOffset = kLengthOffset + kIntSize;
  static const int kExpectedNofPropertiesOffset = kNumLiteralsOffset + kIntSize;

  static const int kStartPositionAndTypeOffset =
      kExpectedNofPropertiesOffset + kIntSize;
  static const int kEndPositionOffset = kStartPositionAndTypeOffset + kIntSize;

  static const int kCompilerHintsOffset = kEndPositionOffset + kIntSize;
  static const int kFunctionTokenPositionOffset =
      kCompilerHintsOffset + kIntSize;

  static const int kCountersOffset = kFunctionTokenPositionOffset + kIntSize;
  static const int kOptCountAndBailoutReasonOffset = kCountersOffset + kIntSize;

  static const int kProfilerTicksOffset =
      kOptCountAndBailoutReasonOffset + kIntSize;
  static const int kAstNodeCountOffset = kProfilerTicksOffset + kIntSize;

  // Total size.
  static const int kSize = kAstNodeCountOffset + kIntSize;

#else
#error Unknown byte ordering
#endif  // Big endian
#endif  // 64-bit

  static const int kAlignedSize = POINTER_SIZE_ALIGN(kSize);

  typedef FixedBodyDescriptor<kCodeOffset,
                              kLastPointerFieldOffset + kPointerSize, kSize>
      BodyDescriptor;
  typedef FixedBodyDescriptor<kNameOffset,
                              kLastPointerFieldOffset + kPointerSize, kSize>
      BodyDescriptorWeakCode;

  // Bit positions in start_position_and_type.
  // The source code start position is in the 30 most significant bits of
  // the start_position_and_type field.
  static const int kIsNamedExpressionBit = 0;
  static const int kIsTopLevelBit = 1;
  static const int kStartPositionShift = 2;
  static const int kStartPositionMask = ~((1 << kStartPositionShift) - 1);

  // Bit positions in compiler_hints.
  enum CompilerHints {
    // byte 0
    kAllowLazyCompilation,
    kMarkedForTierUp,
    kOptimizationDisabled,
    kHasDuplicateParameters,
    kNative,
    kStrictModeFunction,
    kUsesArguments,
    kNeedsHomeObject,
    // byte 1
    kForceInline,
    kIsAsmFunction,
    kMustUseIgnitionTurbo,
    kIsDeclaration,
    kIsAsmWasmBroken,
    kHasConcurrentOptimizationJob,

    kUnused1,  // Unused fields.
    kUnused2,

    // byte 2
    kFunctionKind,
    // rest of byte 2 and first two bits of byte 3 are used by FunctionKind
    // byte 3
    kCompilerHintsCount = kFunctionKind + 10,  // Pseudo entry
  };

  // Bit positions in debugger_hints.
  enum DebuggerHints {
    kIsAnonymousExpression,
    kNameShouldPrintAsAnonymous,
    kDeserialized,
    kHasNoSideEffect,
    kComputedHasNoSideEffect,
    kDebugIsBlackboxed,
    kComputedDebugIsBlackboxed,
    kHasReportedBinaryCoverage
  };

  // kFunctionKind has to be byte-aligned
  STATIC_ASSERT((kFunctionKind % kBitsPerByte) == 0);

  class FunctionKindBits : public BitField<FunctionKind, kFunctionKind, 10> {};

  class DeoptCountBits : public BitField<int, 0, 4> {};
  class OptReenableTriesBits : public BitField<int, 4, 18> {};
  class ICAgeBits : public BitField<int, 22, 8> {};

  class OptCountBits : public BitField<int, 0, 22> {};
  class DisabledOptimizationReasonBits : public BitField<int, 22, 8> {};

 private:
  FRIEND_TEST(PreParserTest, LazyFunctionLength);

  inline int length() const;

#if V8_HOST_ARCH_32_BIT
  // On 32 bit platforms, compiler hints is a smi.
  static const int kCompilerHintsSmiTagSize = kSmiTagSize;
  static const int kCompilerHintsSize = kPointerSize;
#else
  // On 64 bit platforms, compiler hints is not a smi, see comment above.
  static const int kCompilerHintsSmiTagSize = 0;
  static const int kCompilerHintsSize = kIntSize;
#endif

  STATIC_ASSERT(SharedFunctionInfo::kCompilerHintsCount +
                    SharedFunctionInfo::kCompilerHintsSmiTagSize <=
                SharedFunctionInfo::kCompilerHintsSize * kBitsPerByte);

 public:
  // Constants for optimizing codegen for strict mode function and
  // native tests when using integer-width instructions.
  static const int kStrictModeBit =
      kStrictModeFunction + kCompilerHintsSmiTagSize;
  static const int kNativeBit = kNative + kCompilerHintsSmiTagSize;
  static const int kHasDuplicateParametersBit =
      kHasDuplicateParameters + kCompilerHintsSmiTagSize;

  static const int kFunctionKindShift =
      kFunctionKind + kCompilerHintsSmiTagSize;
  static const int kAllFunctionKindBitsMask = FunctionKindBits::kMask
                                              << kCompilerHintsSmiTagSize;

  static const int kMarkedForTierUpBit =
      kMarkedForTierUp + kCompilerHintsSmiTagSize;

  // Constants for optimizing codegen for strict mode function and
  // native tests.
  // Allows to use byte-width instructions.
  static const int kStrictModeBitWithinByte = kStrictModeBit % kBitsPerByte;
  static const int kNativeBitWithinByte = kNativeBit % kBitsPerByte;
  static const int kHasDuplicateParametersBitWithinByte =
      kHasDuplicateParametersBit % kBitsPerByte;

  static const int kClassConstructorBitsWithinByte =
      FunctionKind::kClassConstructor << kCompilerHintsSmiTagSize;
  STATIC_ASSERT(kClassConstructorBitsWithinByte < (1 << kBitsPerByte));

  static const int kDerivedConstructorBitsWithinByte =
      FunctionKind::kDerivedConstructor << kCompilerHintsSmiTagSize;
  STATIC_ASSERT(kDerivedConstructorBitsWithinByte < (1 << kBitsPerByte));

  static const int kMarkedForTierUpBitWithinByte =
      kMarkedForTierUpBit % kBitsPerByte;

#if defined(V8_TARGET_LITTLE_ENDIAN)
#define BYTE_OFFSET(compiler_hint) \
  kCompilerHintsOffset +           \
      (compiler_hint + kCompilerHintsSmiTagSize) / kBitsPerByte
#elif defined(V8_TARGET_BIG_ENDIAN)
#define BYTE_OFFSET(compiler_hint)                  \
  kCompilerHintsOffset + (kCompilerHintsSize - 1) - \
      ((compiler_hint + kCompilerHintsSmiTagSize) / kBitsPerByte)
#else
#error Unknown byte ordering
#endif
  static const int kStrictModeByteOffset = BYTE_OFFSET(kStrictModeFunction);
  static const int kNativeByteOffset = BYTE_OFFSET(kNative);
  static const int kFunctionKindByteOffset = BYTE_OFFSET(kFunctionKind);
  static const int kHasDuplicateParametersByteOffset =
      BYTE_OFFSET(kHasDuplicateParameters);
  static const int kMarkedForTierUpByteOffset = BYTE_OFFSET(kMarkedForTierUp);
#undef BYTE_OFFSET

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(SharedFunctionInfo);
};

// Result of searching in an optimized code map of a SharedFunctionInfo. Note
// that both {code} and {vector} can be NULL to pass search result status.
struct CodeAndVector {
  Code* code;              // Cached optimized code.
  FeedbackVector* vector;  // Cached feedback vector.
};

// Printing support.
struct SourceCodeOf {
  explicit SourceCodeOf(SharedFunctionInfo* v, int max = -1)
      : value(v), max_length(max) {}
  const SharedFunctionInfo* value;
  int max_length;
};

std::ostream& operator<<(std::ostream& os, const SourceCodeOf& v);

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SHARED_FUNCTION_INFO_H_
