// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_FRAME_H_
#define V8_COMPILER_FRAME_H_

#include "src/bit-vector.h"
#include "src/frames.h"

namespace v8 {
namespace internal {
namespace compiler {

// Collects the spill slot and other frame slot requirements for a compiled
// function. Frames are usually populated by the register allocator and are used
// by Linkage to generate code for the prologue and epilogue to compiled code.
//
// Frames are divided up into four regions.
// - The first is the fixed header, which always has a constant size and can be
//   predicted before code generation begins depending on the type of code being
//   generated.
// - The second is the region for spill slots, which is immediately below the
//   fixed header and grows as the register allocator needs to spill to the
//   stack and asks the frame for more space.
// - The third region, which contains the callee-saved registers must be
//   reserved after register allocation, since its size can only be precisely
//   determined after register allocation once the number of used callee-saved
//   register is certain.
// - The fourth region is used to pass arguments to other functions. It should
//   be empty except when a call is being prepared.
//
// Every pointer in a frame has a slot id. On 32-bit platforms, doubles consume
// two slots.
//
// Stack slot indices >= 0 access the callee stack with slot 0 corresponding to
// the callee's saved return address and 1 corresponding to the saved frame
// pointer. Some frames have additional information stored in the fixed header,
// for example JSFunctions store the function context and marker in the fixed
// header, with slot index 2 corresponding to the current function context and 3
// corresponding to the frame marker/JSFunction. The frame region immediately
// below the fixed header contains spill slots starting at 4 for JsFunctions.
// The callee-saved frame region below that starts at 4+spill_slot_count_.
// Callee stack slots corresponding to parameters are accessible through
// negative slot ids.
//
// Every slot of a caller or callee frame is accessible by the register
// allocator and gap resolver with a SpillSlotOperand containing its
// corresponding slot id.
//
// Below an example JSFunction Frame with slot ids, frame regions and contents:
//
//  slot      JS frame
//       +-----------------+--------------------------------
//  -n-1 |   parameter 0   |                            ^
//       |- - - - - - - - -|                            |
//  -n   |                 |                          Caller
//  ...  |       ...       |                       frame slots
//  -2   |  parameter n-1  |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1   |   parameter n   |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | Fixed                      |
//       |- - - - - - - - -| Header <-- frame ptr       |
//   2   |     Context     |   |                        |
//       |- - - - - - - - -|   |                        |
//   3   |JSFunction/Marker|   v                        |
//       +-----------------+----                        |
//   4   |    spill 1      |   ^                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  |      ...        | Spill slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//  m+4  |    spill m      |   v                        |
//       +-----------------+----                        |
//  m+5  |  callee-saved 1 |   ^                        |
//       |- - - - - - - - -|   |                        |
//       |      ...        | Callee-saved               |
//       |- - - - - - - - -|   |                        |
// m+r+4 |  callee-saved r |   v                        |
//       +-----------------+----                        |
//       |   parameter 0   |   ^                        |
//       |- - - - - - - - -|   |                        |
//       |      ...        | Outgoing parameters        |
//       |- - - - - - - - -|   |  (for function calls)  |
//       |   parameter p   |   v                        v
//  -----+-----------------+----- <-- stack ptr -------------
//
class Frame : public ZoneObject {
 public:
  explicit Frame(int fixed_frame_size_in_slots);

  inline int GetTotalFrameSlotCount() const { return frame_slot_count_; }

  inline int GetSpToFpSlotCount() const {
    return GetTotalFrameSlotCount() -
           StandardFrameConstants::kFixedSlotCountAboveFp;
  }
  inline int GetOutgoingParameterSlotCount() const {
    return outgoing_parameter_slot_count_;
  }
  inline int GetSavedCalleeRegisterSlotCount() const {
    return callee_saved_slot_count_;
  }
  inline int GetSpillSlotCount() const { return spill_slot_count_; }

  inline void SetElidedFrameSizeInSlots(int slots) {
    DCHECK_EQ(0, callee_saved_slot_count_);
    DCHECK_EQ(0, spill_slot_count_);
    frame_slot_count_ = slots;
  }

  void SetAllocatedRegisters(BitVector* regs) {
    DCHECK(allocated_registers_ == NULL);
    allocated_registers_ = regs;
  }

  void SetAllocatedDoubleRegisters(BitVector* regs) {
    DCHECK(allocated_double_registers_ == NULL);
    allocated_double_registers_ = regs;
  }

  bool DidAllocateDoubleRegisters() const {
    return !allocated_double_registers_->IsEmpty();
  }

  void AllocateOutgoingParameterSlots(int count) {
    outgoing_parameter_slot_count_ += count;
    frame_slot_count_ += count;
  }

  void ClearOutgoingParameterSlots() {
    frame_slot_count_ -= outgoing_parameter_slot_count_;
    outgoing_parameter_slot_count_ = 0;
  }

  int AlignSavedCalleeRegisterSlots() {
    DCHECK_EQ(0, callee_saved_slot_count_);
    int delta = frame_slot_count_ & 1;
    frame_slot_count_ += delta;
    return delta;
  }

  void AllocateSavedCalleeRegisterSlots(int count) {
    DCHECK_EQ(0, outgoing_parameter_slot_count_);
    frame_slot_count_ += count;
    callee_saved_slot_count_ += count;
  }

  int AllocateSpillSlot(int width) {
    DCHECK_EQ(0, outgoing_parameter_slot_count_);
    DCHECK_EQ(0, callee_saved_slot_count_);
    int frame_slot_count_before = frame_slot_count_;
    int slot = AllocateAlignedFrameSlot(width);
    spill_slot_count_ += (frame_slot_count_ - frame_slot_count_before);
    return slot;
  }

  int ReserveSpillSlots(size_t slot_count) {
    DCHECK_EQ(0, outgoing_parameter_slot_count_);
    DCHECK_EQ(0, callee_saved_slot_count_);
    DCHECK_EQ(0, spill_slot_count_);
    spill_slot_count_ += static_cast<int>(slot_count);
    frame_slot_count_ += static_cast<int>(slot_count);
    return frame_slot_count_ - 1;
  }

  static const int kContextSlot = 2 + StandardFrameConstants::kCPSlotCount;
  static const int kJSFunctionSlot = 3 + StandardFrameConstants::kCPSlotCount;

 private:
  int AllocateAlignedFrameSlot(int width) {
    DCHECK(width == 4 || width == 8);
    // Skip one slot if necessary.
    if (width > kPointerSize) {
      DCHECK(width == kPointerSize * 2);
      frame_slot_count_++;
      frame_slot_count_ |= 1;
    }
    return frame_slot_count_++;
  }

 private:
  int frame_slot_count_;
  int outgoing_parameter_slot_count_;
  int callee_saved_slot_count_;
  int spill_slot_count_;
  BitVector* allocated_registers_;
  BitVector* allocated_double_registers_;

  DISALLOW_COPY_AND_ASSIGN(Frame);
};


// Represents an offset from either the stack pointer or frame pointer.
class FrameOffset {
 public:
  inline bool from_stack_pointer() { return (offset_ & 1) == kFromSp; }
  inline bool from_frame_pointer() { return (offset_ & 1) == kFromFp; }
  inline int offset() { return offset_ & ~1; }

  inline static FrameOffset FromStackPointer(int offset) {
    DCHECK((offset & 1) == 0);
    return FrameOffset(offset | kFromSp);
  }

  inline static FrameOffset FromFramePointer(int offset) {
    DCHECK((offset & 1) == 0);
    return FrameOffset(offset | kFromFp);
  }

 private:
  explicit FrameOffset(int offset) : offset_(offset) {}

  int offset_;  // Encodes SP or FP in the low order bit.

  static const int kFromSp = 1;
  static const int kFromFp = 0;
};
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_FRAME_H_
