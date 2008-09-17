// Copyright 2006-2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "debug.h"
#include "scopeinfo.h"

namespace v8 { namespace internal {

JSBuiltinsObject* Context::builtins() {
  GlobalObject* object = global();
  if (object->IsJSGlobalObject()) {
    return JSGlobalObject::cast(object)->builtins();
  } else {
    ASSERT(object->IsJSBuiltinsObject());
    return JSBuiltinsObject::cast(object);
  }
}


Context* Context::global_context() {
  // Fast case: the global object for this context has been set.  In
  // that case, the global object has a direct pointer to the global
  // context.
  if (global()->IsGlobalObject()) {
    return global()->global_context();
  }
  // During bootstrapping, the global object might not be set and we
  // have to search the context chain to find the global context.
  Context* current = this;
  while (!current->IsGlobalContext()) {
    current = Context::cast(JSFunction::cast(current->closure())->context());
  }
  return current;
}


Handle<Object> Context::Lookup(Handle<String> name, ContextLookupFlags flags,
                               int* index_, PropertyAttributes* attributes) {
  Handle<Context> context(this);

  // The context must be in frame slot 0 (if not debugging).
  if (kDebug && !Debug::InDebugger()) {
    StackFrameLocator locator;
    ASSERT(context->fcontext() ==
           Context::cast(
               locator.FindJavaScriptFrame(0)->context())->fcontext());
  }

  bool follow_context_chain = (flags & FOLLOW_CONTEXT_CHAIN) != 0;
  *index_ = -1;
  *attributes = ABSENT;

  if (FLAG_trace_contexts) {
    PrintF("Context::Lookup(");
    name->ShortPrint();
    PrintF(")\n");
  }

  do {
    if (FLAG_trace_contexts) {
      PrintF(" - looking in context %p", *context);
      if (context->IsGlobalContext()) PrintF(" (global context)");
      PrintF("\n");
    }

    // check extension/with object
    Handle<JSObject> context_ext(context->extension());
    if (*context_ext != NULL) {
      if ((flags & FOLLOW_PROTOTYPE_CHAIN) == 0) {
        *attributes = context_ext->GetLocalPropertyAttribute(*name);
      } else {
        *attributes = context_ext->GetPropertyAttribute(*name);
      }
      if (*attributes != ABSENT) {
        // property found
        if (FLAG_trace_contexts) {
          PrintF("=> found property in context object %p\n", *context_ext);
        }
        return context_ext;
      }
    }

    if (context->is_function_context()) {
      // we have context-local slots

      // check non-parameter locals in context
      Handle<Code> code(context->closure()->code());
      Variable::Mode mode;
      int index = ScopeInfo<>::ContextSlotIndex(*code, *name, &mode);
      ASSERT(index < 0 || index >= MIN_CONTEXT_SLOTS);
      if (index >= 0) {
        // slot found
        if (FLAG_trace_contexts) {
          PrintF("=> found local in context slot %d (mode = %d)\n",
                 index, mode);
        }
        *index_ = index;
        // Note: Fixed context slots are statically allocated by the compiler.
        // Statically allocated variables always have a statically known mode,
        // which is the mode with which they were declared when added to the
        // scope. Thus, the DYNAMIC mode (which corresponds to dynamically
        // declared variables that were introduced through declaration nodes)
        // must not appear here.
        switch (mode) {
          case Variable::INTERNAL :  // fall through
          case Variable::VAR      : *attributes = NONE; break;
          case Variable::CONST    : *attributes = READ_ONLY; break;
          case Variable::DYNAMIC  : UNREACHABLE(); break;
          case Variable::TEMPORARY: UNREACHABLE(); break;
        }
        return context;
      }

      // check parameter locals in context
      int param_index = ScopeInfo<>::ParameterIndex(*code, *name);
      if (param_index >= 0) {
        // slot found
        int index =
            ScopeInfo<>::ContextSlotIndex(*code,
                                          Heap::arguments_shadow_symbol(),
                                          NULL);
        ASSERT(index >= 0);  // arguments must exist and be in the heap context
        Handle<JSObject> arguments(JSObject::cast(context->get(index)));
        ASSERT(arguments->HasLocalProperty(Heap::length_symbol()));
        if (FLAG_trace_contexts) {
          PrintF("=> found parameter %d in arguments object\n", param_index);
        }
        *index_ = param_index;
        *attributes = NONE;
        return arguments;
      }

      // check intermediate context (holding only the function name variable)
      if (follow_context_chain) {
        int index = ScopeInfo<>::FunctionContextSlotIndex(*code, *name);
        if (index >= 0) {
          // slot found
          if (FLAG_trace_contexts) {
            PrintF("=> found intermediate function in context slot %d\n",
                   index);
          }
          *index_ = index;
          *attributes = READ_ONLY;
          return context;
        }
      }
    }

    // proceed with enclosing context
    if (context->IsGlobalContext()) {
      follow_context_chain = false;
    } else if (context->previous() != NULL) {
      context = Handle<Context>(context->previous());
    } else {
      ASSERT(context->is_function_context());
      context = Handle<Context>(Context::cast(context->closure()->context()));
    }
  } while (follow_context_chain);

  // slot not found
  if (FLAG_trace_contexts) {
    PrintF("=> no property/slot found\n");
  }
  return Handle<Object>(reinterpret_cast<JSObject*>(NULL));
}


} }  // namespace v8::internal
