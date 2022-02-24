// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_H_
#define V8_MAGLEV_MAGLEV_H_

// TODO(v8:7700): Remove all references to V8_ENABLE_MAGLEV once maglev ships.

#ifdef V8_ENABLE_MAGLEV

#include "src/handles/handles.h"

namespace v8 {
namespace internal {

class Isolate;
class JSFunction;

class Maglev : public AllStatic {
 public:
  static MaybeHandle<CodeT> Compile(Isolate* isolate,
                                    Handle<JSFunction> function);
};

#endif  // V8_ENABLE_MAGLEV

}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_H_
