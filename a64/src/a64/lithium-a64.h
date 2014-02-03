// Copyright 2013 the V8 project authors. All rights reserved.
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

#ifndef V8_A64_LITHIUM_A64_H_
#define V8_A64_LITHIUM_A64_H_

#include "hydrogen.h"
#include "lithium-allocator.h"
#include "lithium.h"
#include "safepoint-table.h"
#include "utils.h"

namespace v8 {
namespace internal {

// Forward declarations.
class LCodeGen;

#define LITHIUM_CONCRETE_INSTRUCTION_LIST(V)    \
  V(AccessArgumentsAt)                          \
  V(AddI)                                       \
  V(AddS)                                       \
  V(Allocate)                                   \
  V(ApplyArguments)                             \
  V(ArgumentsElements)                          \
  V(ArgumentsLength)                            \
  V(ArithmeticD)                                \
  V(ArithmeticT)                                \
  V(BitI)                                       \
  V(BitS)                                       \
  V(BoundsCheck)                                \
  V(Branch)                                     \
  V(CallConstantFunction)                       \
  V(CallFunction)                               \
  V(CallGlobal)                                 \
  V(CallKeyed)                                  \
  V(CallKnownGlobal)                            \
  V(CallNamed)                                  \
  V(CallNew)                                    \
  V(CallNewArray)                               \
  V(CallRuntime)                                \
  V(CallStub)                                   \
  V(CheckInstanceType)                          \
  V(CheckMaps)                                  \
  V(CheckMapValue)                              \
  V(CheckNonSmi)                                \
  V(CheckSmi)                                   \
  V(CheckValue)                                 \
  V(ClampDToUint8)                              \
  V(ClampIToUint8)                              \
  V(ClampTToUint8)                              \
  V(ClassOfTestAndBranch)                       \
  V(CmpHoleAndBranchD)                          \
  V(CmpHoleAndBranchT)                          \
  V(CmpMapAndBranch)                            \
  V(CmpObjectEqAndBranch)                       \
  V(CmpT)                                       \
  V(CompareNumericAndBranch)                    \
  V(ConstantD)                                  \
  V(ConstantE)                                  \
  V(ConstantI)                                  \
  V(ConstantS)                                  \
  V(ConstantT)                                  \
  V(Context)                                    \
  V(DateField)                                  \
  V(DebugBreak)                                 \
  V(DeclareGlobals)                             \
  V(Deoptimize)                                 \
  V(DivI)                                       \
  V(DoubleToIntOrSmi)                           \
  V(Drop)                                       \
  V(DummyUse)                                   \
  V(ElementsKind)                               \
  V(ForInCacheArray)                            \
  V(ForInPrepareMap)                            \
  V(FunctionLiteral)                            \
  V(GetCachedArrayIndex)                        \
  V(GlobalObject)                               \
  V(GlobalReceiver)                             \
  V(Goto)                                       \
  V(HasCachedArrayIndexAndBranch)               \
  V(HasInstanceTypeAndBranch)                   \
  V(InnerAllocatedObject)                       \
  V(InstanceOf)                                 \
  V(InstanceOfKnownGlobal)                      \
  V(InstanceSize)                               \
  V(InstructionGap)                             \
  V(Integer32ToDouble)                          \
  V(Integer32ToSmi)                             \
  V(InvokeFunction)                             \
  V(IsConstructCallAndBranch)                   \
  V(IsNumberAndBranch)                          \
  V(IsObjectAndBranch)                          \
  V(IsSmiAndBranch)                             \
  V(IsStringAndBranch)                          \
  V(IsUndetectableAndBranch)                    \
  V(Label)                                      \
  V(LazyBailout)                                \
  V(LoadContextSlot)                            \
  V(LoadExternalArrayPointer)                   \
  V(LoadFieldByIndex)                           \
  V(LoadFunctionPrototype)                      \
  V(LoadGlobalCell)                             \
  V(LoadGlobalGeneric)                          \
  V(LoadKeyedExternal)                          \
  V(LoadKeyedFixed)                             \
  V(LoadKeyedFixedDouble)                       \
  V(LoadKeyedGeneric)                           \
  V(LoadNamedField)                             \
  V(LoadNamedGeneric)                           \
  V(MapEnumLength)                              \
  V(MathAbs)                                    \
  V(MathAbsTagged)                              \
  V(MathCos)                                    \
  V(MathExp)                                    \
  V(MathFloor)                                  \
  V(MathFloorOfDiv)                             \
  V(MathLog)                                    \
  V(MathMinMax)                                 \
  V(MathPowHalf)                                \
  V(MathRound)                                  \
  V(MathSin)                                    \
  V(MathSqrt)                                   \
  V(MathTan)                                    \
  V(ModI)                                       \
  V(MulConstIS)                                  \
  V(MulI)                                       \
  V(MulS)                                       \
  V(NumberTagD)                                 \
  V(NumberTagU)                                 \
  V(NumberUntagD)                               \
  V(OsrEntry)                                   \
  V(OuterContext)                               \
  V(Parameter)                                  \
  V(Power)                                      \
  V(PushArgument)                               \
  V(Random)                                     \
  V(RegExpLiteral)                              \
  V(Return)                                     \
  V(SeqStringSetChar)                           \
  V(ShiftI)                                     \
  V(SmiTag)                                     \
  V(SmiUntag)                                   \
  V(StackCheck)                                 \
  V(StoreCodeEntry)                             \
  V(StoreContextSlot)                           \
  V(StoreGlobalCell)                            \
  V(StoreGlobalGeneric)                         \
  V(StoreKeyedExternal)                         \
  V(StoreKeyedFixed)                            \
  V(StoreKeyedFixedDouble)                      \
  V(StoreKeyedGeneric)                          \
  V(StoreNamedField)                            \
  V(StoreNamedGeneric)                          \
  V(StringAdd)                                  \
  V(StringCharCodeAt)                           \
  V(StringCharFromCode)                         \
  V(StringCompareAndBranch)                     \
  V(SubI)                                       \
  V(SubS)                                       \
  V(TaggedToI)                                  \
  V(ThisFunction)                               \
  V(Throw)                                      \
  V(ToFastProperties)                           \
  V(TransitionElementsKind)                     \
  V(TrapAllocationMemento)                      \
  V(TruncateDoubleToIntOrSmi)                   \
  V(Typeof)                                     \
  V(TypeofIsAndBranch)                          \
  V(Uint32ToDouble)                             \
  V(UnknownOSRValue)                            \
  V(ValueOf)                                    \
  V(WrapReceiver)


#define DECLARE_CONCRETE_INSTRUCTION(type, mnemonic)              \
  virtual Opcode opcode() const { return LInstruction::k##type; } \
  virtual void CompileToNative(LCodeGen* generator);              \
  virtual const char* Mnemonic() const { return mnemonic; }       \
  static L##type* cast(LInstruction* instr) {                     \
    ASSERT(instr->Is##type());                                    \
    return reinterpret_cast<L##type*>(instr);                     \
  }


#define DECLARE_HYDROGEN_ACCESSOR(type)           \
  H##type* hydrogen() const {                     \
    return H##type::cast(this->hydrogen_value()); \
  }


class LInstruction: public ZoneObject {
 public:
  LInstruction()
      : environment_(NULL),
        hydrogen_value_(NULL),
        bit_field_(IsCallBits::encode(false)) {
    set_position(RelocInfo::kNoPosition);
  }

  virtual ~LInstruction() { }

  virtual void CompileToNative(LCodeGen* generator) = 0;
  virtual const char* Mnemonic() const = 0;
  virtual void PrintTo(StringStream* stream);
  virtual void PrintDataTo(StringStream* stream);
  virtual void PrintOutputOperandTo(StringStream* stream);

  enum Opcode {
    // Declare a unique enum value for each instruction.
#define DECLARE_OPCODE(type) k##type,
    LITHIUM_CONCRETE_INSTRUCTION_LIST(DECLARE_OPCODE)
    kNumberOfInstructions
#undef DECLARE_OPCODE
  };

  virtual Opcode opcode() const = 0;

  // Declare non-virtual type testers for all leaf IR classes.
#define DECLARE_PREDICATE(type) \
  bool Is##type() const { return opcode() == k##type; }
  LITHIUM_CONCRETE_INSTRUCTION_LIST(DECLARE_PREDICATE)
#undef DECLARE_PREDICATE

  // Declare virtual predicates for instructions that don't have
  // an opcode.
  virtual bool IsGap() const { return false; }

  virtual bool IsControl() const { return false; }

  void set_environment(LEnvironment* env) { environment_ = env; }
  LEnvironment* environment() const { return environment_; }
  bool HasEnvironment() const { return environment_ != NULL; }

  void set_pointer_map(LPointerMap* p) { pointer_map_.set(p); }
  LPointerMap* pointer_map() const { return pointer_map_.get(); }
  bool HasPointerMap() const { return pointer_map_.is_set(); }

  // The 31 bits PositionBits is used to store the int position value. And the
  // position value may be RelocInfo::kNoPosition (-1). The accessor always
  // +1/-1 so that the encoded value of position in bit_field_ is always >= 0
  // and can fit into the 31 bits PositionBits.
  void set_position(int32_t pos) {
    bit_field_ = PositionBits::update(bit_field_, pos + 1);
  }
  int32_t position() { return PositionBits::decode(bit_field_) - 1; }

  void set_hydrogen_value(HValue* value) { hydrogen_value_ = value; }
  HValue* hydrogen_value() const { return hydrogen_value_; }

  virtual void SetDeferredLazyDeoptimizationEnvironment(LEnvironment* env) { }

  void MarkAsCall() { bit_field_ = IsCallBits::update(bit_field_, true); }
  bool IsCall() const { return IsCallBits::decode(bit_field_); }

  // Interface to the register allocator and iterators.
  bool ClobbersTemps() const { return IsCall(); }
  bool ClobbersRegisters() const { return IsCall(); }
  bool ClobbersDoubleRegisters() const { return IsCall(); }
  bool IsMarkedAsCall() const { return IsCall(); }

  virtual bool HasResult() const = 0;
  virtual LOperand* result() const = 0;

  virtual int InputCount() = 0;
  virtual LOperand* InputAt(int i) = 0;
  virtual int TempCount() = 0;
  virtual LOperand* TempAt(int i) = 0;

  LOperand* FirstInput() { return InputAt(0); }
  LOperand* Output() { return HasResult() ? result() : NULL; }

  virtual bool HasInterestingComment(LCodeGen* gen) const { return true; }

#ifdef DEBUG
  void VerifyCall();
#endif

 private:
  class IsCallBits: public BitField<bool, 0, 1> {};
  class PositionBits: public BitField<int, 1, 31> {};

  LEnvironment* environment_;
  SetOncePointer<LPointerMap> pointer_map_;
  HValue* hydrogen_value_;
  int32_t bit_field_;
};


// R = number of result operands (0 or 1).
// I = number of input operands.
// T = number of temporary operands.
template<int R, int I, int T>
class LTemplateInstruction: public LInstruction {
 public:
  // Allow 0 or 1 output operands.
  STATIC_ASSERT(R == 0 || R == 1);
  virtual bool HasResult() const { return (R != 0) && (result() != NULL); }
  void set_result(LOperand* operand) { results_[0] = operand; }
  LOperand* result() const { return results_[0]; }

  int InputCount() { return I; }
  LOperand* InputAt(int i) { return inputs_[i]; }

  int TempCount() { return T; }
  LOperand* TempAt(int i) { return temps_[i]; }

 protected:
  EmbeddedContainer<LOperand*, R> results_;
  EmbeddedContainer<LOperand*, I> inputs_;
  EmbeddedContainer<LOperand*, T> temps_;
};


class LUnknownOSRValue: public LTemplateInstruction<1, 0, 0> {
 public:
  virtual bool HasInterestingComment(LCodeGen* gen) const { return false; }
  DECLARE_CONCRETE_INSTRUCTION(UnknownOSRValue, "unknown-osr-value")
};


template<int I, int T>
class LControlInstruction: public LTemplateInstruction<0, I, T> {
 public:
  LControlInstruction() : false_label_(NULL), true_label_(NULL) { }

  virtual bool IsControl() const { return true; }

  int SuccessorCount() { return hydrogen()->SuccessorCount(); }
  HBasicBlock* SuccessorAt(int i) { return hydrogen()->SuccessorAt(i); }

  int TrueDestination(LChunk* chunk) {
    return chunk->LookupDestination(true_block_id());
  }

  int FalseDestination(LChunk* chunk) {
    return chunk->LookupDestination(false_block_id());
  }

  Label* TrueLabel(LChunk* chunk) {
    if (true_label_ == NULL) {
      true_label_ = chunk->GetAssemblyLabel(TrueDestination(chunk));
    }
    return true_label_;
  }

  Label* FalseLabel(LChunk* chunk) {
    if (false_label_ == NULL) {
      false_label_ = chunk->GetAssemblyLabel(FalseDestination(chunk));
    }
    return false_label_;
  }

 protected:
  int true_block_id() { return SuccessorAt(0)->block_id(); }
  int false_block_id() { return SuccessorAt(1)->block_id(); }

 private:
  DECLARE_HYDROGEN_ACCESSOR(ControlInstruction);

  Label* false_label_;
  Label* true_label_;
};


class LGap: public LTemplateInstruction<0, 0, 0> {
 public:
  explicit LGap(HBasicBlock* block)
      : block_(block) {
    parallel_moves_[BEFORE] = NULL;
    parallel_moves_[START] = NULL;
    parallel_moves_[END] = NULL;
    parallel_moves_[AFTER] = NULL;
  }

  // Can't use the DECLARE-macro here because of sub-classes.
  virtual bool IsGap() const { return true; }
  virtual void PrintDataTo(StringStream* stream);
  static LGap* cast(LInstruction* instr) {
    ASSERT(instr->IsGap());
    return reinterpret_cast<LGap*>(instr);
  }

  bool IsRedundant() const;

  HBasicBlock* block() const { return block_; }

  enum InnerPosition {
    BEFORE,
    START,
    END,
    AFTER,
    FIRST_INNER_POSITION = BEFORE,
    LAST_INNER_POSITION = AFTER
  };

  LParallelMove* GetOrCreateParallelMove(InnerPosition pos, Zone* zone)  {
    if (parallel_moves_[pos] == NULL) {
      parallel_moves_[pos] = new(zone) LParallelMove(zone);
    }
    return parallel_moves_[pos];
  }

  LParallelMove* GetParallelMove(InnerPosition pos)  {
    return parallel_moves_[pos];
  }

 private:
  LParallelMove* parallel_moves_[LAST_INNER_POSITION + 1];
  HBasicBlock* block_;
};


class LInstanceSize: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LInstanceSize(LOperand* object) {
    inputs_[0] = object;
  }

  LOperand* object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(InstanceSize, "instance-size")
  DECLARE_HYDROGEN_ACCESSOR(InstanceSize)
};


class LInstructionGap: public LGap {
 public:
  explicit LInstructionGap(HBasicBlock* block) : LGap(block) { }

  virtual bool HasInterestingComment(LCodeGen* gen) const {
    return !IsRedundant();
  }

  DECLARE_CONCRETE_INSTRUCTION(InstructionGap, "gap")
};


class LDrop: public LTemplateInstruction<0, 0, 0> {
 public:
  explicit LDrop(int count) : count_(count) { }

  int count() const { return count_; }

  DECLARE_CONCRETE_INSTRUCTION(Drop, "drop")

 private:
  int count_;
};


class LDummyUse: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LDummyUse(LOperand* value) {
    inputs_[0] = value;
  }
  DECLARE_CONCRETE_INSTRUCTION(DummyUse, "dummy-use")
};


class LGoto: public LTemplateInstruction<0, 0, 0> {
 public:
  explicit LGoto(int block_id) : block_id_(block_id) { }

  virtual bool HasInterestingComment(LCodeGen* gen) const;
  DECLARE_CONCRETE_INSTRUCTION(Goto, "goto")
  virtual void PrintDataTo(StringStream* stream);
  virtual bool IsControl() const { return true; }

  int block_id() const { return block_id_; }

 private:
  int block_id_;
};


class LLazyBailout: public LTemplateInstruction<0, 0, 0> {
 public:
  LLazyBailout() : gap_instructions_size_(0) { }

  DECLARE_CONCRETE_INSTRUCTION(LazyBailout, "lazy-bailout")

  void set_gap_instructions_size(int gap_instructions_size) {
    gap_instructions_size_ = gap_instructions_size;
  }
  int gap_instructions_size() { return gap_instructions_size_; }

 private:
  int gap_instructions_size_;
};


class LLabel: public LGap {
 public:
  explicit LLabel(HBasicBlock* block)
      : LGap(block), replacement_(NULL) { }

  virtual bool HasInterestingComment(LCodeGen* gen) const { return false; }
  DECLARE_CONCRETE_INSTRUCTION(Label, "label")

  virtual void PrintDataTo(StringStream* stream);

  int block_id() const { return block()->block_id(); }
  bool is_loop_header() const { return block()->IsLoopHeader(); }
  bool is_osr_entry() const { return block()->is_osr_entry(); }
  Label* label() { return &label_; }
  LLabel* replacement() const { return replacement_; }
  void set_replacement(LLabel* label) { replacement_ = label; }
  bool HasReplacement() const { return replacement_ != NULL; }

 private:
  Label label_;
  LLabel* replacement_;
};


class LOsrEntry: public LTemplateInstruction<0, 0, 0> {
 public:
  LOsrEntry() {}

  virtual bool HasInterestingComment(LCodeGen* gen) const { return false; }
  DECLARE_CONCRETE_INSTRUCTION(OsrEntry, "osr-entry")
};


class LOuterContext: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LOuterContext(LOperand* context) {
    inputs_[0] = context;
  }

  LOperand* context() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(OuterContext, "outer-context")
};


class LAccessArgumentsAt: public LTemplateInstruction<1, 3, 1> {
 public:
  LAccessArgumentsAt(LOperand* arguments,
                     LOperand* length,
                     LOperand* index,
                     LOperand* temp) {
    inputs_[0] = arguments;
    inputs_[1] = length;
    inputs_[2] = index;
    temps_[0] = temp;
  }

  DECLARE_CONCRETE_INSTRUCTION(AccessArgumentsAt, "access-arguments-at")

  LOperand* arguments() { return inputs_[0]; }
  LOperand* length() { return inputs_[1]; }
  LOperand* index() { return inputs_[2]; }
  LOperand* temp() { return temps_[0]; }

  virtual void PrintDataTo(StringStream* stream);
};


class LAddI: public LTemplateInstruction<1, 2, 0> {
 public:
  LAddI(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(AddI, "add-i")
  DECLARE_HYDROGEN_ACCESSOR(Add)
};


class LAddS: public LTemplateInstruction<1, 2, 0> {
 public:
  LAddS(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(AddS, "add-s")
  DECLARE_HYDROGEN_ACCESSOR(Add)
};


class LAllocate: public LTemplateInstruction<1, 1, 2> {
 public:
  // TODO(jbramley): On ia32, this takes a context, and it is used by the
  // deferred code. On ARM, an LOperand is allocated for another input, but it
  // is never used, and the deferred code doesn't need the context. Why?
  LAllocate(LOperand* size, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = size;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* size() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(Allocate, "allocate")
  DECLARE_HYDROGEN_ACCESSOR(Allocate)
};


class LApplyArguments: public LTemplateInstruction<1, 4, 0> {
 public:
  LApplyArguments(LOperand* function,
                  LOperand* receiver,
                  LOperand* length,
                  LOperand* elements) {
    inputs_[0] = function;
    inputs_[1] = receiver;
    inputs_[2] = length;
    inputs_[3] = elements;
  }

  DECLARE_CONCRETE_INSTRUCTION(ApplyArguments, "apply-arguments")

  LOperand* function() { return inputs_[0]; }
  LOperand* receiver() { return inputs_[1]; }
  LOperand* length() { return inputs_[2]; }
  LOperand* elements() { return inputs_[3]; }
};


class LArgumentsElements: public LTemplateInstruction<1, 0, 1> {
 public:
  explicit LArgumentsElements(LOperand* temp) {
    temps_[0] = temp;
  }

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ArgumentsElements, "arguments-elements")
  DECLARE_HYDROGEN_ACCESSOR(ArgumentsElements)
};


class LArgumentsLength: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LArgumentsLength(LOperand* elements) {
    inputs_[0] = elements;
  }

  LOperand* elements() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ArgumentsLength, "arguments-length")
};


class LArithmeticD: public LTemplateInstruction<1, 2, 0> {
 public:
  LArithmeticD(Token::Value op, LOperand* left, LOperand* right)
      : op_(op) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  Token::Value op() const { return op_; }
  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  virtual Opcode opcode() const { return LInstruction::kArithmeticD; }
  virtual void CompileToNative(LCodeGen* generator);
  virtual const char* Mnemonic() const;

 private:
  Token::Value op_;
};


class LArithmeticT: public LTemplateInstruction<1, 2, 0> {
 public:
  LArithmeticT(Token::Value op, LOperand* left, LOperand* right)
      : op_(op) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }
  Token::Value op() const { return op_; }

  virtual Opcode opcode() const { return LInstruction::kArithmeticT; }
  virtual void CompileToNative(LCodeGen* generator);
  virtual const char* Mnemonic() const;

 private:
  Token::Value op_;
};


class LBoundsCheck: public LTemplateInstruction<0, 2, 0> {
 public:
  explicit LBoundsCheck(LOperand* index, LOperand* length) {
    inputs_[0] = index;
    inputs_[1] = length;
  }

  LOperand* index() { return inputs_[0]; }
  LOperand* length() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(BoundsCheck, "bounds-check")
  DECLARE_HYDROGEN_ACCESSOR(BoundsCheck)
};


class LBitI: public LTemplateInstruction<1, 2, 0> {
 public:
  LBitI(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  Token::Value op() const { return hydrogen()->op(); }

  DECLARE_CONCRETE_INSTRUCTION(BitI, "bit-i")
  DECLARE_HYDROGEN_ACCESSOR(Bitwise)
};


class LBitS: public LTemplateInstruction<1, 2, 0> {
 public:
  LBitS(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  Token::Value op() const { return hydrogen()->op(); }

  DECLARE_CONCRETE_INSTRUCTION(BitS, "bit-s")
  DECLARE_HYDROGEN_ACCESSOR(Bitwise)
};


class LBranch: public LControlInstruction<1, 2> {
 public:
  explicit LBranch(LOperand* value, LOperand *temp1, LOperand *temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(Branch, "branch")
  DECLARE_HYDROGEN_ACCESSOR(Branch)

  virtual void PrintDataTo(StringStream* stream);
};


class LCallConstantFunction: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(CallConstantFunction, "call-constant-function")
  DECLARE_HYDROGEN_ACCESSOR(CallConstantFunction)

  virtual void PrintDataTo(StringStream* stream);

  Handle<JSFunction> function() { return hydrogen()->function(); }
  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallFunction: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LCallFunction(LOperand* function) {
    inputs_[0] = function;
  }

  LOperand* function() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CallFunction, "call-function")
  DECLARE_HYDROGEN_ACCESSOR(CallFunction)

  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallKnownGlobal: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(CallKnownGlobal, "call-known-global")
  DECLARE_HYDROGEN_ACCESSOR(CallKnownGlobal)

  virtual void PrintDataTo(StringStream* stream);

  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallGlobal: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(CallGlobal, "call-global")
  DECLARE_HYDROGEN_ACCESSOR(CallGlobal)

  virtual void PrintDataTo(StringStream* stream);

  Handle<String> name() const { return hydrogen()->name(); }
  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallKeyed: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LCallKeyed(LOperand* key) {
    inputs_[0] = key;
  }

  LOperand* key() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CallKeyed, "call-keyed")
  DECLARE_HYDROGEN_ACCESSOR(CallKeyed)

  virtual void PrintDataTo(StringStream* stream);

  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallNamed: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(CallNamed, "call-named")
  DECLARE_HYDROGEN_ACCESSOR(CallNamed)

  virtual void PrintDataTo(StringStream* stream);

  Handle<String> name() const { return hydrogen()->name(); }
  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallNew: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LCallNew(LOperand* constructor) {
    inputs_[0] = constructor;
  }

  LOperand* constructor() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CallNew, "call-new")
  DECLARE_HYDROGEN_ACCESSOR(CallNew)

  virtual void PrintDataTo(StringStream* stream);

  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallNewArray: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LCallNewArray(LOperand* constructor) {
    inputs_[0] = constructor;
  }

  LOperand* constructor() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CallNewArray, "call-new-array")
  DECLARE_HYDROGEN_ACCESSOR(CallNewArray)

  virtual void PrintDataTo(StringStream* stream);

  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LCallRuntime: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(CallRuntime, "call-runtime")
  DECLARE_HYDROGEN_ACCESSOR(CallRuntime)

  const Runtime::Function* function() const { return hydrogen()->function(); }
  int arity() const { return hydrogen()->argument_count(); }
};


class LCallStub: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(CallStub, "call-stub")
  DECLARE_HYDROGEN_ACCESSOR(CallStub)

  TranscendentalCache::Type transcendental_type() {
    return hydrogen()->transcendental_type();
  }
};


class LCheckInstanceType: public LTemplateInstruction<0, 1, 1> {
 public:
  explicit LCheckInstanceType(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CheckInstanceType, "check-instance-type")
  DECLARE_HYDROGEN_ACCESSOR(CheckInstanceType)
};


class LCheckMaps: public LTemplateInstruction<0, 1, 1> {
 public:
  explicit LCheckMaps(LOperand* value, LOperand* temp = NULL) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CheckMaps, "check-maps")
  DECLARE_HYDROGEN_ACCESSOR(CheckMaps)
};


class LCheckNonSmi: public LTemplateInstruction<0, 1, 0> {
 public:
  explicit LCheckNonSmi(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CheckNonSmi, "check-non-smi")
  DECLARE_HYDROGEN_ACCESSOR(CheckHeapObject)
};


class LCheckSmi: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LCheckSmi(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CheckSmi, "check-smi")
};


class LCheckValue: public LTemplateInstruction<0, 1, 1> {
 public:
  LCheckValue(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CheckValue, "check-value")
  DECLARE_HYDROGEN_ACCESSOR(CheckValue)
};


class LClampDToUint8: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LClampDToUint8(LOperand* unclamped) {
    inputs_[0] = unclamped;
  }

  LOperand* unclamped() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ClampDToUint8, "clamp-d-to-uint8")
};


class LClampIToUint8: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LClampIToUint8(LOperand* unclamped) {
    inputs_[0] = unclamped;
  }

  LOperand* unclamped() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ClampIToUint8, "clamp-i-to-uint8")
};


class LClampTToUint8: public LTemplateInstruction<1, 1, 2> {
 public:
  LClampTToUint8(LOperand* unclamped, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = unclamped;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* unclamped() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(ClampTToUint8, "clamp-t-to-uint8")
};


class LClassOfTestAndBranch: public LControlInstruction<1, 2> {
 public:
  LClassOfTestAndBranch(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(ClassOfTestAndBranch,
                               "class-of-test-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(ClassOfTestAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LCmpHoleAndBranchD: public LControlInstruction<1, 1> {
 public:
  explicit LCmpHoleAndBranchD(LOperand* object, LOperand* temp) {
    inputs_[0] = object;
    temps_[0] = temp;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CmpHoleAndBranchD, "cmp-hole-and-branch-d")
  DECLARE_HYDROGEN_ACCESSOR(CompareHoleAndBranch)
};


class LCmpHoleAndBranchT: public LControlInstruction<1, 0> {
 public:
  explicit LCmpHoleAndBranchT(LOperand* object) {
    inputs_[0] = object;
  }

  LOperand* object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CmpHoleAndBranchT, "cmp-hole-and-branch-t")
  DECLARE_HYDROGEN_ACCESSOR(CompareHoleAndBranch)
};


class LCmpMapAndBranch: public LControlInstruction<1, 1> {
 public:
  LCmpMapAndBranch(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CmpMapAndBranch, "cmp-map-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(CompareMap)

  Handle<Map> map() const { return hydrogen()->map(); }
};


class LCmpObjectEqAndBranch: public LControlInstruction<2, 0> {
 public:
  LCmpObjectEqAndBranch(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(CmpObjectEqAndBranch, "cmp-object-eq-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(CompareObjectEqAndBranch)
};


class LCmpT: public LTemplateInstruction<1, 2, 0> {
 public:
  LCmpT(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(CmpT, "cmp-t")
  DECLARE_HYDROGEN_ACCESSOR(CompareGeneric)

  Token::Value op() const { return hydrogen()->token(); }
};


class LCompareNumericAndBranch: public LControlInstruction<2, 0> {
 public:
  LCompareNumericAndBranch(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(CompareNumericAndBranch,
                               "compare-numeric-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(CompareNumericAndBranch)

  Token::Value op() const { return hydrogen()->token(); }
  bool is_double() const {
    return hydrogen()->representation().IsDouble();
  }

  virtual void PrintDataTo(StringStream* stream);
};


class LConstantD: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(ConstantD, "constant-d")
  DECLARE_HYDROGEN_ACCESSOR(Constant)

  double value() const { return hydrogen()->DoubleValue(); }
};


class LConstantE: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(ConstantE, "constant-e")
  DECLARE_HYDROGEN_ACCESSOR(Constant)

  ExternalReference value() const {
    return hydrogen()->ExternalReferenceValue();
  }
};


class LConstantI: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(ConstantI, "constant-i")
  DECLARE_HYDROGEN_ACCESSOR(Constant)

  int32_t value() const { return hydrogen()->Integer32Value(); }
};


class LConstantS: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(ConstantS, "constant-s")
  DECLARE_HYDROGEN_ACCESSOR(Constant)

  Smi* value() const { return Smi::FromInt(hydrogen()->Integer32Value()); }
};


class LConstantT: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(ConstantT, "constant-t")
  DECLARE_HYDROGEN_ACCESSOR(Constant)

  Handle<Object> value() const { return hydrogen()->handle(); }
};


class LContext: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(Context, "context")
  DECLARE_HYDROGEN_ACCESSOR(Context)
};


class LDateField: public LTemplateInstruction<1, 1, 0> {
 public:
  LDateField(LOperand* date, Smi* index) : index_(index) {
    inputs_[0] = date;
  }

  LOperand* date() { return inputs_[0]; }
  Smi* index() const { return index_; }

  DECLARE_CONCRETE_INSTRUCTION(DateField, "date-field")
  DECLARE_HYDROGEN_ACCESSOR(DateField)

 private:
  Smi* index_;
};


class LDebugBreak: public LTemplateInstruction<0, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(DebugBreak, "break")
};


class LDeclareGlobals: public LTemplateInstruction<0, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(DeclareGlobals, "declare-globals")
  DECLARE_HYDROGEN_ACCESSOR(DeclareGlobals)
};


class LDeoptimize: public LTemplateInstruction<0, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(Deoptimize, "deoptimize")
  DECLARE_HYDROGEN_ACCESSOR(Deoptimize)
};


class LDivI: public LTemplateInstruction<1, 2, 1> {
 public:
  LDivI(LOperand* left, LOperand* right, LOperand* temp) {
    inputs_[0] = left;
    inputs_[1] = right;
    temps_[0] = temp;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(DivI, "div-i")
  DECLARE_HYDROGEN_ACCESSOR(Div)
};


class LDoubleToIntOrSmi: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LDoubleToIntOrSmi(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(DoubleToIntOrSmi, "double-to-int-or-smi")
  DECLARE_HYDROGEN_ACCESSOR(UnaryOperation)

  bool tag_result() { return hydrogen()->representation().IsSmi(); }
};


class LElementsKind: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LElementsKind(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ElementsKind, "elements-kind")
  DECLARE_HYDROGEN_ACCESSOR(ElementsKind)
};


class LForInCacheArray: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LForInCacheArray(LOperand* map) {
    inputs_[0] = map;
  }

  LOperand* map() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ForInCacheArray, "for-in-cache-array")

  int idx() {
    return HForInCacheArray::cast(this->hydrogen_value())->idx();
  }
};


class LForInPrepareMap: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LForInPrepareMap(LOperand* object) {
    inputs_[0] = object;
  }

  LOperand* object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ForInPrepareMap, "for-in-prepare-map")
};


class LGetCachedArrayIndex: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LGetCachedArrayIndex(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(GetCachedArrayIndex, "get-cached-array-index")
  DECLARE_HYDROGEN_ACCESSOR(GetCachedArrayIndex)
};


class LGlobalObject: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LGlobalObject(LOperand* context) {
    inputs_[0] = context;
  }

  DECLARE_CONCRETE_INSTRUCTION(GlobalObject, "global-object")

  LOperand* context() { return inputs_[0]; }
};


class LGlobalReceiver: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LGlobalReceiver(LOperand* global_object) {
    inputs_[0] = global_object;
  }

  LOperand* global_object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(GlobalReceiver, "global-receiver")
};


class LHasCachedArrayIndexAndBranch: public LControlInstruction<1, 1> {
 public:
  LHasCachedArrayIndexAndBranch(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(HasCachedArrayIndexAndBranch,
                               "has-cached-array-index-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(HasCachedArrayIndexAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LHasInstanceTypeAndBranch: public LControlInstruction<1, 1> {
 public:
  LHasInstanceTypeAndBranch(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(HasInstanceTypeAndBranch,
                               "has-instance-type-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(HasInstanceTypeAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LInnerAllocatedObject: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LInnerAllocatedObject(LOperand* base_object) {
    inputs_[0] = base_object;
  }

  LOperand* base_object() { return inputs_[0]; }
  int offset() { return hydrogen()->offset(); }

  virtual void PrintDataTo(StringStream* stream);

  DECLARE_CONCRETE_INSTRUCTION(InnerAllocatedObject, "sub-allocated-object")
  DECLARE_HYDROGEN_ACCESSOR(InnerAllocatedObject)
};


class LInstanceOf: public LTemplateInstruction<1, 2, 0> {
 public:
  LInstanceOf(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(InstanceOf, "instance-of")
};


class LInstanceOfKnownGlobal: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LInstanceOfKnownGlobal(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(InstanceOfKnownGlobal,
                               "instance-of-known-global")
  DECLARE_HYDROGEN_ACCESSOR(InstanceOfKnownGlobal)

  Handle<JSFunction> function() const { return hydrogen()->function(); }
  LEnvironment* GetDeferredLazyDeoptimizationEnvironment() {
    return lazy_deopt_env_;
  }
  virtual void SetDeferredLazyDeoptimizationEnvironment(LEnvironment* env) {
    lazy_deopt_env_ = env;
  }

 private:
  LEnvironment* lazy_deopt_env_;
};


class LInteger32ToDouble: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LInteger32ToDouble(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(Integer32ToDouble, "int32-to-double")
};


class LInteger32ToSmi: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LInteger32ToSmi(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(Integer32ToDouble, "int32-to-smi")
  DECLARE_HYDROGEN_ACCESSOR(Change)
};


class LInvokeFunction: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LInvokeFunction(LOperand* function) {
    inputs_[0] = function;
  }

  LOperand* function() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(InvokeFunction, "invoke-function")
  DECLARE_HYDROGEN_ACCESSOR(InvokeFunction)

  virtual void PrintDataTo(StringStream* stream);

  int arity() const { return hydrogen()->argument_count() - 1; }
};


class LIsConstructCallAndBranch: public LControlInstruction<0, 2> {
 public:
  LIsConstructCallAndBranch(LOperand* temp1, LOperand* temp2) {
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(IsConstructCallAndBranch,
                               "is-construct-call-and-branch")
};


class LIsNumberAndBranch: public LControlInstruction<1, 0> {
 public:
  explicit LIsNumberAndBranch(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(IsNumberAndBranch, "is-number-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(IsNumberAndBranch)
};


class LIsObjectAndBranch: public LControlInstruction<1, 2> {
 public:
  LIsObjectAndBranch(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(IsObjectAndBranch, "is-object-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(IsObjectAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LIsStringAndBranch: public LControlInstruction<1, 1> {
 public:
  LIsStringAndBranch(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(IsStringAndBranch, "is-string-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(IsStringAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LIsSmiAndBranch: public LControlInstruction<1, 0> {
 public:
  explicit LIsSmiAndBranch(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(IsSmiAndBranch, "is-smi-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(IsSmiAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LIsUndetectableAndBranch: public LControlInstruction<1, 1> {
 public:
  explicit LIsUndetectableAndBranch(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(IsUndetectableAndBranch,
                               "is-undetectable-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(IsUndetectableAndBranch)

  virtual void PrintDataTo(StringStream* stream);
};


class LLoadContextSlot: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LLoadContextSlot(LOperand* context) {
    inputs_[0] = context;
  }

  LOperand* context() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadContextSlot, "load-context-slot")
  DECLARE_HYDROGEN_ACCESSOR(LoadContextSlot)

  int slot_index() { return hydrogen()->slot_index(); }

  virtual void PrintDataTo(StringStream* stream);
};


class LLoadNamedField: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LLoadNamedField(LOperand* object) {
    inputs_[0] = object;
  }

  LOperand* object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadNamedField, "load-named-field")
  DECLARE_HYDROGEN_ACCESSOR(LoadNamedField)
};


class LFunctionLiteral: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(FunctionLiteral, "function-literal")
  DECLARE_HYDROGEN_ACCESSOR(FunctionLiteral)
};


class LLoadExternalArrayPointer: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LLoadExternalArrayPointer(LOperand* object) {
    inputs_[0] = object;
  }

  LOperand* object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadExternalArrayPointer,
                               "load-external-array-pointer")
};


class LLoadFunctionPrototype: public LTemplateInstruction<1, 1, 1> {
 public:
  LLoadFunctionPrototype(LOperand* function, LOperand* temp) {
    inputs_[0] = function;
    temps_[0] = temp;
  }

  LOperand* function() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadFunctionPrototype, "load-function-prototype")
  DECLARE_HYDROGEN_ACCESSOR(LoadFunctionPrototype)
};


class LLoadGlobalCell: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(LoadGlobalCell, "load-global-cell")
  DECLARE_HYDROGEN_ACCESSOR(LoadGlobalCell)
};


class LLoadGlobalGeneric: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LLoadGlobalGeneric(LOperand* global_object) {
    inputs_[0] = global_object;
  }

  LOperand* global_object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadGlobalGeneric, "load-global-generic")
  DECLARE_HYDROGEN_ACCESSOR(LoadGlobalGeneric)

  Handle<Object> name() const { return hydrogen()->name(); }
  bool for_typeof() const { return hydrogen()->for_typeof(); }
};


template<int T>
class LLoadKeyed: public LTemplateInstruction<1, 2, T> {
 public:
  LLoadKeyed(LOperand* elements, LOperand* key) {
    this->inputs_[0] = elements;
    this->inputs_[1] = key;
  }

  LOperand* elements() { return this->inputs_[0]; }
  LOperand* key() { return this->inputs_[1]; }
  ElementsKind elements_kind() const {
    return this->hydrogen()->elements_kind();
  }
  bool is_external() const {
    return this->hydrogen()->is_external();
  }
  uint32_t additional_index() const {
    return this->hydrogen()->index_offset();
  }
  void PrintDataTo(StringStream* stream) {
    this->elements()->PrintTo(stream);
    stream->Add("[");
    this->key()->PrintTo(stream);
    if (this->hydrogen()->IsDehoisted()) {
      stream->Add(" + %d]", this->additional_index());
    } else {
      stream->Add("]");
    }
  }

  DECLARE_HYDROGEN_ACCESSOR(LoadKeyed)
};


class LLoadKeyedExternal: public LLoadKeyed<1> {
 public:
  LLoadKeyedExternal(LOperand* elements, LOperand* key, LOperand* temp) :
      LLoadKeyed<1>(elements, key) {
    temps_[0] = temp;
  }

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadKeyedExternal, "load-keyed-external");
};


class LLoadKeyedFixed: public LLoadKeyed<1> {
 public:
  LLoadKeyedFixed(LOperand* elements, LOperand* key, LOperand* temp) :
      LLoadKeyed<1>(elements, key) {
    temps_[0] = temp;
  }

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadKeyedFixed, "load-keyed-fixed");
};


class LLoadKeyedFixedDouble: public LLoadKeyed<1> {
 public:
  LLoadKeyedFixedDouble(LOperand* elements, LOperand* key, LOperand* temp) :
      LLoadKeyed<1>(elements, key) {
    temps_[0] = temp;
  }

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadKeyedFixedDouble, "load-keyed-fixed-double");
};


class LLoadKeyedGeneric: public LTemplateInstruction<1, 2, 0> {
 public:
  LLoadKeyedGeneric(LOperand* object, LOperand* key) {
    inputs_[0] = object;
    inputs_[1] = key;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* key() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadKeyedGeneric, "load-keyed-generic")
};


class LLoadNamedGeneric: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LLoadNamedGeneric(LOperand* object) {
    inputs_[0] = object;
  }

  LOperand* object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadNamedGeneric, "load-named-generic")
  DECLARE_HYDROGEN_ACCESSOR(LoadNamedGeneric)

  Handle<Object> name() const { return hydrogen()->name(); }
};


class LMapEnumLength: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LMapEnumLength(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(MapEnumLength, "map-enum-length")
};


template<int T>
class LUnaryMathOperation: public LTemplateInstruction<1, 1, T> {
 public:
  explicit LUnaryMathOperation(LOperand* value) {
    this->inputs_[0] = value;
  }

  LOperand* value() { return this->inputs_[0]; }
  BuiltinFunctionId op() const { return this->hydrogen()->op(); }

  void PrintDataTo(StringStream* stream);

  DECLARE_HYDROGEN_ACCESSOR(UnaryMathOperation)
};


class LMathAbs: public LUnaryMathOperation<0> {
 public:
  explicit LMathAbs(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathAbs, "math-abs")
};


class LMathAbsTagged: public LUnaryMathOperation<3> {
 public:
  LMathAbsTagged(LOperand* value,
                      LOperand* temp1, LOperand* temp2, LOperand* temp3)
      : LUnaryMathOperation<3>(value) {
    temps_[0] = temp1;
    temps_[1] = temp2;
    temps_[2] = temp3;
  }

  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }
  LOperand* temp3() { return temps_[2]; }

  DECLARE_CONCRETE_INSTRUCTION(MathAbsTagged, "math-abs-tagged")
};


class LMathCos: public LUnaryMathOperation<0> {
 public:
  explicit LMathCos(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathCos, "math-cos")
};


class LMathExp: public LUnaryMathOperation<4> {
 public:
  LMathExp(LOperand* value,
                LOperand* double_temp1,
                LOperand* temp1,
                LOperand* temp2,
                LOperand* temp3)
      : LUnaryMathOperation<4>(value) {
    temps_[0] = double_temp1;
    temps_[1] = temp1;
    temps_[2] = temp2;
    temps_[3] = temp3;
    ExternalReference::InitializeMathExpData();
  }

  LOperand* double_temp1() { return temps_[0]; }
  LOperand* temp1() { return temps_[1]; }
  LOperand* temp2() { return temps_[2]; }
  LOperand* temp3() { return temps_[3]; }

  DECLARE_CONCRETE_INSTRUCTION(MathExp, "math-exp")
};


class LMathFloor: public LUnaryMathOperation<0> {
 public:
  explicit LMathFloor(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathFloor, "math-floor")
};


class LMathFloorOfDiv : public LTemplateInstruction<1, 2, 1> {
 public:
  LMathFloorOfDiv(LOperand* left,
                  LOperand* right,
                  LOperand* temp = NULL) {
    inputs_[0] = left;
    inputs_[1] = right;
    temps_[0] = temp;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(MathFloorOfDiv, "math-floor-of-div")
  DECLARE_HYDROGEN_ACCESSOR(MathFloorOfDiv)
};


class LMathLog: public LUnaryMathOperation<0> {
 public:
  explicit LMathLog(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathLog, "math-log")
};


class LMathMinMax: public LTemplateInstruction<1, 2, 0> {
 public:
  LMathMinMax(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(MathMinMax, "math-min-max")
  DECLARE_HYDROGEN_ACCESSOR(MathMinMax)
};


class LMathPowHalf: public LUnaryMathOperation<0> {
 public:
  explicit LMathPowHalf(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathPowHalf, "math-pow-half")
};


class LMathRound: public LUnaryMathOperation<1> {
 public:
  LMathRound(LOperand* value, LOperand* temp1)
      : LUnaryMathOperation<1>(value) {
    temps_[0] = temp1;
  }

  LOperand* temp1() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(MathRound, "math-round")
};


class LMathSin: public LUnaryMathOperation<0> {
 public:
  explicit LMathSin(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathSin, "math-sin")
};


class LMathSqrt: public LUnaryMathOperation<0> {
 public:
  explicit LMathSqrt(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathSqrt, "math-sqrt")
};


class LMathTan: public LUnaryMathOperation<0> {
 public:
  explicit LMathTan(LOperand* value) : LUnaryMathOperation<0>(value) { }
  DECLARE_CONCRETE_INSTRUCTION(MathTan, "math-tan")
};


class LModI: public LTemplateInstruction<1, 2, 0> {
 public:
  LModI(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(ModI, "mod-i")
  DECLARE_HYDROGEN_ACCESSOR(Mod)
};


class LMulConstIS: public LTemplateInstruction<1, 2, 0> {
 public:
  LMulConstIS(LOperand* left, LConstantOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LConstantOperand* right() { return LConstantOperand::cast(inputs_[1]); }

  DECLARE_CONCRETE_INSTRUCTION(MulConstIS, "mul-const-i-s")
  DECLARE_HYDROGEN_ACCESSOR(Mul)
};


class LMulI: public LTemplateInstruction<1, 2, 0> {
 public:
  LMulI(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(MulI, "mul-i")
  DECLARE_HYDROGEN_ACCESSOR(Mul)
};


class LMulS: public LTemplateInstruction<1, 2, 0> {
 public:
  LMulS(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(MulI, "mul-s")
  DECLARE_HYDROGEN_ACCESSOR(Mul)
};


class LNumberTagD: public LTemplateInstruction<1, 1, 2> {
 public:
  LNumberTagD(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(NumberTagD, "number-tag-d")
  DECLARE_HYDROGEN_ACCESSOR(Change)
};


class LNumberTagU: public LTemplateInstruction<1, 1, 2> {
 public:
  explicit LNumberTagU(LOperand* value,
                       LOperand* temp1,
                       LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(NumberTagU, "number-tag-u")
};


class LNumberUntagD: public LTemplateInstruction<1, 1, 1> {
 public:
  LNumberUntagD(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(NumberUntagD, "double-untag")
  DECLARE_HYDROGEN_ACCESSOR(Change)
};


class LParameter: public LTemplateInstruction<1, 0, 0> {
 public:
  virtual bool HasInterestingComment(LCodeGen* gen) const { return false; }
  DECLARE_CONCRETE_INSTRUCTION(Parameter, "parameter")
};


class LPower: public LTemplateInstruction<1, 2, 0> {
 public:
  LPower(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(Power, "power")
  DECLARE_HYDROGEN_ACCESSOR(Power)
};


class LPushArgument: public LTemplateInstruction<0, 1, 0> {
 public:
  explicit LPushArgument(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(PushArgument, "push-argument")
};


class LRandom: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LRandom(LOperand* global_object) {
    inputs_[0] = global_object;
  }

  LOperand* global_object() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(Random, "random")
  DECLARE_HYDROGEN_ACCESSOR(Random)
};


class LRegExpLiteral: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(RegExpLiteral, "regexp-literal")
  DECLARE_HYDROGEN_ACCESSOR(RegExpLiteral)
};


class LReturn: public LTemplateInstruction<0, 2, 0> {
 public:
  LReturn(LOperand* value, LOperand* parameter_count) {
    inputs_[0] = value;
    inputs_[1] = parameter_count;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* parameter_count() { return inputs_[1]; }

  bool has_constant_parameter_count() {
    return parameter_count()->IsConstantOperand();
  }
  LConstantOperand* constant_parameter_count() {
    ASSERT(has_constant_parameter_count());
    return LConstantOperand::cast(parameter_count());
  }

  DECLARE_CONCRETE_INSTRUCTION(Return, "return")
};


class LSeqStringSetChar: public LTemplateInstruction<1, 3, 1> {
 public:
  LSeqStringSetChar(String::Encoding encoding,
                    LOperand* string,
                    LOperand* index,
                    LOperand* value,
                    LOperand* temp) : encoding_(encoding) {
    inputs_[0] = string;
    inputs_[1] = index;
    inputs_[2] = value;
    temps_[0] = temp;
  }

  String::Encoding encoding() { return encoding_; }
  LOperand* string() { return inputs_[0]; }
  LOperand* index() { return inputs_[1]; }
  LOperand* value() { return inputs_[2]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(SeqStringSetChar, "seq-string-set-char")
  DECLARE_HYDROGEN_ACCESSOR(SeqStringSetChar)

 private:
  String::Encoding encoding_;
};


class LSmiTag: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LSmiTag(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(SmiTag, "smi-tag")
};


class LSmiUntag: public LTemplateInstruction<1, 1, 0> {
 public:
  LSmiUntag(LOperand* value, bool needs_check)
      : needs_check_(needs_check) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }
  bool needs_check() const { return needs_check_; }

  DECLARE_CONCRETE_INSTRUCTION(SmiUntag, "smi-untag")

 private:
  bool needs_check_;
};


class LStackCheck: public LTemplateInstruction<0, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(StackCheck, "stack-check")
  DECLARE_HYDROGEN_ACCESSOR(StackCheck)

  Label* done_label() { return &done_label_; }

 private:
  Label done_label_;
};


template<int T>
class LStoreKeyed: public LTemplateInstruction<0, 3, T> {
 public:
  LStoreKeyed(LOperand* elements, LOperand* key, LOperand* value) {
    this->inputs_[0] = elements;
    this->inputs_[1] = key;
    this->inputs_[2] = value;
  }

  bool is_external() const { return this->hydrogen()->is_external(); }
  LOperand* elements() { return this->inputs_[0]; }
  LOperand* key() { return this->inputs_[1]; }
  LOperand* value() { return this->inputs_[2]; }
  ElementsKind elements_kind() const {
    return this->hydrogen()->elements_kind();
  }

  bool NeedsCanonicalization() {
    return this->hydrogen()->NeedsCanonicalization();
  }
  uint32_t additional_index() const { return this->hydrogen()->index_offset(); }

  void PrintDataTo(StringStream* stream) {
    this->elements()->PrintTo(stream);
    stream->Add("[");
    this->key()->PrintTo(stream);
    if (this->hydrogen()->IsDehoisted()) {
      stream->Add(" + %d] <-", this->additional_index());
    } else {
      stream->Add("] <- ");
    }

    if (this->value() == NULL) {
      ASSERT(hydrogen()->IsConstantHoleStore() &&
             hydrogen()->value()->representation().IsDouble());
      stream->Add("<the hole(nan)>");
    } else {
      this->value()->PrintTo(stream);
    }
  }

  DECLARE_HYDROGEN_ACCESSOR(StoreKeyed)
};


class LStoreKeyedExternal: public LStoreKeyed<1> {
 public:
  LStoreKeyedExternal(LOperand* elements, LOperand* key, LOperand* value,
                      LOperand* temp) :
      LStoreKeyed<1>(elements, key, value) {
    temps_[0] = temp;
  };

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreKeyedExternal, "store-keyed-external")
};


class LStoreKeyedFixed: public LStoreKeyed<1> {
 public:
  LStoreKeyedFixed(LOperand* elements, LOperand* key, LOperand* value,
                   LOperand* temp) :
      LStoreKeyed<1>(elements, key, value) {
    temps_[0] = temp;
  };

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreKeyedFixed, "store-keyed-fixed")
};


class LStoreKeyedFixedDouble: public LStoreKeyed<1> {
 public:
  LStoreKeyedFixedDouble(LOperand* elements, LOperand* key, LOperand* value,
                         LOperand* temp) :
      LStoreKeyed<1>(elements, key, value) {
    temps_[0] = temp;
  };

  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreKeyedFixedDouble,
                               "store-keyed-fixed-double")
};


class LStoreKeyedGeneric: public LTemplateInstruction<0, 3, 0> {
 public:
  LStoreKeyedGeneric(LOperand* obj, LOperand* key, LOperand* value) {
    inputs_[0] = obj;
    inputs_[1] = key;
    inputs_[2] = value;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* key() { return inputs_[1]; }
  LOperand* value() { return inputs_[2]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreKeyedGeneric, "store-keyed-generic")
  DECLARE_HYDROGEN_ACCESSOR(StoreKeyedGeneric)

  virtual void PrintDataTo(StringStream* stream);

  StrictModeFlag strict_mode_flag() { return hydrogen()->strict_mode_flag(); }
};


class LStoreNamedField: public LTemplateInstruction<0, 2, 2> {
 public:
  LStoreNamedField(LOperand* object, LOperand* value,
                   LOperand* temp0, LOperand* temp1) {
    inputs_[0] = object;
    inputs_[1] = value;
    temps_[0] = temp0;
    temps_[1] = temp1;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* value() { return inputs_[1]; }
  LOperand* temp0() { return temps_[0]; }
  LOperand* temp1() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreNamedField, "store-named-field")
  DECLARE_HYDROGEN_ACCESSOR(StoreNamedField)

  virtual void PrintDataTo(StringStream* stream);

  Handle<Map> transition() const { return hydrogen()->transition_map(); }
  Representation representation() const {
    return hydrogen()->field_representation();
  }
};


class LStoreNamedGeneric: public LTemplateInstruction<0, 2, 0> {
 public:
  LStoreNamedGeneric(LOperand* object, LOperand* value) {
    inputs_[0] = object;
    inputs_[1] = value;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* value() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreNamedGeneric, "store-named-generic")
  DECLARE_HYDROGEN_ACCESSOR(StoreNamedGeneric)

  virtual void PrintDataTo(StringStream* stream);

  Handle<Object> name() const { return hydrogen()->name(); }
  StrictModeFlag strict_mode_flag() { return hydrogen()->strict_mode_flag(); }
};


class LStringAdd: public LTemplateInstruction<1, 2, 0> {
 public:
  LStringAdd(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StringAdd, "string-add")
  DECLARE_HYDROGEN_ACCESSOR(StringAdd)
};



class LStringCharCodeAt: public LTemplateInstruction<1, 2, 0> {
 public:
  LStringCharCodeAt(LOperand* string, LOperand* index) {
    inputs_[0] = string;
    inputs_[1] = index;
  }

  LOperand* string() { return inputs_[0]; }
  LOperand* index() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StringCharCodeAt, "string-char-code-at")
  DECLARE_HYDROGEN_ACCESSOR(StringCharCodeAt)
};


class LStringCharFromCode: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LStringCharFromCode(LOperand* char_code) {
    inputs_[0] = char_code;
  }

  LOperand* char_code() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(StringCharFromCode, "string-char-from-code")
  DECLARE_HYDROGEN_ACCESSOR(StringCharFromCode)
};


class LStringCompareAndBranch: public LControlInstruction<2, 0> {
 public:
  LStringCompareAndBranch(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StringCompareAndBranch,
                               "string-compare-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(StringCompareAndBranch)

  Token::Value op() const { return hydrogen()->token(); }

  virtual void PrintDataTo(StringStream* stream);
};


// Truncating conversion from a tagged value to an int32.
class LTaggedToI: public LTemplateInstruction<1, 1, 2> {
 public:
  explicit LTaggedToI(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(TaggedToI, "tagged-to-i")
  DECLARE_HYDROGEN_ACCESSOR(UnaryOperation)

  bool truncating() { return hydrogen()->CanTruncateToInt32(); }
};


class LShiftI: public LTemplateInstruction<1, 2, 0> {
 public:
  LShiftI(Token::Value op, LOperand* left, LOperand* right, bool can_deopt)
      : op_(op), can_deopt_(can_deopt) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  Token::Value op() const { return op_; }
  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }
  bool can_deopt() const { return can_deopt_; }

  DECLARE_CONCRETE_INSTRUCTION(ShiftI, "shift-i")

 private:
  Token::Value op_;
  bool can_deopt_;
};


class LStoreCodeEntry V8_FINAL: public LTemplateInstruction<0, 2, 1> {
 public:
  LStoreCodeEntry(LOperand* function, LOperand* code_object,
                  LOperand* temp) {
    inputs_[0] = function;
    inputs_[1] = code_object;
    temps_[0] = temp;
  }

  LOperand* function() { return inputs_[0]; }
  LOperand* code_object() { return inputs_[1]; }
  LOperand* temp() { return temps_[0]; }

  virtual void PrintDataTo(StringStream* stream) V8_OVERRIDE;

  DECLARE_CONCRETE_INSTRUCTION(StoreCodeEntry, "store-code-entry")
  DECLARE_HYDROGEN_ACCESSOR(StoreCodeEntry)
};


class LStoreContextSlot: public LTemplateInstruction<0, 2, 1> {
 public:
  LStoreContextSlot(LOperand* context, LOperand* value, LOperand* temp) {
    inputs_[0] = context;
    inputs_[1] = value;
    temps_[0] = temp;
  }

  LOperand* context() { return inputs_[0]; }
  LOperand* value() { return inputs_[1]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreContextSlot, "store-context-slot")
  DECLARE_HYDROGEN_ACCESSOR(StoreContextSlot)

  int slot_index() { return hydrogen()->slot_index(); }

  virtual void PrintDataTo(StringStream* stream);
};


class LStoreGlobalCell: public LTemplateInstruction<0, 1, 2> {
 public:
  LStoreGlobalCell(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreGlobalCell, "store-global-cell")
  DECLARE_HYDROGEN_ACCESSOR(StoreGlobalCell)
};


class LStoreGlobalGeneric: public LTemplateInstruction<0, 2, 0> {
 public:
  LStoreGlobalGeneric(LOperand* global_object, LOperand* value) {
    inputs_[0] = global_object;
    inputs_[1] = value;
  }

  LOperand* global_object() { return inputs_[0]; }
  LOperand* value() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(StoreGlobalGeneric, "store-global-generic")
  DECLARE_HYDROGEN_ACCESSOR(StoreGlobalGeneric)

  Handle<Object> name() const { return hydrogen()->name(); }
  StrictModeFlag strict_mode_flag() { return hydrogen()->strict_mode_flag(); }
};


class LSubI: public LTemplateInstruction<1, 2, 0> {
 public:
  LSubI(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(SubI, "sub-i")
  DECLARE_HYDROGEN_ACCESSOR(Sub)
};


class LSubS: public LTemplateInstruction<1, 2, 0> {
 public:
  LSubS(LOperand* left, LOperand* right) {
    inputs_[0] = left;
    inputs_[1] = right;
  }

  LOperand* left() { return inputs_[0]; }
  LOperand* right() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(SubS, "sub-s")
  DECLARE_HYDROGEN_ACCESSOR(Sub)
};


class LThisFunction: public LTemplateInstruction<1, 0, 0> {
 public:
  DECLARE_CONCRETE_INSTRUCTION(ThisFunction, "this-function")
  DECLARE_HYDROGEN_ACCESSOR(ThisFunction)
};


class LThrow: public LTemplateInstruction<0, 1, 0> {
 public:
  explicit LThrow(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(Throw, "throw")
};


class LToFastProperties: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LToFastProperties(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ToFastProperties, "to-fast-properties")
  DECLARE_HYDROGEN_ACCESSOR(ToFastProperties)
};


class LTransitionElementsKind: public LTemplateInstruction<0, 1, 2> {
 public:
  LTransitionElementsKind(LOperand* object,
                          LOperand* temp1,
                          LOperand* temp2 = NULL) {
    inputs_[0] = object;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(TransitionElementsKind,
                               "transition-elements-kind")
  DECLARE_HYDROGEN_ACCESSOR(TransitionElementsKind)

  virtual void PrintDataTo(StringStream* stream);

  Handle<Map> original_map() { return hydrogen()->original_map(); }
  Handle<Map> transitioned_map() { return hydrogen()->transitioned_map(); }
  ElementsKind from_kind() { return hydrogen()->from_kind(); }
  ElementsKind to_kind() { return hydrogen()->to_kind(); }
};


class LTrapAllocationMemento : public LTemplateInstruction<0, 1, 2> {
 public:
  LTrapAllocationMemento(LOperand* object, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = object;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(TrapAllocationMemento, "trap-allocation-memento")
};


class LTruncateDoubleToIntOrSmi: public LTemplateInstruction<1, 1, 2> {
 public:
  LTruncateDoubleToIntOrSmi(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(TruncateDoubleToIntOrSmi,
                               "truncate-double-to-int-or-smi")
  DECLARE_HYDROGEN_ACCESSOR(UnaryOperation)

  bool tag_result() { return hydrogen()->representation().IsSmi(); }
};


class LTypeof: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LTypeof(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(Typeof, "typeof")
};


class LTypeofIsAndBranch: public LControlInstruction<1, 2> {
 public:
  LTypeofIsAndBranch(LOperand* value, LOperand* temp1, LOperand* temp2) {
    inputs_[0] = value;
    temps_[0] = temp1;
    temps_[1] = temp2;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp1() { return temps_[0]; }
  LOperand* temp2() { return temps_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(TypeofIsAndBranch, "typeof-is-and-branch")
  DECLARE_HYDROGEN_ACCESSOR(TypeofIsAndBranch)

  Handle<String> type_literal() { return hydrogen()->type_literal(); }

  virtual void PrintDataTo(StringStream* stream);
};


class LUint32ToDouble: public LTemplateInstruction<1, 1, 0> {
 public:
  explicit LUint32ToDouble(LOperand* value) {
    inputs_[0] = value;
  }

  LOperand* value() { return inputs_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(Uint32ToDouble, "uint32-to-double")
};


class LValueOf: public LTemplateInstruction<1, 1, 1> {
 public:
  LValueOf(LOperand* value, LOperand* temp) {
    inputs_[0] = value;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(ValueOf, "value-of")
  DECLARE_HYDROGEN_ACCESSOR(ValueOf)
};


class LCheckMapValue: public LTemplateInstruction<0, 2, 1> {
 public:
  LCheckMapValue(LOperand* value, LOperand* map, LOperand* temp) {
    inputs_[0] = value;
    inputs_[1] = map;
    temps_[0] = temp;
  }

  LOperand* value() { return inputs_[0]; }
  LOperand* map() { return inputs_[1]; }
  LOperand* temp() { return temps_[0]; }

  DECLARE_CONCRETE_INSTRUCTION(CheckMapValue, "check-map-value")
};


class LLoadFieldByIndex: public LTemplateInstruction<1, 2, 0> {
 public:
  LLoadFieldByIndex(LOperand* object, LOperand* index) {
    inputs_[0] = object;
    inputs_[1] = index;
  }

  LOperand* object() { return inputs_[0]; }
  LOperand* index() { return inputs_[1]; }

  DECLARE_CONCRETE_INSTRUCTION(LoadFieldByIndex, "load-field-by-index")
};


class LWrapReceiver: public LTemplateInstruction<1, 2, 1> {
 public:
  LWrapReceiver(LOperand* receiver, LOperand* function, LOperand* temp) {
    inputs_[0] = receiver;
    inputs_[1] = function;
    temps_[0] = temp;
  }

  DECLARE_CONCRETE_INSTRUCTION(WrapReceiver, "wrap-receiver")

  LOperand* receiver() { return inputs_[0]; }
  LOperand* function() { return inputs_[1]; }
  LOperand* temp() { return temps_[0]; }
};


class LChunkBuilder;
class LPlatformChunk: public LChunk {
 public:
  LPlatformChunk(CompilationInfo* info, HGraph* graph)
      : LChunk(info, graph) { }

  int GetNextSpillIndex();
  LOperand* GetNextSpillSlot(bool is_double);
};


class LChunkBuilder BASE_EMBEDDED {
 public:
  LChunkBuilder(CompilationInfo* info, HGraph* graph, LAllocator* allocator)
      : chunk_(NULL),
        info_(info),
        graph_(graph),
        zone_(graph->zone()),
        status_(UNUSED),
        current_instruction_(NULL),
        current_block_(NULL),
        argument_count_(0),
        allocator_(allocator),
        position_(RelocInfo::kNoPosition),
        instruction_pending_deoptimization_environment_(NULL),
        pending_deoptimization_ast_id_(BailoutId::None()) { }

  // Build the sequence for the graph.
  LPlatformChunk* Build();

  // Declare methods that deal with the individual node types.
#define DECLARE_DO(type) LInstruction* Do##type(H##type* node);
  HYDROGEN_CONCRETE_INSTRUCTION_LIST(DECLARE_DO)
#undef DECLARE_DO

  static bool HasMagicNumberForDivision(int32_t divisor);
  static HValue* SimplifiedDivisorForMathFloorOfDiv(HValue* val);

 private:
  enum Status {
    UNUSED,
    BUILDING,
    DONE,
    ABORTED
  };

  HGraph* graph() const { return graph_; }
  Isolate* isolate() const { return info_->isolate(); }

  bool is_unused() const { return status_ == UNUSED; }
  bool is_building() const { return status_ == BUILDING; }
  bool is_done() const { return status_ == DONE; }
  bool is_aborted() const { return status_ == ABORTED; }

  Zone* zone() const { return zone_; }
  int argument_count() const { return argument_count_; }
  CompilationInfo* info() const { return info_; }
  Heap* heap() const { return isolate()->heap(); }

  void Abort(BailoutReason reason);

  // Methods for getting operands for Use / Define / Temp.
  LUnallocated* ToUnallocated(Register reg);
  LUnallocated* ToUnallocated(DoubleRegister reg);

  // Methods for setting up define-use relationships.
  MUST_USE_RESULT LOperand* Use(HValue* value, LUnallocated* operand);
  MUST_USE_RESULT LOperand* UseFixed(HValue* value, Register fixed_register);
  MUST_USE_RESULT LOperand* UseFixedDouble(HValue* value,
                                           DoubleRegister fixed_register);

  // A value that is guaranteed to be allocated to a register.
  // The operand created by UseRegister is guaranteed to be live until the end
  // of the instruction. This means that register allocator will not reuse its
  // register for any other operand inside instruction.
  MUST_USE_RESULT LOperand* UseRegister(HValue* value);

  // The operand created by UseRegisterAndClobber is guaranteed to be live until
  // the end of the end of the instruction, and it may also be used as a scratch
  // register by the instruction implementation.
  //
  // This behaves identically to ARM's UseTempRegister. However, it is renamed
  // to discourage its use in A64, since in most cases it is better to allocate
  // a temporary register for the Lithium instruction.
  MUST_USE_RESULT LOperand* UseRegisterAndClobber(HValue* value);

  // The operand created by UseRegisterAtStart is guaranteed to be live only at
  // instruction start. The register allocator is free to assign the same
  // register to some other operand used inside instruction (i.e. temporary or
  // output).
  MUST_USE_RESULT LOperand* UseRegisterAtStart(HValue* value);

  // An input operand in a register or a constant operand.
  MUST_USE_RESULT LOperand* UseRegisterOrConstant(HValue* value);
  MUST_USE_RESULT LOperand* UseRegisterOrConstantAtStart(HValue* value);

  // A constant operand.
  MUST_USE_RESULT LConstantOperand* UseConstant(HValue* value);

  // An input operand in register, stack slot or a constant operand.
  // Will not be moved to a register even if one is freely available.
  MUST_USE_RESULT LOperand* UseAny(HValue* value);

  // Temporary operand that must be in a register.
  MUST_USE_RESULT LUnallocated* TempRegister();

  // Temporary operand that must be in a fixed double register.
  MUST_USE_RESULT LOperand* FixedTemp(DoubleRegister reg);

  // Methods for setting up define-use relationships.
  // Return the same instruction that they are passed.
  template<int I, int T>
      LInstruction* Define(LTemplateInstruction<1, I, T>* instr,
                           LUnallocated* result);
  template<int I, int T>
      LInstruction* DefineAsRegister(LTemplateInstruction<1, I, T>* instr);

  template<int I, int T>
      LInstruction* DefineAsSpilled(LTemplateInstruction<1, I, T>* instr,
                                    int index);

  template<int I, int T>
      LInstruction* DefineSameAsFirst(LTemplateInstruction<1, I, T>* instr);

  template<int I, int T>
      LInstruction* DefineFixed(LTemplateInstruction<1, I, T>* instr,
                                Register reg);
  template<int I, int T>
      LInstruction* DefineFixedDouble(LTemplateInstruction<1, I, T>* instr,
                                      DoubleRegister reg);

  enum CanDeoptimize { CAN_DEOPTIMIZE_EAGERLY, CANNOT_DEOPTIMIZE_EAGERLY };

  // By default we assume that instruction sequences generated for calls
  // cannot deoptimize eagerly and we do not attach environment to this
  // instruction.
  LInstruction* MarkAsCall(
      LInstruction* instr,
      HInstruction* hinstr,
      CanDeoptimize can_deoptimize = CANNOT_DEOPTIMIZE_EAGERLY);

  LInstruction* AssignPointerMap(LInstruction* instr);
  LInstruction* AssignEnvironment(LInstruction* instr);
  LEnvironment* CreateEnvironment(HEnvironment* hydrogen_env,
                                  int* argument_index_accumulator,
                                  ZoneList<HValue*>* objects_to_materialize);

  void VisitInstruction(HInstruction* current);
  void DoBasicBlock(HBasicBlock* block);

  LInstruction* DoShift(Token::Value op, HBitwiseBinaryOperation* instr);
  LInstruction* DoArithmeticD(Token::Value op,
                              HArithmeticBinaryOperation* instr);
  LInstruction* DoArithmeticT(Token::Value op,
                              HBinaryOperation* instr);

  LPlatformChunk* chunk_;
  CompilationInfo* info_;
  HGraph* const graph_;
  Zone* zone_;
  Status status_;
  HInstruction* current_instruction_;
  HBasicBlock* current_block_;
  int argument_count_;
  LAllocator* allocator_;
  int position_;
  LInstruction* instruction_pending_deoptimization_environment_;
  BailoutId pending_deoptimization_ast_id_;

  DISALLOW_COPY_AND_ASSIGN(LChunkBuilder);
};

#undef DECLARE_HYDROGEN_ACCESSOR
#undef DECLARE_CONCRETE_INSTRUCTION

} }  // namespace v8::internal

#endif  // V8_A64_LITHIUM_A64_H_
