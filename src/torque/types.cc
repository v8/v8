// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <iostream>

#include "src/torque/types.h"

namespace v8 {
namespace internal {
namespace torque {

std::ostream& operator<<(std::ostream& os, const Signature& sig) {
  os << "(";
  for (size_t i = 0; i < sig.parameter_names.size(); ++i) {
    if (i > 0) os << ", ";
    if (!sig.parameter_names.empty()) os << sig.parameter_names[i] << ": ";
    os << sig.parameter_types.types[i];
  }
  if (sig.parameter_types.var_args) {
    if (sig.parameter_names.size()) os << ", ";
    os << "...";
  }
  os << ")";
  if (!sig.return_type.IsVoid()) {
    os << ": " << sig.return_type;
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const TypeVector& types) {
  for (size_t i = 0; i < types.size(); ++i) {
    if (i > 0) os << ", ";
    os << types[i];
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const ParameterTypes& p) {
  for (size_t i = 0; i < p.types.size(); ++i) {
    if (i > 0) os << ", ";
    os << p.types[i];
  }
  if (p.var_args) {
    if (p.types.size() > 0) os << ", ";
    os << "...";
  }
  return os;
}

}  // namespace torque
}  // namespace internal
}  // namespace v8
