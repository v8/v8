// Copyright (c) 1994-2006 Sun Microsystems Inc.
// All Rights Reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// - Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// - Redistribution in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// - Neither the name of Sun Microsystems or the names of contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// The original source code covered by the above license above has been
// modified significantly by Google Inc.
// Copyright 2006-2008 the V8 project authors. All rights reserved.

#ifndef V8_ASSEMBLER_H_
#define V8_ASSEMBLER_H_

#include "runtime.h"
#include "top.h"
#include "zone-inl.h"

namespace v8 { namespace internal {


// -----------------------------------------------------------------------------
// Labels represent pc locations; they are typically jump or call targets.
// After declaration, a label can be freely used to denote known or (yet)
// unknown pc location. Assembler::bind() is used to bind a label to the
// current pc. A label can be bound only once.

class Label : public ZoneObject {  // ShadowLables are dynamically allocated.
 public:
  INLINE(Label())                 { Unuse(); }
  INLINE(~Label())                { ASSERT(!is_linked()); }

  INLINE(void Unuse())            { pos_ = 0; }

  INLINE(bool is_bound()  const)  { return pos_ <  0; }
  INLINE(bool is_unused() const)  { return pos_ == 0; }
  INLINE(bool is_linked() const)  { return pos_ >  0; }

  // Returns the position of bound or linked labels. Cannot be used
  // for unused labels.
  int pos() const;

 private:
  // pos_ encodes both the binding state (via its sign)
  // and the binding position (via its value) of a label.
  //
  // pos_ <  0  bound label, pos() returns the jump target position
  // pos_ == 0  unused label
  // pos_ >  0  linked label, pos() returns the last reference position
  int pos_;

  void bind_to(int pos)  {
    pos_ = -pos - 1;
    ASSERT(is_bound());
  }
  void link_to(int pos)  {
    pos_ =  pos + 1;
    ASSERT(is_linked());
  }

  friend class Assembler;
  friend class Displacement;
  friend class LabelShadow;
};


// A LabelShadow is a label that temporarily shadows another label. It
// is used to catch linking and binding of labels in certain scopes,
// e.g. try blocks. LabelShadows are themselves labels which can be
// used (only) after they are not shadowing anymore.
class LabelShadow: public Label {
 public:
  explicit LabelShadow(Label* shadowed) {
    ASSERT(shadowed != NULL);
    shadowed_ = shadowed;
    shadowed_pos_ = shadowed->pos_;
    shadowed->Unuse();
#ifdef DEBUG
    is_shadowing_ = true;
#endif
  }

  ~LabelShadow() {
    ASSERT(!is_shadowing_);
  }

  void StopShadowing() {
    ASSERT(is_shadowing_ && is_unused());
    pos_ = shadowed_->pos_;
    shadowed_->pos_ = shadowed_pos_;
#ifdef DEBUG
    is_shadowing_ = false;
#endif
  }

  Label* shadowed() const { return shadowed_; }

 private:
  Label* shadowed_;
  int shadowed_pos_;
#ifdef DEBUG
  bool is_shadowing_;
#endif
};


// -----------------------------------------------------------------------------
// Relocation information

// The constant kNoPosition is used with the collecting of source positions
// in the relocation information. Two types of source positions are collected
// "position" (RelocMode position) and "statement position" (RelocMode
// statement_position). The "position" is collected at places in the source
// code which are of interest when making stack traces to pin-point the source
// location of a stack frame as close as possible. The "statement position" is
// collected at the beginning at each statement, and is used to indicate
// possible break locations. kNoPosition is used to indicate an
// invalid/uninitialized position value.
static const int kNoPosition = -1;


enum RelocMode {
  // Please note the order is important (see is_code_target, is_gc_reloc_mode).
  js_construct_call,   // code target that is an exit JavaScript frame stub.
  exit_js_frame,       // code target that is an exit JavaScript frame stub.
  code_target_context,  // code target used for contextual loads.
  code_target,         // code target which is not any of the above.
  embedded_object,
  embedded_string,

  // Everything after runtime_entry (inclusive) is not GC'ed.
  runtime_entry,
  js_return,  // Marks start of the ExitJSFrame code.
  comment,
  position,  // See comment for kNoPosition above.
  statement_position,  // See comment for kNoPosition above.
  external_reference,  // The address of an external C++ function.
  // add more as needed
  no_reloc,  // never recorded

  // Pseudo-types
  reloc_mode_count,
  last_code_enum = code_target,
  last_gced_enum = embedded_string
};


inline int RelocMask(RelocMode mode) {
  return 1 << mode;
}


inline bool is_js_construct_call(RelocMode mode) {
  return mode == js_construct_call;
}


inline bool is_exit_js_frame(RelocMode mode) {
  return mode == exit_js_frame;
}


inline bool is_code_target(RelocMode mode) {
  return mode <= last_code_enum;
}


// Is the relocation mode affected by GC?
inline bool is_gc_reloc_mode(RelocMode mode) {
  return mode <= last_gced_enum;
}


inline bool is_js_return(RelocMode mode) {
  return mode == js_return;
}


inline bool is_comment(RelocMode mode) {
  return mode == comment;
}


inline bool is_position(RelocMode mode) {
  return mode == position || mode == statement_position;
}


inline bool is_statement_position(RelocMode mode) {
  return mode == statement_position;
}

inline bool is_external_reference(RelocMode mode) {
  return mode == external_reference;
}

// Relocation information consists of the address (pc) of the datum
// to which the relocation information applies, the relocation mode
// (rmode), and an optional data field. The relocation mode may be
// "descriptive" and not indicate a need for relocation, but simply
// describe a property of the datum. Such rmodes are useful for GC
// and nice disassembly output.

class RelocInfo BASE_EMBEDDED {
 public:
  RelocInfo() {}
  RelocInfo(byte* pc, RelocMode rmode, intptr_t data)
      : pc_(pc), rmode_(rmode), data_(data) {
  }

  // Accessors
  byte* pc() const  { return pc_; }
  void set_pc(byte* pc) { pc_ = pc; }
  RelocMode rmode() const {  return rmode_; }
  intptr_t data() const  { return data_; }

  // Apply a relocation by delta bytes
  INLINE(void apply(int delta));

  // Read/modify the code target in the branch/call instruction this relocation
  // applies to; can only be called if this->is_code_target(rmode_)
  INLINE(Address target_address());
  INLINE(void set_target_address(Address target));
  INLINE(Object* target_object());
  INLINE(Object** target_object_address());
  INLINE(void set_target_object(Object* target));

  // Read/modify the reference in the instruction this relocation
  // applies to; can only be called if rmode_ is external_reference
  INLINE(Address* target_reference_address());

  // Read/modify the address of a call instruction. This is used to relocate
  // the break points where straight-line code is patched with a call
  // instruction.
  INLINE(Address call_address());
  INLINE(void set_call_address(Address target));
  INLINE(Object* call_object());
  INLINE(Object** call_object_address());
  INLINE(void set_call_object(Object* target));

  // Patch the code with some other code.
  void patch_code(byte* instructions, int instruction_count);

  // Patch the code with a call.
  void patch_code_with_call(Address target, int guard_bytes);
  INLINE(bool is_call_instruction());

#ifdef ENABLE_DISASSEMBLER
  // Printing
  static const char* RelocModeName(RelocMode rmode);
  void Print();
#endif  // ENABLE_DISASSEMBLER
#ifdef DEBUG
  // Debugging
  void Verify();
#endif

  static const int kCodeTargetMask = (1 << (last_code_enum + 1)) - 1;
  static const int kPositionMask = 1 << position | 1 << statement_position;
  static const int kDebugMask = kPositionMask | 1 << comment;
  static const int kApplyMask;  // Modes affected by apply. Depends on arch.

 private:
  // On ARM, note that pc_ is the address of the constant pool entry
  // to be relocated and not the address of the instruction
  // referencing the constant pool entry (except when rmode_ ==
  // comment).
  byte* pc_;
  RelocMode rmode_;
  intptr_t data_;
  friend class RelocIterator;
};


// RelocInfoWriter serializes a stream of relocation info. It writes towards
// lower addresses.
class RelocInfoWriter BASE_EMBEDDED {
 public:
  RelocInfoWriter() : pos_(NULL), last_pc_(NULL), last_data_(0) {}
  RelocInfoWriter(byte* pos, byte* pc) : pos_(pos), last_pc_(pc),
                                         last_data_(0) {}

  byte* pos() const { return pos_; }
  byte* last_pc() const { return last_pc_; }

  void Write(const RelocInfo* rinfo);

  // Update the state of the stream after reloc info buffer
  // and/or code is moved while the stream is active.
  void Reposition(byte* pos, byte* pc) {
    pos_ = pos;
    last_pc_ = pc;
  }

  // Max size (bytes) of a written RelocInfo.
  static const int kMaxSize = 12;

 private:
  inline uint32_t WriteVariableLengthPCJump(uint32_t pc_delta);
  inline void WriteTaggedPC(uint32_t pc_delta, int tag);
  inline void WriteExtraTaggedPC(uint32_t pc_delta, int extra_tag);
  inline void WriteExtraTaggedData(int32_t data_delta, int top_tag);
  inline void WriteTaggedData(int32_t data_delta, int tag);
  inline void WriteExtraTag(int extra_tag, int top_tag);

  byte* pos_;
  byte* last_pc_;
  intptr_t last_data_;
  DISALLOW_COPY_AND_ASSIGN(RelocInfoWriter);
};


// A RelocIterator iterates over relocation information.
// Typical use:
//
//   for (RelocIterator it(code); !it.done(); it.next()) {
//     // do something with it.rinfo() here
//   }
//
// A mask can be specified to skip unwanted modes.
class RelocIterator: public Malloced {
 public:
  // Create a new iterator positioned at
  // the beginning of the reloc info.
  // Relocation information with mode k is included in the
  // iteration iff bit k of mode_mask is set.
  explicit RelocIterator(Code* code, int mode_mask = -1);
  explicit RelocIterator(const CodeDesc& desc, int mode_mask = -1);

  // Iteration
  bool done() const  { return done_; }
  void next();

  // Return pointer valid until next next().
  RelocInfo* rinfo() {
    ASSERT(!done());
    return &rinfo_;
  }

 private:
  // Advance* moves the position before/after reading.
  // *Read* reads from current byte(s) into rinfo_.
  // *Get* just reads and returns info on current byte.
  void Advance(int bytes = 1) { pos_ -= bytes; }
  int AdvanceGetTag();
  int GetExtraTag();
  int GetTopTag();
  void ReadTaggedPC();
  void AdvanceReadPC();
  void AdvanceReadData();
  void AdvanceReadVariableLengthPCJump();
  int GetPositionTypeTag();
  void ReadTaggedData();

  static RelocMode DebugInfoModeFromTag(int tag);

  // If the given mode is wanted, set it in rinfo_ and return true.
  // Else return false. Used for efficiently skipping unwanted modes.
  bool SetMode(RelocMode mode) {
    return (mode_mask_ & 1 << mode) ? (rinfo_.rmode_ = mode, true) : false;
  }

  byte* pos_;
  byte* end_;
  RelocInfo rinfo_;
  bool done_;
  int mode_mask_;
  DISALLOW_COPY_AND_ASSIGN(RelocIterator);
};


//------------------------------------------------------------------------------
// External function

//----------------------------------------------------------------------------
class IC_Utility;
class Debug_Address;
class SCTableReference;

// An ExternalReference represents a C++ address called from the generated
// code. All references to C++ functions and must be encapsulated in an
// ExternalReference instance. This is done in order to track the origin of
// all external references in the code.
class ExternalReference BASE_EMBEDDED {
 public:
  explicit ExternalReference(Builtins::CFunctionId id);

  explicit ExternalReference(Builtins::Name name);

  explicit ExternalReference(Runtime::FunctionId id);

  explicit ExternalReference(Runtime::Function* f);

  explicit ExternalReference(const IC_Utility& ic_utility);

  explicit ExternalReference(const Debug_Address& debug_address);

  explicit ExternalReference(StatsCounter* counter);

  explicit ExternalReference(Top::AddressId id);

  explicit ExternalReference(const SCTableReference& table_ref);

  // One-of-a-kind references. These references are not part of a general
  // pattern. This means that they have to be added to the
  // ExternalReferenceTable in serialize.cc manually.

  static ExternalReference builtin_passed_function();

  // Static variable Factory::the_hole_value.location()
  static ExternalReference the_hole_value_location();

  // Static variable StackGuard::address_of_limit()
  static ExternalReference address_of_stack_guard_limit();

  // Function Debug::Break()
  static ExternalReference debug_break();

  // Static variable Heap::NewSpaceStart()
  static ExternalReference new_space_start();

  // Used for fast allocation in generated code.
  static ExternalReference new_space_allocation_top_address();
  static ExternalReference new_space_allocation_limit_address();

  // Used to check if single stepping is enabled in generated code.
  static ExternalReference debug_step_in_fp_address();

  Address address() const {return address_;}

 private:
  explicit ExternalReference(void* address)
    : address_(reinterpret_cast<Address>(address)) {}

  Address address_;
};


// -----------------------------------------------------------------------------
// Utility functions

// Move these into inline file?

static inline bool is_intn(int x, int n)  {
  return -(1 << (n-1)) <= x && x < (1 << (n-1));
}

static inline bool is_int24(int x)  { return is_intn(x, 24); }
static inline bool is_int8(int x)  { return is_intn(x, 8); }

static inline bool is_uintn(int x, int n) {
  return (x & -(1 << n)) == 0;
}

static inline bool is_uint3(int x)  { return is_uintn(x, 3); }
static inline bool is_uint4(int x)  { return is_uintn(x, 4); }
static inline bool is_uint5(int x)  { return is_uintn(x, 5); }
static inline bool is_uint8(int x)  { return is_uintn(x, 8); }
static inline bool is_uint12(int x)  { return is_uintn(x, 12); }
static inline bool is_uint16(int x)  { return is_uintn(x, 16); }
static inline bool is_uint24(int x)  { return is_uintn(x, 24); }

} }  // namespace v8::internal

#endif  // V8_ASSEMBLER_H_
