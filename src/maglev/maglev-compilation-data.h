// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_COMPILATION_DATA_H_
#define V8_MAGLEV_MAGLEV_COMPILATION_DATA_H_

#include "src/common/globals.h"
#include "src/compiler/bytecode-analysis.h"
#include "src/compiler/heap-refs.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevGraphLabeller;
struct MaglevCompilationData {
  explicit MaglevCompilationData(compiler::JSHeapBroker* broker);
  ~MaglevCompilationData();
  std::unique_ptr<MaglevGraphLabeller> graph_labeller;
  compiler::JSHeapBroker* const broker;
  Isolate* const isolate;
  Zone zone;
};

struct MaglevCompilationUnit {
  MaglevCompilationUnit(MaglevCompilationData* data,
                        Handle<JSFunction> function);

  compiler::JSHeapBroker* broker() const { return compilation_data->broker; }
  Isolate* isolate() const { return compilation_data->isolate; }
  Zone* zone() const { return &compilation_data->zone; }
  int register_count() const { return register_count_; }
  int parameter_count() const { return parameter_count_; }
  bool has_graph_labeller() const { return !!compilation_data->graph_labeller; }
  MaglevGraphLabeller* graph_labeller() const {
    DCHECK(has_graph_labeller());
    return compilation_data->graph_labeller.get();
  }

  MaglevCompilationData* const compilation_data;
  const compiler::BytecodeArrayRef bytecode;
  const compiler::FeedbackVectorRef feedback;
  compiler::BytecodeAnalysis const bytecode_analysis;
  int register_count_;
  int parameter_count_;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_COMPILATION_DATA_H_
