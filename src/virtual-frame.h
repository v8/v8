// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef V8_VIRTUAL_FRAME_H_
#define V8_VIRTUAL_FRAME_H_

#include "macro-assembler.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// Virtual frame elements
//
// The internal elements of the virtual frames.  There are several kinds of
// elements:
//   * Invalid: elements that are uninitialized or not actually part
//     of the virtual frame.  They should not be read.
//   * Memory: an element that resides in the actual frame.  Its address is
//     given by its position in the virtual frame.
//   * Register: an element that resides in a register.
//   * Constant: an element whose value is known at compile time.

class FrameElement BASE_EMBEDDED {
 public:
  enum SyncFlag {
    SYNCED,
    NOT_SYNCED
  };

  // The default constructor creates an invalid frame element.
  FrameElement() {
    Initialize(INVALID, no_reg, NOT_SYNCED);
  }

  // Factory function to construct an invalid frame element.
  static FrameElement InvalidElement() {
    FrameElement result;
    return result;
  }

  // Factory function to construct an in-memory frame element.
  static FrameElement MemoryElement() {
    FrameElement result(MEMORY, no_reg, SYNCED);
    return result;
  }

  // Factory function to construct an in-register frame element.
  static FrameElement RegisterElement(Register reg, SyncFlag is_synced) {
    FrameElement result(REGISTER, reg, is_synced);
    return result;
  }

  // Factory function to construct a frame element whose value is known at
  // compile time.
  static FrameElement ConstantElement(Handle<Object> value,
                                      SyncFlag is_synced) {
    FrameElement result(value, is_synced);
    return result;
  }

  bool is_synced() const { return SyncField::decode(type_) == SYNCED; }

  void set_sync() {
    ASSERT(type() != MEMORY);
    type_ = (type_ & ~SyncField::mask()) | SyncField::encode(SYNCED);
  }

  void clear_sync() {
    ASSERT(type() != MEMORY);
    type_ = (type_ & ~SyncField::mask()) | SyncField::encode(NOT_SYNCED);
  }

  bool is_valid() const { return type() != INVALID; }
  bool is_memory() const { return type() == MEMORY; }
  bool is_register() const { return type() == REGISTER; }
  bool is_constant() const { return type() == CONSTANT; }
  bool is_copy() const { return type() == COPY; }

  bool is_copied() const { return IsCopiedField::decode(type_); }

  void set_copied() {
    type_ = (type_ & ~IsCopiedField::mask()) | IsCopiedField::encode(true);
  }

  void clear_copied() {
    type_ = (type_ & ~IsCopiedField::mask()) | IsCopiedField::encode(false);
  }

  Register reg() const {
    ASSERT(is_register());
    return data_.reg_;
  }

  Handle<Object> handle() const {
    ASSERT(is_constant());
    return Handle<Object>(data_.handle_);
  }

  int index() const {
    ASSERT(is_copy());
    return data_.index_;
  }

  bool Equals(FrameElement other);

 private:
  enum Type {
    INVALID,
    MEMORY,
    REGISTER,
    CONSTANT,
    COPY
  };

  // BitField is <type, shift, size>.
  class SyncField : public BitField<SyncFlag, 0, 1> {};
  class IsCopiedField : public BitField<bool, 1, 1> {};
  class TypeField : public BitField<Type, 2, 32 - 2> {};

  Type type() const { return TypeField::decode(type_); }

  // The element's type and a dirty bit.  The dirty bit can be cleared
  // for non-memory elements to indicate that the element agrees with
  // the value in memory in the actual frame.
  int type_;

  union {
    Register reg_;
    Object** handle_;
    int index_;
  } data_;

  // Used to construct memory and register elements.
  FrameElement(Type type, Register reg, SyncFlag is_synced) {
    Initialize(type, reg, is_synced);
  }

  // Used to construct constant elements.
  inline FrameElement(Handle<Object> value, SyncFlag is_synced);

  // Used to initialize invalid, memory, and register elements.
  inline void Initialize(Type type, Register reg, SyncFlag is_synced);

  friend class VirtualFrame;
};


} }  // namespace v8::internal

#ifdef ARM
#include "virtual-frame-arm.h"
#else  // ia32
#include "virtual-frame-ia32.h"
#endif


namespace v8 { namespace internal {

FrameElement::FrameElement(Handle<Object> value, SyncFlag is_synced) {
  type_ = TypeField::encode(CONSTANT)
          | IsCopiedField::encode(false)
          | SyncField::encode(is_synced);
  data_.handle_ = value.location();
}


void FrameElement::Initialize(Type type, Register reg, SyncFlag is_synced) {
  type_ = TypeField::encode(type)
          | IsCopiedField::encode(false)
          | SyncField::encode(is_synced);
  data_.reg_ = reg;
}


} }  // namespace v8::internal

#endif  // V8_VIRTUAL_FRAME_H_
