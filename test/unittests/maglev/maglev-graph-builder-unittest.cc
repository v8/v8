// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef V8_ENABLE_MAGLEV

#include "src/maglev/maglev-graph-builder.h"

#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compiler.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "test/unittests/maglev/maglev-test.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevGraphBuilderTest : public TestWithNativeContextAndZone {};

template <typename T>
static T* getUniqueNode(Graph* graph) {
  T* target = nullptr;
  for (auto* block : graph->blocks()) {
    for (auto* node : block->nodes()) {
      if (node->Is<T>()) {
        CHECK_NULL(target);
        target = node->Cast<T>();
      }
    }
  }
  CHECK_NOT_NULL(target);
  return target;
}

TEST_F(MaglevGraphBuilderTest, TrailingArgumentsRemoval) {
  i::v8_flags.allow_natives_syntax = true;
  for (bool strict : {true, false}) {
    HandleScope scope(isolate());
    std::string script;
    if (strict) {
      script += "'use strict'\n";
    }
    script += R"(
      function f(a, b) { return a + b; }
      function g(a, b, c, d) { return f(a, b, c, d); }
      %PrepareFunctionForOptimization(g);
      g(1, 2, 3, 4);
      (g)
    )";
    Handle<JSFunction> function = RunJS<JSFunction>(script.c_str());
    CHECK_EQ(strict, function->shared()->CanOnlyAccessFixedFormalParameters());
    auto info =
        MaglevCompilationInfo::New(isolate(), function, BytecodeOffset::None());
    Graph* graph = Graph::New(info.get());
    compiler::CurrentHeapBrokerScope current_broker(info->broker());
    MaglevGraphBuilder graph_builder(isolate()->AsLocalIsolate(),
                                     info->toplevel_compilation_unit(), graph);
    PersistentHandlesScope persistent_scope(isolate());
    CHECK(graph_builder.Build());
    CallKnownJSFunction* func = getUniqueNode<CallKnownJSFunction>(graph);
    CHECK_EQ(strict ? 2 : 4, func->num_args());
    auto callee = func->shared_function_info().object()->Name()->ToCString();
    CHECK_EQ(0, strcmp("f", callee.get()));
    persistent_scope.Detach();
  }
}

TEST_F(MaglevGraphBuilderTest, UnusedArgumentsRemoval) {
  i::v8_flags.allow_natives_syntax = true;
  HandleScope scope(isolate());
  std::string script = R"(
      'use strict'
      function f(a, b) { return b; }
      function g(a, b) { return f(a, b); }
      %PrepareFunctionForOptimization(g);
      g(1, 2);
      (g)
    )";
  Handle<JSFunction> function = RunJS<JSFunction>(script.c_str());
  auto info =
      MaglevCompilationInfo::New(isolate(), function, BytecodeOffset::None());
  Graph* graph = Graph::New(info.get());
  compiler::CurrentHeapBrokerScope current_broker(info->broker());
  MaglevGraphBuilder graph_builder(isolate()->AsLocalIsolate(),
                                   info->toplevel_compilation_unit(), graph);
  PersistentHandlesScope persistent_scope(isolate());
  CHECK(graph_builder.Build());
  CallKnownJSFunction* func = getUniqueNode<CallKnownJSFunction>(graph);
  CHECK_EQ(2, func->num_args());
  auto callee = func->shared_function_info().object()->Name()->ToCString();
  CHECK_EQ(0, strcmp("f", callee.get()));
  // First arg has been eliminated.
  CHECK_EQ(func->arg(0).node()->Cast<RootConstant>()->index(),
           RootIndex::kOptimizedOut);
  persistent_scope.Detach();
}

// Regression test for the unused-parameter replacement loop bounding on
// `sizeof(unused_parameter_bits)` (== 4) instead of the highest set bit. An
// unused parameter at an index >= 4 must still be replaced by OptimizedOut.
TEST_F(MaglevGraphBuilderTest, UnusedArgumentsRemovalHighIndex) {
  i::v8_flags.allow_natives_syntax = true;
  HandleScope scope(isolate());
  // The 6th parameter (index 5) is unused; all others are used.
  std::string script = R"(
      'use strict'
      function f(a, b, c, d, e, unused) { return a + b + c + d + e; }
      function g(a, b, c, d, e, h) { return f(a, b, c, d, e, h); }
      %PrepareFunctionForOptimization(g);
      g(1, 2, 3, 4, 5, 6);
      (g)
    )";
  Handle<JSFunction> function = RunJS<JSFunction>(script.c_str());
  auto info =
      MaglevCompilationInfo::New(isolate(), function, BytecodeOffset::None());
  Graph* graph = Graph::New(info.get());
  compiler::CurrentHeapBrokerScope current_broker(info->broker());
  MaglevGraphBuilder graph_builder(isolate()->AsLocalIsolate(),
                                   info->toplevel_compilation_unit(), graph);
  PersistentHandlesScope persistent_scope(isolate());
  CHECK(graph_builder.Build());
  CallKnownJSFunction* func = getUniqueNode<CallKnownJSFunction>(graph);
  // No trailing arguments to remove: all 6 are formal parameters.
  CHECK_EQ(6, func->num_args());
  auto callee = func->shared_function_info().object()->Name()->ToCString();
  CHECK_EQ(0, strcmp("f", callee.get()));
  // The unused parameter at index 5 must have been replaced.
  CHECK_EQ(func->arg(5).node()->Cast<RootConstant>()->index(),
           RootIndex::kOptimizedOut);
  persistent_scope.Detach();
}

// Regression test for the truncation path: when the callee is invoked with more
// arguments than it has formal parameters, the trailing arguments are dropped
// and unused parameters (including ones at high indices) must still be replaced
// by OptimizedOut, without indexing past the truncated argument list.
TEST_F(MaglevGraphBuilderTest, UnusedArgumentsRemovalWithTruncation) {
  i::v8_flags.allow_natives_syntax = true;
  HandleScope scope(isolate());
  // `f` has 5 formal parameters; the 5th (index 4) is unused. It is called with
  // 6 arguments, so the call is truncated down to 5 arguments.
  std::string script = R"(
      'use strict'
      function f(a, b, c, d, unused) { return a + b + c + d; }
      function g(a, b, c, d, e, h) { return f(a, b, c, d, e, h); }
      %PrepareFunctionForOptimization(g);
      g(1, 2, 3, 4, 5, 6);
      (g)
    )";
  Handle<JSFunction> function = RunJS<JSFunction>(script.c_str());
  auto info =
      MaglevCompilationInfo::New(isolate(), function, BytecodeOffset::None());
  Graph* graph = Graph::New(info.get());
  compiler::CurrentHeapBrokerScope current_broker(info->broker());
  MaglevGraphBuilder graph_builder(isolate()->AsLocalIsolate(),
                                   info->toplevel_compilation_unit(), graph);
  PersistentHandlesScope persistent_scope(isolate());
  CHECK(graph_builder.Build());
  CallKnownJSFunction* func = getUniqueNode<CallKnownJSFunction>(graph);
  // The trailing 6th argument has been dropped, leaving the 5 formal params.
  CHECK_EQ(5, func->num_args());
  auto callee = func->shared_function_info().object()->Name()->ToCString();
  CHECK_EQ(0, strcmp("f", callee.get()));
  // The unused parameter at index 4 must have been replaced.
  CHECK_EQ(func->arg(4).node()->Cast<RootConstant>()->index(),
           RootIndex::kOptimizedOut);
  persistent_scope.Detach();
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV
