// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXTERNAL_REFERENCE_H_
#define V8_EXTERNAL_REFERENCE_H_

#include "src/globals.h"
#include "src/isolate.h"
#include "src/runtime/runtime.h"

namespace v8 {

class ApiFunction;

namespace internal {

class Isolate;
class SCTableReference;
class StatsCounter;

//------------------------------------------------------------------------------
// External references

// An ExternalReference represents a C++ address used in the generated
// code. All references to C++ functions and variables must be encapsulated in
// an ExternalReference instance. This is done in order to track the origin of
// all external references in the code so that they can be bound to the correct
// addresses when deserializing a heap.
class ExternalReference BASE_EMBEDDED {
 public:
  // Used in the simulator to support different native api calls.
  enum Type {
    // Builtin call.
    // Object* f(v8::internal::Arguments).
    BUILTIN_CALL,  // default

    // Builtin call returning object pair.
    // ObjectPair f(v8::internal::Arguments).
    BUILTIN_CALL_PAIR,

    // Builtin that takes float arguments and returns an int.
    // int f(double, double).
    BUILTIN_COMPARE_CALL,

    // Builtin call that returns floating point.
    // double f(double, double).
    BUILTIN_FP_FP_CALL,

    // Builtin call that returns floating point.
    // double f(double).
    BUILTIN_FP_CALL,

    // Builtin call that returns floating point.
    // double f(double, int).
    BUILTIN_FP_INT_CALL,

    // Direct call to API function callback.
    // void f(v8::FunctionCallbackInfo&)
    DIRECT_API_CALL,

    // Call to function callback via InvokeFunctionCallback.
    // void f(v8::FunctionCallbackInfo&, v8::FunctionCallback)
    PROFILING_API_CALL,

    // Direct call to accessor getter callback.
    // void f(Local<Name> property, PropertyCallbackInfo& info)
    DIRECT_GETTER_CALL,

    // Call to accessor getter callback via InvokeAccessorGetterCallback.
    // void f(Local<Name> property, PropertyCallbackInfo& info,
    //     AccessorNameGetterCallback callback)
    PROFILING_GETTER_CALL
  };

  static void SetUp();

  typedef void* ExternalReferenceRedirector(void* original, Type type);

  ExternalReference() : address_(nullptr) {}

  ExternalReference(Address address, Isolate* isolate);

  ExternalReference(ApiFunction* ptr, Type type, Isolate* isolate);

  ExternalReference(Runtime::FunctionId id, Isolate* isolate);

  ExternalReference(const Runtime::Function* f, Isolate* isolate);

  explicit ExternalReference(StatsCounter* counter);

  ExternalReference(IsolateAddressId id, Isolate* isolate);

  explicit ExternalReference(const SCTableReference& table_ref);

  // Isolate as an external reference.
  static ExternalReference isolate_address(Isolate* isolate);

  // The builtins table as an external reference, used by lazy deserialization.
  static ExternalReference builtins_address(Isolate* isolate);

  static ExternalReference handle_scope_implementer_address(Isolate* isolate);
  static ExternalReference pending_microtask_count_address(Isolate* isolate);

  // One-of-a-kind references. These references are not part of a general
  // pattern. This means that they have to be added to the
  // ExternalReferenceTable in serialize.cc manually.

  static ExternalReference interpreter_dispatch_table_address(Isolate* isolate);
  static ExternalReference interpreter_dispatch_counters(Isolate* isolate);
  static ExternalReference bytecode_size_table_address(Isolate* isolate);

  static ExternalReference incremental_marking_record_write_function(
      Isolate* isolate);
  static ExternalReference store_buffer_overflow_function(Isolate* isolate);
  static ExternalReference delete_handle_scope_extensions(Isolate* isolate);

  static ExternalReference get_date_field_function(Isolate* isolate);
  static ExternalReference date_cache_stamp(Isolate* isolate);

  // Deoptimization support.
  static ExternalReference new_deoptimizer_function(Isolate* isolate);
  static ExternalReference compute_output_frames_function(Isolate* isolate);

  static ExternalReference wasm_f32_trunc(Isolate* isolate);
  static ExternalReference wasm_f32_floor(Isolate* isolate);
  static ExternalReference wasm_f32_ceil(Isolate* isolate);
  static ExternalReference wasm_f32_nearest_int(Isolate* isolate);
  static ExternalReference wasm_f64_trunc(Isolate* isolate);
  static ExternalReference wasm_f64_floor(Isolate* isolate);
  static ExternalReference wasm_f64_ceil(Isolate* isolate);
  static ExternalReference wasm_f64_nearest_int(Isolate* isolate);
  static ExternalReference wasm_int64_to_float32(Isolate* isolate);
  static ExternalReference wasm_uint64_to_float32(Isolate* isolate);
  static ExternalReference wasm_int64_to_float64(Isolate* isolate);
  static ExternalReference wasm_uint64_to_float64(Isolate* isolate);
  static ExternalReference wasm_float32_to_int64(Isolate* isolate);
  static ExternalReference wasm_float32_to_uint64(Isolate* isolate);
  static ExternalReference wasm_float64_to_int64(Isolate* isolate);
  static ExternalReference wasm_float64_to_uint64(Isolate* isolate);
  static ExternalReference wasm_int64_div(Isolate* isolate);
  static ExternalReference wasm_int64_mod(Isolate* isolate);
  static ExternalReference wasm_uint64_div(Isolate* isolate);
  static ExternalReference wasm_uint64_mod(Isolate* isolate);
  static ExternalReference wasm_word32_ctz(Isolate* isolate);
  static ExternalReference wasm_word64_ctz(Isolate* isolate);
  static ExternalReference wasm_word32_popcnt(Isolate* isolate);
  static ExternalReference wasm_word64_popcnt(Isolate* isolate);
  static ExternalReference wasm_word32_rol(Isolate* isolate);
  static ExternalReference wasm_word32_ror(Isolate* isolate);
  static ExternalReference wasm_float64_pow(Isolate* isolate);
  static ExternalReference wasm_set_thread_in_wasm_flag(Isolate* isolate);
  static ExternalReference wasm_clear_thread_in_wasm_flag(Isolate* isolate);

  static ExternalReference f64_acos_wrapper_function(Isolate* isolate);
  static ExternalReference f64_asin_wrapper_function(Isolate* isolate);
  static ExternalReference f64_mod_wrapper_function(Isolate* isolate);

  // Trap callback function for cctest/wasm/wasm-run-utils.h
  static ExternalReference wasm_call_trap_callback_for_testing(
      Isolate* isolate);

  // Log support.
  static ExternalReference log_enter_external_function(Isolate* isolate);
  static ExternalReference log_leave_external_function(Isolate* isolate);

  // Static variable Heap::roots_array_start()
  static ExternalReference roots_array_start(Isolate* isolate);

  // Static variable Heap::allocation_sites_list_address()
  static ExternalReference allocation_sites_list_address(Isolate* isolate);

  // Static variable StackGuard::address_of_jslimit()
  V8_EXPORT_PRIVATE static ExternalReference address_of_stack_limit(
      Isolate* isolate);

  // Static variable StackGuard::address_of_real_jslimit()
  static ExternalReference address_of_real_stack_limit(Isolate* isolate);

  // Static variable RegExpStack::limit_address()
  static ExternalReference address_of_regexp_stack_limit(Isolate* isolate);

  // Static variables for RegExp.
  static ExternalReference address_of_static_offsets_vector(Isolate* isolate);
  static ExternalReference address_of_regexp_stack_memory_address(
      Isolate* isolate);
  static ExternalReference address_of_regexp_stack_memory_size(
      Isolate* isolate);

  // Write barrier.
  static ExternalReference store_buffer_top(Isolate* isolate);
  static ExternalReference heap_is_marking_flag_address(Isolate* isolate);

  // Used for fast allocation in generated code.
  static ExternalReference new_space_allocation_top_address(Isolate* isolate);
  static ExternalReference new_space_allocation_limit_address(Isolate* isolate);
  static ExternalReference old_space_allocation_top_address(Isolate* isolate);
  static ExternalReference old_space_allocation_limit_address(Isolate* isolate);

  static ExternalReference mod_two_doubles_operation(Isolate* isolate);
  static ExternalReference power_double_double_function(Isolate* isolate);

  static ExternalReference handle_scope_next_address(Isolate* isolate);
  static ExternalReference handle_scope_limit_address(Isolate* isolate);
  static ExternalReference handle_scope_level_address(Isolate* isolate);

  static ExternalReference scheduled_exception_address(Isolate* isolate);
  static ExternalReference address_of_pending_message_obj(Isolate* isolate);

  // Static variables containing common double constants.
  static ExternalReference address_of_min_int();
  static ExternalReference address_of_one_half();
  static ExternalReference address_of_minus_one_half();
  static ExternalReference address_of_negative_infinity();
  static ExternalReference address_of_the_hole_nan();
  static ExternalReference address_of_uint32_bias();

  // Static variables containing simd constants.
  static ExternalReference address_of_float_abs_constant();
  static ExternalReference address_of_float_neg_constant();
  static ExternalReference address_of_double_abs_constant();
  static ExternalReference address_of_double_neg_constant();

  // IEEE 754 functions.
  static ExternalReference ieee754_acos_function(Isolate* isolate);
  static ExternalReference ieee754_acosh_function(Isolate* isolate);
  static ExternalReference ieee754_asin_function(Isolate* isolate);
  static ExternalReference ieee754_asinh_function(Isolate* isolate);
  static ExternalReference ieee754_atan_function(Isolate* isolate);
  static ExternalReference ieee754_atanh_function(Isolate* isolate);
  static ExternalReference ieee754_atan2_function(Isolate* isolate);
  static ExternalReference ieee754_cbrt_function(Isolate* isolate);
  static ExternalReference ieee754_cos_function(Isolate* isolate);
  static ExternalReference ieee754_cosh_function(Isolate* isolate);
  static ExternalReference ieee754_exp_function(Isolate* isolate);
  static ExternalReference ieee754_expm1_function(Isolate* isolate);
  static ExternalReference ieee754_log_function(Isolate* isolate);
  static ExternalReference ieee754_log1p_function(Isolate* isolate);
  static ExternalReference ieee754_log10_function(Isolate* isolate);
  static ExternalReference ieee754_log2_function(Isolate* isolate);
  static ExternalReference ieee754_sin_function(Isolate* isolate);
  static ExternalReference ieee754_sinh_function(Isolate* isolate);
  static ExternalReference ieee754_tan_function(Isolate* isolate);
  static ExternalReference ieee754_tanh_function(Isolate* isolate);

  static ExternalReference libc_memchr_function(Isolate* isolate);
  static ExternalReference libc_memcpy_function(Isolate* isolate);
  static ExternalReference libc_memmove_function(Isolate* isolate);
  static ExternalReference libc_memset_function(Isolate* isolate);

  static ExternalReference printf_function(Isolate* isolate);

  static ExternalReference try_internalize_string_function(Isolate* isolate);

  static ExternalReference check_object_type(Isolate* isolate);

#ifdef V8_INTL_SUPPORT
  static ExternalReference intl_convert_one_byte_to_lower(Isolate* isolate);
  static ExternalReference intl_to_latin1_lower_table(Isolate* isolate);
#endif  // V8_INTL_SUPPORT

  template <typename SubjectChar, typename PatternChar>
  static ExternalReference search_string_raw(Isolate* isolate);

  static ExternalReference orderedhashmap_gethash_raw(Isolate* isolate);

  static ExternalReference get_or_create_hash_raw(Isolate* isolate);
  static ExternalReference jsreceiver_create_identity_hash(Isolate* isolate);

  static ExternalReference copy_fast_number_jsarray_elements_to_typed_array(
      Isolate* isolate);
  static ExternalReference copy_typed_array_elements_to_typed_array(
      Isolate* isolate);
  static ExternalReference copy_typed_array_elements_slice(Isolate* isolate);

  static ExternalReference page_flags(Page* page);

  static ExternalReference ForDeoptEntry(Address entry);

  static ExternalReference cpu_features();

  static ExternalReference debug_is_active_address(Isolate* isolate);
  static ExternalReference debug_hook_on_function_call_address(
      Isolate* isolate);

  static ExternalReference is_profiling_address(Isolate* isolate);
  static ExternalReference invoke_function_callback(Isolate* isolate);
  static ExternalReference invoke_accessor_getter_callback(Isolate* isolate);

  static ExternalReference promise_hook_or_debug_is_active_address(
      Isolate* isolate);

  V8_EXPORT_PRIVATE static ExternalReference runtime_function_table_address(
      Isolate* isolate);

  static ExternalReference invalidate_prototype_chains_function(
      Isolate* isolate);

  Address address() const { return reinterpret_cast<Address>(address_); }

  // Used to read out the last step action of the debugger.
  static ExternalReference debug_last_step_action_address(Isolate* isolate);

  // Used to check for suspended generator, used for stepping across await call.
  static ExternalReference debug_suspended_generator_address(Isolate* isolate);

  // Used to store the frame pointer to drop to when restarting a frame.
  static ExternalReference debug_restart_fp_address(Isolate* isolate);

#ifndef V8_INTERPRETED_REGEXP
  // C functions called from RegExp generated code.

  // Function NativeRegExpMacroAssembler::CaseInsensitiveCompareUC16()
  static ExternalReference re_case_insensitive_compare_uc16(Isolate* isolate);

  // Function RegExpMacroAssembler*::CheckStackGuardState()
  static ExternalReference re_check_stack_guard_state(Isolate* isolate);

  // Function NativeRegExpMacroAssembler::GrowStack()
  static ExternalReference re_grow_stack(Isolate* isolate);

  // byte NativeRegExpMacroAssembler::word_character_bitmap
  static ExternalReference re_word_character_map();

#endif

  // This lets you register a function that rewrites all external references.
  // Used by the ARM simulator to catch calls to external references.
  static void set_redirector(Isolate* isolate,
                             ExternalReferenceRedirector* redirector);

  static ExternalReference stress_deopt_count(Isolate* isolate);

  static ExternalReference force_slow_path(Isolate* isolate);

  static ExternalReference fixed_typed_array_base_data_offset();

 private:
  explicit ExternalReference(void* address) : address_(address) {}

  static void* Redirect(Isolate* isolate, Address address_arg,
                        Type type = ExternalReference::BUILTIN_CALL) {
    ExternalReferenceRedirector* redirector =
        reinterpret_cast<ExternalReferenceRedirector*>(
            isolate->external_reference_redirector());
    void* address = reinterpret_cast<void*>(address_arg);
    void* answer =
        (redirector == nullptr) ? address : (*redirector)(address, type);
    return answer;
  }

  void* address_;
};

V8_EXPORT_PRIVATE bool operator==(ExternalReference, ExternalReference);
bool operator!=(ExternalReference, ExternalReference);

size_t hash_value(ExternalReference);

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, ExternalReference);

}  // namespace internal
}  // namespace v8

#endif  // V8_EXTERNAL_REFERENCE_H_
