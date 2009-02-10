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

#ifndef V8_REGISTER_ALLOCATOR_H_
#define V8_REGISTER_ALLOCATOR_H_

#include "macro-assembler.h"

namespace v8 { namespace internal {

// -------------------------------------------------------------------------
// Results
//
// Results encapsulate the compile-time values manipulated by the code
// generator.  They can represent registers or constants.

class Result BASE_EMBEDDED {
 public:
  enum Type {
    INVALID,
    REGISTER,
    CONSTANT
  };

  // Construct an invalid result.
  explicit Result(CodeGenerator* cgen) : type_(INVALID), cgen_(cgen) {}

  // Construct a register Result.
  Result(Register reg, CodeGenerator* cgen);

  // Construct a Result whose value is a compile-time constant.
  Result(Handle<Object> value, CodeGenerator * cgen)
      : type_(CONSTANT),
        cgen_(cgen) {
    data_.handle_ = value.location();
  }

  // The copy constructor and assignment operators could each create a new
  // register reference.
  Result(const Result& other) {
    other.CopyTo(this);
  }

  Result& operator=(const Result& other) {
    if (this != &other) {
      Unuse();
      other.CopyTo(this);
    }
    return *this;
  }

  ~Result() { Unuse(); }

  void Unuse();

  Type type() const { return type_; }

  bool is_valid() const { return type() != INVALID; }
  bool is_register() const { return type() == REGISTER; }
  bool is_constant() const { return type() == CONSTANT; }

  Register reg() const {
    ASSERT(type() == REGISTER);
    return data_.reg_;
  }

  Handle<Object> handle() const {
    ASSERT(type() == CONSTANT);
    return Handle<Object>(data_.handle_);
  }

  // Move this result to an arbitrary register.  The register is not
  // necessarily spilled from the frame or even singly-referenced outside
  // it.
  void ToRegister();

  // Move this result to a specified register.  The register is spilled from
  // the frame, and the register is singly-referenced (by this result)
  // outside the frame.
  void ToRegister(Register reg);

 private:
  Type type_;

  union {
    Register reg_;
    Object** handle_;
  } data_;

  CodeGenerator* cgen_;

  void CopyTo(Result* destination) const;
};

} }  // namespace v8::internal


#ifdef ARM
#else  // ia32
#include "register-allocator-ia32.h"
#endif

#endif  // V8_REGISTER_ALLOCATOR_H_
