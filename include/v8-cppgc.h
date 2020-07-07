// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_CPPGC_H_
#define INCLUDE_V8_CPPGC_H_

#include "cppgc/visitor.h"
#include "v8.h"  // NOLINT(build/include_directory)

namespace v8 {

template <typename T>
class JSMember;

namespace internal {

// TODO(chromium:1056170): Provide implementation based on global handles.
class JSMemberBase {
 private:
  JSMemberBase() = default;

  template <typename T>
  friend class v8::JSMember;
};

}  // namespace internal

/**
 * A traced handle without destructor that clears the handle. The handle may
 * only be used in GarbageCollected objects and must be processed in a Trace()
 * method.
 *
 * TODO(chromium:1056170): Implementation.
 */
template <typename T>
class JSMember : public internal::JSMemberBase {
  static_assert(std::is_base_of<v8::Value, T>::value,
                "JSMember only supports references to v8::Value");

 public:
  JSMember() = default;

  // Heterogeneous construction.
  template <typename U,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  JSMember(const JSMember<U>& other) {}  // NOLINT
};

class JSVisitor : public cppgc::Visitor {
 public:
  explicit JSVisitor(cppgc::Visitor::Key key) : cppgc::Visitor(key) {}

  template <typename T>
  void Trace(const JSMember<T>& ref) {
    Visit(ref);
  }

 protected:
  using cppgc::Visitor::Visit;

  virtual void Visit(const internal::JSMemberBase& ref) {}
};

}  // namespace v8

namespace cppgc {

template <typename T>
struct TraceTrait<v8::JSMember<T>> {
  static void Trace(Visitor* visitor, const v8::JSMember<T>* self) {
    static_cast<v8::JSVisitor*>(visitor)->Trace(*self);
  }
};

}  // namespace cppgc

#endif  // INCLUDE_V8_CPPGC_H_
