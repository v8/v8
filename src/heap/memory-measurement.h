// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_MEASUREMENT_H_
#define V8_HEAP_MEMORY_MEASUREMENT_H_

#include <unordered_map>

#include "src/common/globals.h"
#include "src/objects/contexts.h"
#include "src/objects/map.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

class Heap;

class V8_EXPORT_PRIVATE MemoryMeasurement {
 public:
  explicit MemoryMeasurement(Isolate* isolate);
  Handle<JSPromise> EnqueueRequest(Handle<NativeContext> context,
                                   v8::MeasureMemoryMode mode);
 private:
  Isolate* isolate_;
};

// Infers the native context for some of the heap objects.
class V8_EXPORT_PRIVATE NativeContextInferrer {
 public:
  // The native_context parameter is both the input and output parameter.
  // It should be initialized to the context that will be used for the object
  // if the inference is not successful. The function performs more work if the
  // context is the shared context.
  V8_INLINE bool Infer(Isolate* isolate, Map map, HeapObject object,
                       Address* native_context);

 private:
  bool InferForJSFunction(JSFunction function, Address* native_context);
  bool InferForJSObject(Isolate* isolate, Map map, JSObject object,
                        Address* native_context);
};

// Maintains mapping from native contexts to their sizes.
class V8_EXPORT_PRIVATE NativeContextStats {
 public:
  void IncrementSize(Address context, size_t size) {
    size_by_context_[context] += size;
  }

  size_t Get(Address context) const {
    const auto it = size_by_context_.find(context);
    if (it == size_by_context_.end()) return 0;
    return it->second;
  }

  void Clear();
  void Merge(const NativeContextStats& other);

 private:
  std::unordered_map<Address, size_t> size_by_context_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_MEASUREMENT_H_
