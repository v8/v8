// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/operator.h"

#include <limits>

namespace v8 {
namespace internal {
namespace compiler {

Operator::~Operator() {}


std::ostream& operator<<(std::ostream& os, const Operator& op) {
  op.PrintTo(os);
  return os;
}


SimpleOperator::SimpleOperator(Opcode opcode, Properties properties,
                               size_t input_count, size_t output_count,
                               const char* mnemonic)
    : Operator(opcode, properties, mnemonic),
      input_count_(static_cast<uint8_t>(input_count)),
      output_count_(static_cast<uint8_t>(output_count)) {
  DCHECK(input_count <= std::numeric_limits<uint8_t>::max());
  DCHECK(output_count <= std::numeric_limits<uint8_t>::max());
}


SimpleOperator::~SimpleOperator() {}


bool SimpleOperator::Equals(const Operator* that) const {
  return opcode() == that->opcode();
}


size_t SimpleOperator::HashCode() const {
  return base::hash<Opcode>()(opcode());
}


int SimpleOperator::InputCount() const { return input_count_; }


int SimpleOperator::OutputCount() const { return output_count_; }


void SimpleOperator::PrintTo(std::ostream& os) const { os << mnemonic(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
