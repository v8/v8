// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_COMPILER_H_
#define V8_MAGLEV_MAGLEV_COMPILER_H_

#include "src/common/globals.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/heap-refs.h"
#include "src/maglev/maglev-compilation-data.h"

namespace v8 {
namespace internal {

namespace compiler {
class JSHeapBroker;
}

namespace maglev {

class MaglevCompiler {
 public:
  explicit MaglevCompiler(compiler::JSHeapBroker* broker,
                          Handle<JSFunction> function);

  MaybeHandle<Code> Compile();

  compiler::JSHeapBroker* broker() const { return compilation_data_.broker; }
  Zone* zone() { return &compilation_data_.zone; }
  Isolate* isolate() { return compilation_data_.isolate; }

 private:
  MaglevCompilationData compilation_data_;
  MaglevCompilationUnit toplevel_compilation_unit_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_COMPILER_H_
