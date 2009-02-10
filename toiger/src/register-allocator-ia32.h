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

#ifndef V8_REGISTER_ALLOCATOR_IA32_H_
#define V8_REGISTER_ALLOCATOR_IA32_H_

namespace v8 { namespace internal {


// -------------------------------------------------------------------------
// Register file
//
// The register file tracks reference counts for the processor registers.
// It is used by both the register allocator and the virtual frame.

class RegisterFile BASE_EMBEDDED {
 public:
  RegisterFile() { Reset(); }

  void Reset() {
    for (int i = 0; i < kNumRegisters; i++) {
      ref_counts_[i] = 0;
    }
  }

  // Predicates and accessors for the reference counts.  The versions
  // that take a register code rather than a register are for
  // convenience in loops over the register codes.
  bool is_used(int reg_code) const { return ref_counts_[reg_code] > 0; }
  bool is_used(Register reg) const { return is_used(reg.code()); }
  int count(int reg_code) const { return ref_counts_[reg_code]; }
  int count(Register reg) const { return count(reg.code()); }

  // Record a use of a register by incrementing its reference count.
  void Use(Register reg) {
    ref_counts_[reg.code()]++;
  }

  // Record that a register will no longer be used by decrementing its
  // reference count.
  void Unuse(Register reg) {
    ASSERT(is_used(reg.code()));
    if (is_used(reg.code())) {
      ref_counts_[reg.code()]--;
    }
  }

  // Copy the reference counts from this register file to the other.
  void CopyTo(RegisterFile* other);

  static const int kNumRegisters = 8;

 private:
  int ref_counts_[kNumRegisters];
};


// -------------------------------------------------------------------------
// Register allocator
//

class RegisterAllocator BASE_EMBEDDED {
 public:
  explicit RegisterAllocator(CodeGenerator* cgen) : cgen_(cgen) {}

  static RegisterFile Reserved() {
    RegisterFile reserved;
    reserved.Use(esi);
    reserved.Use(ebp);
    reserved.Use(esp);
    return reserved;
  }

  int num_registers() const { return RegisterFile::kNumRegisters; }

  // Predicates and accessors for the registers' reference counts.
  bool is_used(int reg_code) const { return registers_.is_used(reg_code); }
  bool is_used(Register reg) const { return registers_.is_used(reg.code()); }
  int count(int reg_code) const { return registers_.count(reg_code); }
  int count(Register reg) const { return registers_.count(reg.code()); }

  // Explicitly record a reference to a register.
  void Use(Register reg) { registers_.Use(reg); }

  // Explicitly record that a register will no longer be used.
  void Unuse(Register reg) { registers_.Unuse(reg); }

  // Initialize the register allocator for entry to a JS function.  On
  // entry, esp, ebp, esi, and edi are externally referenced (ie, outside
  // the virtual frame); and the other registers are free.
  void Initialize();

  // Reset the register reference counts to free all non-reserved registers.
  // A frame-external reference is kept to each of the reserved registers
  // (esp, ebp, and esi).
  void Reset();

  // Allocate a free register and return a register result if possible or
  // fail and return an invalid result.
  Result Allocate();

  // Allocate a specific register if possible, spilling it from the frame if
  // necessary, or else fail and return an invalid result.
  Result Allocate(Register target);

  // Allocate a free register without spilling any from the current frame or
  // fail and return an invalid result.
  Result AllocateWithoutSpilling();

  // Allocate a free byte register without spilling any from the
  // current frame or fail and return an invalid result.
  Result AllocateByteRegisterWithoutSpilling();

  // Copy the internal state to a register file, to be restored later by
  // RestoreFrom.
  void SaveTo(RegisterFile* register_file) {
    registers_.CopyTo(register_file);
  }

  void RestoreFrom(RegisterFile* register_file) {
    register_file->CopyTo(&registers_);
  }

 private:
  CodeGenerator* cgen_;
  RegisterFile registers_;
};

} }  // namespace v8::internal

#endif  // V8_REGISTER_ALLOCATOR_IA32_H_
