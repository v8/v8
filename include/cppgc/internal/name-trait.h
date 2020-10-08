// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_NAME_TRAIT_H_
#define INCLUDE_CPPGC_INTERNAL_NAME_TRAIT_H_

#include "cppgc/name-provider.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {
namespace internal {

struct HeapObjectName {
  const char* value;
  bool name_was_hidden;
};

class V8_EXPORT NameTraitBase {
 protected:
  static HeapObjectName GetNameFromTypeSignature(const char*);
};

// Trait that specifies how the garbage collector retrieves the name for a
// given object.
template <typename T>
class NameTrait final : public NameTraitBase {
 public:
  static HeapObjectName GetName(const void* obj) {
    return GetNameFor(static_cast<const T*>(obj));
  }

 private:
  static HeapObjectName GetNameFor(const NameProvider* name_provider) {
    return {name_provider->GetName(), false};
  }

  static HeapObjectName GetNameFor(...) {
#if CPPGC_SUPPORTS_OBJECT_NAMES

#if defined(V8_CC_GNU)
#define PRETTY_FUNCTION_VALUE __PRETTY_FUNCTION__
#elif defined(V8_CC_MSVC)
#define PRETTY_FUNCTION_VALUE __FUNCSIG__
#else
#define PRETTY_FUNCTION_VALUE nullptr
#endif

    static const HeapObjectName leaky_name =
        GetNameFromTypeSignature(PRETTY_FUNCTION_VALUE);
    return leaky_name;

#undef PRETTY_FUNCTION_VALUE

#else   // !CPPGC_SUPPORTS_OBJECT_NAMES
    return {NameProvider::kHiddenName, true};
#endif  // !CPPGC_SUPPORTS_OBJECT_NAMES
  }
};

using NameCallback = HeapObjectName (*)(const void*);

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_NAME_TRAIT_H_
