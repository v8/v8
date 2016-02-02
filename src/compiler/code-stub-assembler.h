// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_CODE_STUB_ASSEMBLER_H_
#define V8_COMPILER_CODE_STUB_ASSEMBLER_H_

#include <map>

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/allocation.h"
#include "src/builtins.h"
#include "src/heap/heap.h"
#include "src/machine-type.h"
#include "src/runtime/runtime.h"
#include "src/zone-containers.h"

namespace v8 {
namespace internal {

class CallInterfaceDescriptor;
class Isolate;
class Zone;

namespace compiler {

class CallDescriptor;
class Graph;
class Node;
class Operator;
class RawMachineAssembler;
class RawMachineLabel;
class Schedule;

#define CODE_STUB_ASSEMBLER_BINARY_OP_LIST(V) \
  V(IntPtrAdd)                                \
  V(IntPtrSub)                                \
  V(Int32Add)                                 \
  V(Int32Sub)                                 \
  V(Int32Mul)                                 \
  V(WordEqual)                                \
  V(WordNotEqual)                             \
  V(WordOr)                                   \
  V(WordAnd)                                  \
  V(WordXor)                                  \
  V(WordShl)                                  \
  V(WordShr)                                  \
  V(WordSar)                                  \
  V(WordRor)                                  \
  V(Word32Equal)                              \
  V(Word32NotEqual)                           \
  V(Word32Or)                                 \
  V(Word32And)                                \
  V(Word32Xor)                                \
  V(Word32Shr)                                \
  V(Word32Sar)                                \
  V(Word32Ror)                                \
  V(Word64Equal)                              \
  V(Word64NotEqual)                           \
  V(Word64Or)                                 \
  V(Word64And)                                \
  V(Word64Xor)                                \
  V(Word64Shr)                                \
  V(Word64Sar)                                \
  V(Word64Ror)

class CodeStubAssembler {
 public:
  CodeStubAssembler(Isolate* isolate, Zone* zone,
                    const CallInterfaceDescriptor& descriptor,
                    Code::Flags flags, const char* name);
  virtual ~CodeStubAssembler();

  Handle<Code> GenerateCode();

  class Label;
  class Variable {
   public:
    explicit Variable(CodeStubAssembler* assembler, MachineRepresentation rep);
    void Bind(Node* value);
    Node* value() const;
    MachineRepresentation rep() const;
    bool IsBound() const;

   private:
    friend class CodeStubAssembler;
    class Impl;
    Impl* impl_;
  };

  // ===========================================================================
  // Base Assembler
  // ===========================================================================

  // Constants.
  Node* Int32Constant(int value);
  Node* IntPtrConstant(intptr_t value);
  Node* NumberConstant(double value);
  Node* HeapConstant(Handle<HeapObject> object);
  Node* BooleanConstant(bool value);
  Node* ExternalConstant(ExternalReference address);

  Node* Parameter(int value);
  void Return(Node* value);

  void Bind(Label* label);
  void Goto(Label* label);
  void Branch(Node* condition, Label* true_label, Label* false_label);

  void Switch(Node* index, Label* default_label, int32_t* case_values,
              Label** case_labels, size_t case_count);

  // Access to the frame pointer
  Node* LoadFramePointer();
  Node* LoadParentFramePointer();

// Basic arithmetic operations.
#define DECLARE_CODE_STUB_ASSEMBER_BINARY_OP(name) Node* name(Node* a, Node* b);
  CODE_STUB_ASSEMBLER_BINARY_OP_LIST(DECLARE_CODE_STUB_ASSEMBER_BINARY_OP)
#undef DECLARE_CODE_STUB_ASSEMBER_BINARY_OP

  Node* WordShl(Node* value, int shift);

  // Calls
  Node* CallRuntime(Runtime::FunctionId function_id, Node* context, Node* arg1);
  Node* CallRuntime(Runtime::FunctionId function_id, Node* context, Node* arg1,
                    Node* arg2);

  Node* TailCallRuntime(Runtime::FunctionId function_id, Node* context,
                        Node* arg1);
  Node* TailCallRuntime(Runtime::FunctionId function_id, Node* context,
                        Node* arg1, Node* arg2);
  Node* TailCallRuntime(Runtime::FunctionId function_id, Node* context,
                        Node* arg1, Node* arg2, Node* arg3);
  Node* TailCallRuntime(Runtime::FunctionId function_id, Node* context,
                        Node* arg1, Node* arg2, Node* arg3, Node* arg4);

  Node* TailCallStub(CodeStub& stub, Node** args);
  Node* TailCall(const CallInterfaceDescriptor& descriptor, Node* target,
                 Node** args);

  // ===========================================================================
  // Macros
  // ===========================================================================

  // Tag and untag Smi values.
  Node* SmiTag(Node* value);
  Node* SmiUntag(Node* value);

  // Load a value from the root array.
  Node* LoadRoot(Heap::RootListIndex root_index);

  // Check a value for smi-ness
  Node* WordIsSmi(Node* a);

  // Load an object pointer from a buffer that isn't in the heap.
  Node* LoadBufferObject(Node* buffer, int offset);
  // Load a field from an object on the heap.
  Node* LoadObjectField(Node* object, int offset);

  // Load an array element from a FixedArray.
  Node* LoadFixedArrayElementSmiIndex(Node* object, Node* smi_index,
                                      int additional_offset = 0);
  Node* LoadFixedArrayElementConstantIndex(Node* object, int index);

 private:
  friend class CodeStubAssemblerTester;

  Node* CallN(CallDescriptor* descriptor, Node* code_target, Node** args);
  Node* TailCallN(CallDescriptor* descriptor, Node* code_target, Node** args);

  Node* SmiShiftBitsConstant();

  // Private helpers which delegate to RawMachineAssembler.
  Graph* graph();
  Isolate* isolate();
  Zone* zone();

  base::SmartPointer<RawMachineAssembler> raw_assembler_;
  Code::Flags flags_;
  const char* name_;
  bool code_generated_;
  ZoneVector<Variable::Impl*> variables_;

  DISALLOW_COPY_AND_ASSIGN(CodeStubAssembler);
};

class CodeStubAssembler::Label {
 public:
  explicit Label(CodeStubAssembler* assembler);
  Label(CodeStubAssembler* assembler, int merged_variable_count,
        CodeStubAssembler::Variable** merged_variables);
  Label(CodeStubAssembler* assembler,
        CodeStubAssembler::Variable* merged_variable);
  ~Label() {}

 private:
  friend class CodeStubAssembler;

  void Bind();
  void MergeVariables();

  bool bound_;
  size_t merge_count_;
  CodeStubAssembler* assembler_;
  RawMachineLabel* label_;
  // Map of variables that need to be merged to their phi nodes (or placeholders
  // for those phis).
  std::map<Variable::Impl*, Node*> variable_phis_;
  // Map of variables to the list of value nodes that have been added from each
  // merge path in their order of merging.
  std::map<Variable::Impl*, std::vector<Node*>> variable_merges_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_CODE_STUB_ASSEMBLER_H_
