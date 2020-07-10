// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_CPPGC_H_
#define INCLUDE_V8_CPPGC_H_

#include "cppgc/visitor.h"
#include "v8-internal.h"  // NOLINT(build/include_directory)
#include "v8.h"  // NOLINT(build/include_directory)

namespace v8 {

class Isolate;
template <typename T>
class JSMember;

namespace internal {

class JSMemberBaseExtractor;

// TODO(chromium:1056170): Provide implementation based on global handles.
class JSMemberBase {
 public:
  /**
   * Returns true if the reference is empty, i.e., has not been assigned
   * object.
   */
  bool IsEmpty() const { return val_ == kNullAddress; }

  /**
   * Clears the reference. IsEmpty() will return true after this call.
   */
  V8_INLINE void Reset();

 private:
  static internal::Address New(v8::Isolate* isolate, internal::Address* object,
                               internal::Address* slot);
  static void Delete(internal::Address* slot);

  JSMemberBase() = default;

  JSMemberBase(v8::Isolate* isolate, internal::Address* object)
      : val_(New(isolate, object, &this->val_)) {}

  internal::Address val_ = kNullAddress;

  template <typename T>
  friend class v8::JSMember;
  friend class v8::internal::JSMemberBaseExtractor;
};

void JSMemberBase::Reset() {
  if (IsEmpty()) return;
  Delete(reinterpret_cast<internal::Address*>(val_));
  val_ = kNullAddress;
}

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

  template <typename U,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  JSMember(Isolate* isolate, Local<U> that)
      : internal::JSMemberBase(isolate, that.val_) {}

  // Heterogeneous construction.
  // TODO(chromium:1056170): Implementation.
  template <typename U,
            typename = std::enable_if_t<std::is_base_of<T, U>::value>>
  JSMember(const JSMember<U>& other) {}  // NOLINT
};

class JSVisitor : public cppgc::Visitor {
 public:
  explicit JSVisitor(cppgc::Visitor::Key key) : cppgc::Visitor(key) {}

  template <typename T>
  void Trace(const JSMember<T>& ref) {
    if (ref.IsEmpty()) return;
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
