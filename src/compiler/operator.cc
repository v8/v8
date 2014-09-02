// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/operator.h"

#include "src/assembler.h"

namespace v8 {
namespace internal {
namespace compiler {

Operator::~Operator() {}


SimpleOperator::SimpleOperator(Opcode opcode, Properties properties,
                               int input_count, int output_count,
                               const char* mnemonic)
    : Operator(opcode, properties, mnemonic),
      input_count_(input_count),
      output_count_(output_count) {}


SimpleOperator::~SimpleOperator() {}


// static
OStream& StaticParameterTraits<ExternalReference>::PrintTo(
    OStream& os, ExternalReference reference) {
  os << reference.address();
  // TODO(bmeurer): Move to operator<<(os, ExternalReference)
  const Runtime::Function* function =
      Runtime::FunctionForEntry(reference.address());
  if (function) {
    os << " <" << function->name << ".entry>";
  }
  return os;
}


// static
int StaticParameterTraits<ExternalReference>::HashCode(
    ExternalReference reference) {
  return reinterpret_cast<intptr_t>(reference.address()) & 0xFFFFFFFF;
}


// static
bool StaticParameterTraits<ExternalReference>::Equals(ExternalReference lhs,
                                                      ExternalReference rhs) {
  return lhs == rhs;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
