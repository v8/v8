// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_FLAGS_H_
#define V8_BASE_FLAGS_H_

#include "include/v8config.h"

namespace v8 {
namespace base {

// The Flags class provides a type-safe way of storing OR-combinations of enum
// values. The Flags<T> class is a template class, where T is an enum type.
//
// The traditional C++ approach for storing OR-combinations of enum values is to
// use an int or unsigned int variable. The inconvenience with this approach is
// that there's no type checking at all; any enum value can be OR'd with any
// other enum value and passed on to a function that takes an int or unsigned
// int.
template <typename T>
class Flags V8_FINAL {
 public:
  typedef T flag_type;
  typedef int mask_type;

  Flags() : mask_(0) {}
  Flags(flag_type flag) : mask_(flag) {}  // NOLINT(runtime/explicit)
  explicit Flags(mask_type mask) : mask_(mask) {}

  Flags& operator&=(const Flags& flags) {
    mask_ &= flags.mask_;
    return *this;
  }
  Flags& operator|=(const Flags& flags) {
    mask_ |= flags.mask_;
    return *this;
  }
  Flags& operator^=(const Flags& flags) {
    mask_ ^= flags.mask_;
    return *this;
  }

  Flags operator&(const Flags& flags) const { return Flags(*this) &= flags; }
  Flags operator|(const Flags& flags) const { return Flags(*this) |= flags; }
  Flags operator^(const Flags& flags) const { return Flags(*this) ^= flags; }

  Flags& operator&=(flag_type flag) { return operator&=(Flags(flag)); }
  Flags& operator|=(flag_type flag) { return operator|=(Flags(flag)); }
  Flags& operator^=(flag_type flag) { return operator^=(Flags(flag)); }

  Flags operator&(flag_type flag) const { return operator&(Flags(flag)); }
  Flags operator|(flag_type flag) const { return operator|(Flags(flag)); }
  Flags operator^(flag_type flag) const { return operator^(Flags(flag)); }

  Flags operator~() const { return Flags(~mask_); }

  operator mask_type() const { return mask_; }
  bool operator!() const { return !mask_; }

 private:
  mask_type mask_;
};


#define DEFINE_FLAGS(Type, Enum) typedef ::v8::base::Flags<Enum> Type

#define DEFINE_OPERATORS_FOR_FLAGS(Type)                                       \
  inline ::v8::base::Flags<Type::flag_type> operator&(                         \
      Type::flag_type lhs,                                                     \
      Type::flag_type rhs)V8_UNUSED V8_WARN_UNUSED_RESULT;                     \
  inline ::v8::base::Flags<Type::flag_type> operator&(Type::flag_type lhs,     \
                                                      Type::flag_type rhs) {   \
    return ::v8::base::Flags<Type::flag_type>(lhs) & rhs;                      \
  }                                                                            \
  inline ::v8::base::Flags<Type::flag_type> operator&(                         \
      Type::flag_type lhs, const ::v8::base::Flags<Type::flag_type>& rhs)      \
      V8_UNUSED V8_WARN_UNUSED_RESULT;                                         \
  inline ::v8::base::Flags<Type::flag_type> operator&(                         \
      Type::flag_type lhs, const ::v8::base::Flags<Type::flag_type>& rhs) {    \
    return rhs & lhs;                                                          \
  }                                                                            \
  inline void operator&(Type::flag_type lhs, Type::mask_type rhs)V8_UNUSED;    \
  inline void operator&(Type::flag_type lhs, Type::mask_type rhs) {}           \
  inline ::v8::base::Flags<Type::flag_type> operator|(Type::flag_type lhs,     \
                                                      Type::flag_type rhs)     \
      V8_UNUSED V8_WARN_UNUSED_RESULT;                                         \
  inline ::v8::base::Flags<Type::flag_type> operator|(Type::flag_type lhs,     \
                                                      Type::flag_type rhs) {   \
    return ::v8::base::Flags<Type::flag_type>(lhs) | rhs;                      \
  }                                                                            \
  inline ::v8::base::Flags<Type::flag_type> operator|(                         \
      Type::flag_type lhs, const ::v8::base::Flags<Type::flag_type>& rhs)      \
      V8_UNUSED V8_WARN_UNUSED_RESULT;                                         \
  inline ::v8::base::Flags<Type::flag_type> operator|(                         \
      Type::flag_type lhs, const ::v8::base::Flags<Type::flag_type>& rhs) {    \
    return rhs | lhs;                                                          \
  }                                                                            \
  inline void operator|(Type::flag_type lhs, Type::mask_type rhs) V8_UNUSED;   \
  inline void operator|(Type::flag_type lhs, Type::mask_type rhs) {}           \
  inline ::v8::base::Flags<Type::flag_type> operator^(Type::flag_type lhs,     \
                                                      Type::flag_type rhs)     \
      V8_UNUSED V8_WARN_UNUSED_RESULT;                                         \
  inline ::v8::base::Flags<Type::flag_type> operator^(Type::flag_type lhs,     \
                                                      Type::flag_type rhs) {   \
    return ::v8::base::Flags<Type::flag_type>(lhs) ^ rhs;                      \
  } inline ::v8::base::Flags<Type::flag_type>                                  \
      operator^(Type::flag_type lhs,                                           \
                const ::v8::base::Flags<Type::flag_type>& rhs)                 \
      V8_UNUSED V8_WARN_UNUSED_RESULT;                                         \
  inline ::v8::base::Flags<Type::flag_type> operator^(                         \
      Type::flag_type lhs, const ::v8::base::Flags<Type::flag_type>& rhs) {    \
    return rhs ^ lhs;                                                          \
  } inline void operator^(Type::flag_type lhs, Type::mask_type rhs) V8_UNUSED; \
  inline void operator^(Type::flag_type lhs, Type::mask_type rhs) {}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_FLAGS_H_
