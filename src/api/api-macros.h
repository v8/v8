// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note 1: Any file that includes this one should include api-macros-undef.h
// at the bottom.

// Note 2: This file is deliberately missing the include guards (the undeffing
// approach wouldn't work otherwise).
//
// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

#define API_RCS_SCOPE(i_isolate, class_name, function_name) \
  RCS_SCOPE(i_isolate,                                      \
            i::RuntimeCallCounterId::kAPI_##class_name##_##function_name);

#define ENTER_V8_BASIC(i_isolate)                            \
  /* Embedders should never enter V8 after terminating it */ \
  DCHECK_IMPLIES(i::v8_flags.strict_termination_checks,      \
                 !i_isolate->is_execution_terminating());    \
  i::VMState<v8::OTHER> __state__((i_isolate))

#define PREPARE_FOR_DEBUG_INTERFACE_EXECUTION_WITH_ISOLATE(i_isolate, context, \
                                                           T)                  \
  DCHECK(!i_isolate->is_execution_terminating());                              \
  InternalEscapableScope handle_scope(i_isolate);                              \
  CallDepthScope<false> call_depth_scope(i_isolate, context);                  \
  i::VMState<v8::OTHER> __state__((i_isolate));                                \
  bool has_exception = false

#ifdef DEBUG
// Lightweight version for APIs that don't require an active context.
#define DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate)                      \
  i::DisallowJavascriptExecutionDebugOnly __no_script__((i_isolate)); \
  i::DisallowExceptions __no_exceptions__((i_isolate))

#define ENTER_V8_FOR_NEW_CONTEXT(i_isolate)                 \
  DCHECK_IMPLIES(i::v8_flags.strict_termination_checks,     \
                 !(i_isolate)->is_execution_terminating()); \
  i::VMState<v8::OTHER> __state__((i_isolate));             \
  i::DisallowExceptions __no_exceptions__((i_isolate))
#else  // DEBUG
#define DCHECK_NO_SCRIPT_NO_EXCEPTION(i_isolate)

#define ENTER_V8_FOR_NEW_CONTEXT(i_isolate) \
  i::VMState<v8::OTHER> __state__((i_isolate));
#endif  // DEBUG
