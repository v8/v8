// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/protectors.h"

#include "src/execution/isolate-inl.h"
#include "src/execution/protectors-inl.h"
#include "src/handles/handles-inl.h"
#include "src/objects/contexts.h"
#include "src/objects/property-cell.h"
#include "src/objects/smi.h"
#include "src/tracing/trace-event.h"
#include "src/utils/utils.h"

namespace v8::internal {

namespace {

void TraceProtectorInvalidation(const char* protector_name) {
  DCHECK(v8_flags.trace_protector_invalidation);
  static constexpr char kInvalidateProtectorTracingCategory[] =
      "V8.InvalidateProtector";
  static constexpr char kInvalidateProtectorTracingArg[] = "protector-name";

  DCHECK(v8_flags.trace_protector_invalidation);

  // TODO(jgruber): Remove the PrintF once tracing can output to stdout.
  i::PrintF("Invalidating protector cell %s\n", protector_name);
  TRACE_EVENT_INSTANT1("v8", kInvalidateProtectorTracingCategory,
                       TRACE_EVENT_SCOPE_THREAD, kInvalidateProtectorTracingArg,
                       protector_name);
}

// Static asserts to ensure we have a use counter for every protector. If this
// fails, add the use counter in V8 and chromium. Note: IsDefined is not
// strictly needed but clarifies the intent of the static assert.
constexpr bool IsDefined(v8::Isolate::UseCounterFeature) { return true; }
#define V(Name, ...) \
  static_assert(IsDefined(v8::Isolate::kInvalidated##Name##Protector));

DECLARED_PROTECTORS_ON_ISOLATE(V)
#undef V

}  // namespace

#define INVALIDATE_PROTECTOR_ON_ISOLATE_DEFINITION(name, unused_index, cell) \
  void Protectors::Invalidate##name(Isolate* isolate) {                      \
    DCHECK(isolate->factory()->cell()->value().IsSmi());                     \
    DCHECK(Is##name##Intact(isolate));                                       \
    if (v8_flags.trace_protector_invalidation) {                             \
      TraceProtectorInvalidation(#name);                                     \
    }                                                                        \
    isolate->CountUsage(v8::Isolate::kInvalidated##name##Protector);         \
    isolate->factory()->cell()->InvalidateProtector();                       \
    DCHECK(!Is##name##Intact(isolate));                                      \
  }
DECLARED_PROTECTORS_ON_ISOLATE(INVALIDATE_PROTECTOR_ON_ISOLATE_DEFINITION)
#undef INVALIDATE_PROTECTOR_ON_ISOLATE_DEFINITION

void Protectors::InvalidateRespectiveIteratorLookupChain(
    Isolate* isolate, InstanceType instance_type) {
  if (InstanceTypeChecker::IsJSArrayIterator(instance_type) ||
      InstanceTypeChecker::IsJSArrayIteratorPrototype(instance_type)) {
    if (!Protectors::IsArrayIteratorLookupChainIntact(isolate)) return;
    Protectors::InvalidateArrayIteratorLookupChain(isolate);

  } else if (InstanceTypeChecker::IsJSMapIterator(instance_type) ||
             InstanceTypeChecker::IsJSMapIteratorPrototype(instance_type)) {
    if (!Protectors::IsMapIteratorLookupChainIntact(isolate)) return;
    Protectors::InvalidateMapIteratorLookupChain(isolate);

  } else if (InstanceTypeChecker::IsJSSetIterator(instance_type) ||
             InstanceTypeChecker::IsJSSetIteratorPrototype(instance_type)) {
    if (!Protectors::IsSetIteratorLookupChainIntact(isolate)) return;
    Protectors::InvalidateSetIteratorLookupChain(isolate);

  } else if (InstanceTypeChecker::IsJSStringIterator(instance_type) ||
             InstanceTypeChecker::IsJSStringIteratorPrototype(instance_type)) {
    if (!Protectors::IsStringIteratorLookupChainIntact(isolate)) return;
    Protectors::InvalidateStringIteratorLookupChain(isolate);
  }
}

void Protectors::InvalidateRespectiveIteratorLookupChainForReturn(
    Isolate* isolate, InstanceType instance_type) {
  if (InstanceTypeChecker::IsJSIteratorPrototype(instance_type) ||
      InstanceTypeChecker::IsJSObjectPrototype(instance_type)) {
    // Addition of the "return" property to the Object prototype alters
    // behaviour of all iterators because the "return" callback might need to be
    // called according to the iterator protocol.
    Protectors::InvalidateAllIteratorLookupChains(isolate);
  } else {
    Protectors::InvalidateRespectiveIteratorLookupChain(isolate, instance_type);
  }
}

void Protectors::InvalidateAllIteratorLookupChains(Isolate* isolate) {
  if (Protectors::IsArrayIteratorLookupChainIntact(isolate)) {
    Protectors::InvalidateArrayIteratorLookupChain(isolate);
  }
  if (Protectors::IsMapIteratorLookupChainIntact(isolate)) {
    Protectors::InvalidateMapIteratorLookupChain(isolate);
  }
  if (Protectors::IsSetIteratorLookupChainIntact(isolate)) {
    Protectors::InvalidateSetIteratorLookupChain(isolate);
  }
  if (Protectors::IsStringIteratorLookupChainIntact(isolate)) {
    Protectors::InvalidateStringIteratorLookupChain(isolate);
  }
}

}  // namespace v8::internal
