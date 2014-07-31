// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_OPERATOR_PROPERTIES_H_
#define V8_COMPILER_OPERATOR_PROPERTIES_H_

#include "src/v8.h"

namespace v8 {
namespace internal {
namespace compiler {

class Operator;

class OperatorProperties {
 public:
  static int GetValueOutputCount(Operator* op);
  static int GetValueInputCount(Operator* op);
  static bool HasContextInput(Operator* op);
  static int GetEffectInputCount(Operator* op);
  static int GetControlInputCount(Operator* op);

  static bool IsBasicBlockBegin(Operator* op);

  static bool CanBeScheduled(Operator* op);
  static bool HasFixedSchedulePosition(Operator* op);
  static bool IsScheduleRoot(Operator* op);

  static bool CanLazilyDeoptimize(Operator* op);
};
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_OPERATOR_PROPERTIES_H_
