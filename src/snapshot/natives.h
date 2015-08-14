// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SNAPSHOT_NATIVES_H_
#define V8_SNAPSHOT_NATIVES_H_

#include "src/heap/heap.h"
#include "src/heap/heap-inl.h"
#include "src/objects.h"
#include "src/objects-inl.h"
#include "src/vector.h"

namespace v8 { class StartupData; }  // Forward declaration.

namespace v8 {
namespace internal {

enum NativeType { CORE, CODE_STUB, EXPERIMENTAL, EXTRAS, D8, TEST };

template <NativeType type>
class NativesCollection {
 public:
  // The following methods are implemented in js2c-generated code:

  // Number of built-in scripts.
  static int GetBuiltinsCount();
  // Number of debugger implementation scripts.
  static int GetDebuggerCount();

  // These are used to access built-in scripts.  The debugger implementation
  // scripts have an index in the interval [0, GetDebuggerCount()).  The
  // non-debugger scripts have an index in the interval [GetDebuggerCount(),
  // GetNativesCount()).
  static int GetIndex(const char* name);
  static Vector<const char> GetScriptSource(int index);
  static Vector<const char> GetScriptName(int index);
  static Vector<const char> GetScriptsSource();

  // The following methods are implemented below:

  inline static FixedArray* GetSourceCache(Heap* heap);
  static void UpdateSourceCache(Heap* heap);
};

typedef NativesCollection<CORE> Natives;
typedef NativesCollection<CODE_STUB> CodeStubNatives;
typedef NativesCollection<EXPERIMENTAL> ExperimentalNatives;
typedef NativesCollection<EXTRAS> ExtraNatives;


template <>
inline FixedArray* Natives::GetSourceCache(Heap* heap) {
  return heap->natives_source_cache();
}


template <>
inline FixedArray* ExperimentalNatives::GetSourceCache(Heap* heap) {
  return heap->experimental_natives_source_cache();
}


template <>
inline FixedArray* ExtraNatives::GetSourceCache(Heap* heap) {
  return heap->extra_natives_source_cache();
}


template <>
inline FixedArray* CodeStubNatives::GetSourceCache(Heap* heap) {
  return heap->code_stub_natives_source_cache();
}

template <NativeType type>
void NativesCollection<type>::UpdateSourceCache(Heap* heap) {
  for (int i = 0; i < GetBuiltinsCount(); i++) {
    Object* source = GetSourceCache(heap)->get(i);
    if (!source->IsUndefined()) {
      ExternalOneByteString::cast(source)->update_data_cache();
    }
  }
}


#ifdef V8_USE_EXTERNAL_STARTUP_DATA
// Used for reading the natives at runtime. Implementation in natives-empty.cc
void SetNativesFromFile(StartupData* natives_blob);
void ReadNatives();
void DisposeNatives();
#endif

} }  // namespace v8::internal

#endif  // V8_SNAPSHOT_NATIVES_H_
