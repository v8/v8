// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_BYTECODE_REGISTER_ALLOCATOR_H_
#define V8_INTERPRETER_BYTECODE_REGISTER_ALLOCATOR_H_

#include "src/interpreter/bytecode-register.h"
#include "src/interpreter/bytecodes.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace interpreter {

class RegisterList {
 public:
  RegisterList() : first_reg_index_(Register().index()), register_count_(0) {}
  RegisterList(int first_reg_index, int register_count)
      : first_reg_index_(first_reg_index), register_count_(register_count) {}

  // Returns a new RegisterList which is a truncated version of this list, with
  // |count| registers.
  const RegisterList Truncate(int new_count) {
    DCHECK_GE(new_count, 0);
    DCHECK_LT(new_count, register_count_);
    return RegisterList(first_reg_index_, new_count);
  }

  const Register operator[](size_t i) const {
    DCHECK_LT(static_cast<int>(i), register_count_);
    return Register(first_reg_index_ + static_cast<int>(i));
  }

  const Register first_register() const {
    return (register_count() == 0) ? Register(0) : (*this)[0];
  }

  int register_count() const { return register_count_; }

 private:
  int first_reg_index_;
  int register_count_;
};

// A class that allows the allocation of contiguous temporary registers.
class BytecodeRegisterAllocator final {
 public:
  // Enables observation of register allocation and free events.
  class Observer {
   public:
    virtual ~Observer() {}
    virtual void RegisterAllocateEvent(Register reg) = 0;
    virtual void RegisterListAllocateEvent(RegisterList reg_list) = 0;
    virtual void RegisterListFreeEvent(RegisterList reg_list) = 0;
  };

  explicit BytecodeRegisterAllocator(int start_index)
      : next_register_index_(start_index),
        max_register_count_(start_index),
        observer_(nullptr) {}
  ~BytecodeRegisterAllocator() {}

  // Returns a new register.
  Register NewRegister() {
    Register reg(next_register_index_++);
    max_register_count_ = std::max(next_register_index_, max_register_count_);
    if (observer_) {
      observer_->RegisterAllocateEvent(reg);
    }
    return reg;
  }

  // Returns a consecutive list of |count| new registers.
  RegisterList NewRegisterList(int count) {
    RegisterList reg_list(next_register_index_, count);
    next_register_index_ += count;
    max_register_count_ = std::max(next_register_index_, max_register_count_);
    if (observer_) {
      observer_->RegisterListAllocateEvent(reg_list);
    }
    return reg_list;
  }

  // Release all registers above |register_index|.
  void ReleaseRegisters(int register_index) {
    if (observer_) {
      observer_->RegisterListFreeEvent(
          RegisterList(register_index, next_register_index_ - register_index));
    }
    next_register_index_ = register_index;
  }

  // Returns true if the register |reg| is a live register.
  bool RegisterIsLive(Register reg) const {
    return reg.index() < next_register_index_;
  }

  void set_observer(Observer* observer) { observer_ = observer; }

  int next_register_index() const { return next_register_index_; }
  int maximum_register_count() const { return max_register_count_; }

 private:
  int next_register_index_;
  int max_register_count_;
  Observer* observer_;

  DISALLOW_COPY_AND_ASSIGN(BytecodeRegisterAllocator);
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8


#endif  // V8_INTERPRETER_BYTECODE_REGISTER_ALLOCATOR_H_
