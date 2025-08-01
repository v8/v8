// Copyright 2010 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_VM_STATE_INL_H_
#define V8_EXECUTION_VM_STATE_INL_H_

#include "src/execution/vm-state.h"
// Include the non-inl header before the rest of the headers.

#include "src/execution/isolate-inl.h"
#include "src/execution/simulator.h"
#include "src/logging/log.h"
#include "src/tracing/trace-event.h"

namespace v8 {
namespace internal {

constexpr const char* ToString(StateTag state) {
  switch (state) {
    case JS:
      return "JS";
    case GC:
      return "GC";
    case PARSER:
      return "PARSER";
    case BYTECODE_COMPILER:
      return "BYTECODE_COMPILER";
    case COMPILER:
      return "COMPILER";
    case OTHER:
      return "OTHER";
    case EXTERNAL:
      return "EXTERNAL";
    case ATOMICS_WAIT:
      return "ATOMICS_WAIT";
    case IDLE:
      return "IDLE";
    case IDLE_EXTERNAL:
      return "IDLE_EXTERNAL";
    case LOGGING:
      return "LOGGING";
  }
}

inline std::ostream& operator<<(std::ostream& os, StateTag kind) {
  return os << ToString(kind);
}

template <StateTag Tag>
VMState<Tag>::VMState(Isolate* isolate)
    : isolate_(isolate), previous_tag_(isolate->current_vm_state()) {
  isolate_->set_current_vm_state(Tag);
}

template <StateTag Tag>
VMState<Tag>::~VMState() {
  isolate_->set_current_vm_state(previous_tag_);
}

ExternalCallbackScope::ExternalCallbackScope(
    Isolate* isolate, Address callback, v8::ExceptionContext exception_context,
    const void* callback_info)
    : callback_(callback),
      callback_info_(callback_info),
      previous_scope_(isolate->external_callback_scope()),
      vm_state_(isolate),
      exception_context_(exception_context),
      pause_timed_histogram_scope_(isolate->counters()->execute()) {
#if USE_SIMULATOR || V8_USE_ADDRESS_SANITIZER || V8_USE_SAFE_STACK
  js_stack_comparable_address_ =
      i::SimulatorStack::RegisterJSStackComparableAddress(isolate);
#endif
  vm_state_.isolate_->set_external_callback_scope(this);
#ifdef V8_RUNTIME_CALL_STATS
  TRACE_EVENT_BEGIN0(TRACE_DISABLED_BY_DEFAULT("v8.runtime"),
                     "V8.ExternalCallback");
#endif
  // The external callback might be called via different code paths and on some
  // of them it's not guaranteed that the topmost_script_having_context value
  // is still valid (in particular, when the callback call is initiated by
  // embedder via V8 Api). So, clear it to ensure correctness of
  // Isolate::GetIncumbentContext().
  vm_state_.isolate_->clear_topmost_script_having_context();
}

ExternalCallbackScope::~ExternalCallbackScope() {
  vm_state_.isolate_->set_external_callback_scope(previous_scope_);
  // JS code might have been executed by the callback and it could have changed
  // {topmost_script_having_context}, clear it to ensure correctness of
  // Isolate::GetIncumbentContext() in case it'll be called after returning
  // from the callback.
  vm_state_.isolate_->clear_topmost_script_having_context();
#ifdef V8_RUNTIME_CALL_STATS
  TRACE_EVENT_END0(TRACE_DISABLED_BY_DEFAULT("v8.runtime"),
                   "V8.ExternalCallback");
#endif
#if USE_SIMULATOR || V8_USE_ADDRESS_SANITIZER || V8_USE_SAFE_STACK
  i::SimulatorStack::UnregisterJSStackComparableAddress(vm_state_.isolate_);
#endif
}

Address ExternalCallbackScope::JSStackComparableAddress() {
#if USE_SIMULATOR || V8_USE_ADDRESS_SANITIZER || V8_USE_SAFE_STACK
  return js_stack_comparable_address_;
#else
  return reinterpret_cast<Address>(this);
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_VM_STATE_INL_H_
