// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/type-hints.h"

namespace v8 {
namespace internal {
namespace compiler {

std::ostream& operator<<(std::ostream& os, BinaryOperationHints::Hint hint) {
  switch (hint) {
    case BinaryOperationHints::kNone:
      return os << "None";
    case BinaryOperationHints::kSignedSmall:
      return os << "SignedSmall";
    case BinaryOperationHints::kSigned32:
      return os << "Signed32";
    case BinaryOperationHints::kNumberOrUndefined:
      return os << "NumberOrUndefined";
    case BinaryOperationHints::kString:
      return os << "String";
    case BinaryOperationHints::kAny:
      return os << "Any";
  }
  UNREACHABLE();
  return os;
}

std::ostream& operator<<(std::ostream& os, BinaryOperationHints hints) {
  return os << hints.left() << "*" << hints.right() << "->" << hints.result();
}

std::ostream& operator<<(std::ostream& os, CompareOperationHints::Hint hint) {
  switch (hint) {
    case CompareOperationHints::kNone:
      return os << "None";
    case CompareOperationHints::kBoolean:
      return os << "Boolean";
    case CompareOperationHints::kSignedSmall:
      return os << "SignedSmall";
    case CompareOperationHints::kNumber:
      return os << "Number";
    case CompareOperationHints::kString:
      return os << "String";
    case CompareOperationHints::kInternalizedString:
      return os << "InternalizedString";
    case CompareOperationHints::kUniqueName:
      return os << "UniqueName";
    case CompareOperationHints::kReceiver:
      return os << "Receiver";
    case CompareOperationHints::kAny:
      return os << "Any";
  }
  UNREACHABLE();
  return os;
}

std::ostream& operator<<(std::ostream& os, CompareOperationHints hints) {
  return os << hints.left() << "*" << hints.right() << " (" << hints.combined()
            << ")";
}

std::ostream& operator<<(std::ostream& os, ToBooleanHint hint) {
  switch (hint) {
    case ToBooleanHint::kNone:
      return os << "None";
    case ToBooleanHint::kUndefined:
      return os << "Undefined";
    case ToBooleanHint::kBoolean:
      return os << "Boolean";
    case ToBooleanHint::kNull:
      return os << "Null";
    case ToBooleanHint::kSmallInteger:
      return os << "SmallInteger";
    case ToBooleanHint::kReceiver:
      return os << "Receiver";
    case ToBooleanHint::kString:
      return os << "String";
    case ToBooleanHint::kSymbol:
      return os << "Symbol";
    case ToBooleanHint::kHeapNumber:
      return os << "HeapNumber";
    case ToBooleanHint::kSimdValue:
      return os << "SimdValue";
    case ToBooleanHint::kAny:
      return os << "Any";
  }
  UNREACHABLE();
  return os;
}

std::ostream& operator<<(std::ostream& os, ToBooleanHints hints) {
  if (hints == ToBooleanHint::kAny) return os << "Any";
  if (hints == ToBooleanHint::kNone) return os << "None";
  bool first = true;
  for (ToBooleanHints::mask_type i = 0; i < sizeof(i) * CHAR_BIT; ++i) {
    ToBooleanHint const hint = static_cast<ToBooleanHint>(1u << i);
    if (hints & hint) {
      if (!first) os << "|";
      first = false;
      os << hint;
    }
  }
  return os;
}

// static
bool BinaryOperationHints::Is(Hint h1, Hint h2) {
  if (h1 == h2) return true;
  switch (h1) {
    case kNone:
      return true;
    case kSignedSmall:
      return h2 == kSigned32 || h2 == kNumberOrUndefined || h2 == kAny;
    case kSigned32:
      return h2 == kNumberOrUndefined || h2 == kAny;
    case kNumberOrUndefined:
      return h2 == kAny;
    case kString:
      return h2 == kAny;
    case kAny:
      return false;
  }
  UNREACHABLE();
  return false;
}

// static
BinaryOperationHints::Hint BinaryOperationHints::Combine(Hint h1, Hint h2) {
  if (Is(h1, h2)) return h2;
  if (Is(h2, h1)) return h1;
  return kAny;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
