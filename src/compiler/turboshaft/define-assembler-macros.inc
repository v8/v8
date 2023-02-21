// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// PRESUBMIT_INTENTIONALLY_MISSING_INCLUDE_GUARD

// This file defines Turboshaft's assembler macros. Include this file before
// your reducers and don't forget to include 'undef-assembler-macros.inc'
// afterwards.

#ifdef V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED
#error \
    "Assembler macros already defined. Did you forget to #include \"undef-assembler-macros.inc\" in a previous file?"
#endif

#define V8_COMPILER_TURBOSHAFT_ASSEMBLER_MACROS_DEFINED 1

#define BIND(label, ...)                                                        \
  auto [CONCAT(prevent_empty_binding, __LINE__), ## __VA_ARGS__] =              \
    (Asm().Bind(label.block()), label.GetAll(Asm()));                           \
    (((void)CONCAT(prevent_empty_binding, __LINE__)), label.block()->IsBound())
#define GOTO(label, ...)                           \
  Asm().ControlFlowHelper_Goto(label, { __VA_ARGS__ })
#define GOTO_IF(cond, label, ...)                          \
  Asm().ControlFlowHelper_GotoIf(cond, label, { __VA_ARGS__ })
#define GOTO_IF_NOT(cond, label, ...)                         \
  Asm().ControlFlowHelper_GotoIfNot(cond, label, { __VA_ARGS__ })

#define IF_WITH_HINT(cond, hint)                                   \
  for (bool bound = Asm().ControlFlowHelper_If(cond, hint); bound; \
       (bound = false), Asm().ControlFlowHelper_GotoEnd())
#define IF(cond) IF_WITH_HINT(cond, BranchHint::kNone)
#define ELSE_IF(cond)                                            \
  for (bool bound = Asm().ControlFlowHelper_ElseIf(cond); bound; \
       (bound = false), Asm().ControlFlowHelper_GotoEnd())
#define ELSE                                               \
  for (bool bound = Asm().ControlFlowHelper_Else(); bound; \
       (bound = false), Asm().ControlFlowHelper_GotoEnd())
#define END_IF Asm().ControlFlowHelper_EndIf();

#define __ Asm().
