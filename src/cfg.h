// Copyright 2009 the V8 project authors. All rights reserved.
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

#ifndef V8_CFG_H_
#define V8_CFG_H_

#include "ast.h"

namespace v8 {
namespace internal {

class ExitNode;
class Location;

// Translate a source AST into a control-flow graph (CFG).  The CFG contains
// single-entry, single-exit blocks of straight-line instructions and
// administrative nodes.
//
// Instructions are described by the following grammar.
//
// <Instruction> ::=
//     BinaryOpInstr <Location> Token::Value <Value> <Value>
//   | ReturnInstr Effect <Value>
//
// Values are trivial expressions:
//
// <Value> ::= Constant | <Location>
//
// Locations are storable values ('lvalues').  They can be slots,
// compiler-generated temporaries, or the special location 'Effect'
// indicating that no value is needed.
//
// <Location> ::=
//     SlotLocation Slot::Type <Index>
//   | TempLocation
//   | Effect


// Administrative nodes: There are several types of 'administrative' nodes
// that do not contain instructions and do not necessarily have a single
// predecessor and a single successor.
//
// EntryNode: there is a distinguished entry node that has no predecessors
// and a single successor.
//
// ExitNode: there is a distinguished exit node that has arbitrarily many
// predecessors and no successor.
//
// JoinNode: join nodes have multiple predecessors and a single successor.
//
// BranchNode: branch nodes have a single predecessor and multiple
// successors.


// A convenient class to keep 'global' values when building a CFG.  Since
// CFG construction can be invoked recursively, CFG globals are stacked.
class CfgGlobals BASE_EMBEDDED {
 public:
  explicit CfgGlobals(FunctionLiteral* fun);

  ~CfgGlobals() { top_ = previous_; }

  static CfgGlobals* current() {
    ASSERT(top_ != NULL);
    return top_;
  }

  // The function currently being compiled.
  FunctionLiteral* fun() { return global_fun_; }

  // The shared global exit node for all exits from the function.
  ExitNode* exit() { return global_exit_; }

  // A singleton effect location.
  Location* effect_location() { return effect_; }

#ifdef DEBUG
  int next_node_number() { return node_counter_++; }
  int next_temp_number() { return temp_counter_++; }
#endif

 private:
  static CfgGlobals* top_;
  FunctionLiteral* global_fun_;
  ExitNode* global_exit_;
  Location* effect_;

#ifdef DEBUG
  // Used to number nodes and temporaries when printing.
  int node_counter_;
  int temp_counter_;
#endif

  CfgGlobals* previous_;
};


// Values represent trivial source expressions: ones with no side effects
// and that do not require code to be generated.
class Value : public ZoneObject {
 public:
  virtual ~Value() {}

  // Predicates:

  // True if the value is a temporary allocated to the stack in
  // fast-compilation mode.
  virtual bool is_on_stack() { return false; }

  // True if the value is a compiler-generated temporary location.
  virtual bool is_temporary() { return false; }

  // Support for fast-compilation mode:

  // Move the value into a register.
  virtual void Get(MacroAssembler* masm, Register reg) = 0;

  // Push the value on the stack.
  virtual void Push(MacroAssembler* masm) = 0;

#ifdef DEBUG
  virtual void Print() = 0;
#endif
};


// A compile-time constant that appeared as a literal in the source AST.
class Constant : public Value {
 public:
  explicit Constant(Handle<Object> handle) : handle_(handle) {}

  virtual ~Constant() {}

  // Support for fast-compilation mode.
  void Get(MacroAssembler* masm, Register reg);
  void Push(MacroAssembler* masm);

#ifdef DEBUG
  void Print();
#endif

 private:
  Handle<Object> handle_;
};


// Locations are values that can be stored into ('lvalues').
class Location : public Value {
 public:
  virtual ~Location() {}

  // Static factory function returning the singleton effect location.
  static Location* Effect() {
    return CfgGlobals::current()->effect_location();
  }

  // Support for fast-compilation mode:

  // Assumes temporaries have been allocated.
  virtual void Get(MacroAssembler* masm, Register reg) = 0;

  // Store the value in a register to the location.  Assumes temporaries
  // have been allocated.
  virtual void Set(MacroAssembler* masm, Register reg) = 0;

  // Assumes temporaries have been allocated, and if the value is a
  // temporary it was not allocated to the stack.
  virtual void Push(MacroAssembler* masm) = 0;

#ifdef DEBUG
  virtual void Print() = 0;
#endif
};


// Effect is a special (singleton) location that indicates the value of a
// computation is not needed (though its side effects are).
class Effect : public Location {
 public:
  // We should not try to emit code to read Effect.
  void Get(MacroAssembler* masm, Register reg) { UNREACHABLE(); }
  void Push(MacroAssembler* masm) { UNREACHABLE(); }

  // Setting Effect is ignored.
  void Set(MacroAssembler* masm, Register reg) {}

#ifdef DEBUG
  void Print();
#endif

 private:
  Effect() {}

  friend class CfgGlobals;
};


// SlotLocations represent parameters and stack-allocated (i.e.,
// non-context) local variables.
class SlotLocation : public Location {
 public:
  SlotLocation(Slot::Type type, int index) : type_(type), index_(index) {}

  // Accessors.
  Slot::Type type() { return type_; }
  int index() { return index_; }

  // Support for fast-compilation mode.
  void Get(MacroAssembler* masm, Register reg);
  void Set(MacroAssembler* masm, Register reg);
  void Push(MacroAssembler* masm);

#ifdef DEBUG
  void Print();
#endif

 private:
  Slot::Type type_;
  int index_;
};


// TempLocations represent compiler generated temporaries.  They are
// allocated to registers or memory either before code generation (in the
// optimized-for-speed compiler) or on the fly during code generation (in
// the optimized-for-space compiler).
class TempLocation : public Location {
 public:
  // Fast-compilation mode allocation decisions.
  enum Where {
    NOWHERE,      // Not yet allocated.
    ACCUMULATOR,  // Allocated to the dedicated accumulator register.
    STACK         //   "   "   "   "  stack.
  };

  TempLocation() : where_(NOWHERE) {
#ifdef DEBUG
    number_ = -1;
#endif
  }

  // Cast accessor.
  static TempLocation* cast(Location* loc) {
    ASSERT(loc->is_temporary());
    return reinterpret_cast<TempLocation*>(loc);
  }

  // Accessors.
  Where where() { return where_; }
  void set_where(Where where) { where_ = where; }

  // Predicates.
  bool is_on_stack() { return where_ == STACK; }
  bool is_temporary() { return true; }

  // Support for fast-compilation mode.  Assume the temp has been allocated.
  void Get(MacroAssembler* masm, Register reg);
  void Set(MacroAssembler* masm, Register reg);
  void Push(MacroAssembler* masm);

#ifdef DEBUG
  int number() {
    if (number_ == -1) number_ = CfgGlobals::current()->next_temp_number();
    return number_;
  }

  void Print();
#endif

 private:
  Where where_;

#ifdef DEBUG
  int number_;
#endif
};


// Instructions are computations.  The represent non-trivial source
// expressions: typically ones that have side effects and require code to
// be generated.
class Instruction : public ZoneObject {
 public:
  // Every instruction has a location where its result is stored (which may
  // be Effect, the default).
  Instruction() : loc_(CfgGlobals::current()->effect_location()) {}

  explicit Instruction(Location* loc) : loc_(loc) {}

  virtual ~Instruction() {}

  // Accessors.
  Location* location() { return loc_; }
  void set_location(Location* loc) { loc_ = loc; }

  // Support for fast-compilation mode:

  // Emit code to perform the instruction.
  virtual void Compile(MacroAssembler* masm) = 0;

  // Allocate a temporary which is the result of the immediate predecessor
  // instruction.  It is allocated to the accumulator register if it is used
  // as an operand to this instruction, otherwise to the stack.
  virtual void FastAllocate(TempLocation* temp) = 0;

#ifdef DEBUG
  virtual void Print() = 0;
#endif

 protected:
  Location* loc_;
};


// A phantom instruction that indicates the start of a statement.  It
// causes the statement position to be recorded in the relocation
// information but generates no code.
class PositionInstr : public Instruction {
 public:
  explicit PositionInstr(int pos) : pos_(pos) {}

  // Support for fast-compilation mode.
  void Compile(MacroAssembler* masm);

  // This should not be called.  The last instruction of the previous
  // statement should not have a temporary as its location.
  void FastAllocate(TempLocation* temp) { UNREACHABLE(); }

#ifdef DEBUG
  // Printing support.  Print nothing.
  void Print() {}
#endif

 private:
  int pos_;
};


// Perform a (non-short-circuited) binary operation on a pair of values,
// leaving the result in a location.
class BinaryOpInstr : public Instruction {
 public:
  BinaryOpInstr(Location* loc, Token::Value op, Value* val0, Value* val1)
      : Instruction(loc), op_(op), val0_(val0), val1_(val1) {
  }

  // Support for fast-compilation mode.
  void Compile(MacroAssembler* masm);
  void FastAllocate(TempLocation* temp);

#ifdef DEBUG
  void Print();
#endif

 private:
  Token::Value op_;
  Value* val0_;
  Value* val1_;
};


// Return a value.  Has the side effect of moving its value into the return
// value register.  Can only occur as the last instruction in an instruction
// block, and implies that the block is closed (cannot have instructions
// appended or graph fragments concatenated to the end) and that the block's
// successor is the global exit node for the current function.
class ReturnInstr : public Instruction {
 public:
  // Location is always Effect.
  explicit ReturnInstr(Value* value) : value_(value) {}

  virtual ~ReturnInstr() {}

  // Support for fast-compilation mode.
  void Compile(MacroAssembler* masm);
  void FastAllocate(TempLocation* temp);

#ifdef DEBUG
  void Print();
#endif

 private:
  Value* value_;
};


// Nodes make up control-flow graphs.
class CfgNode : public ZoneObject {
 public:
  CfgNode() : is_marked_(false) {
#ifdef DEBUG
    number_ = -1;
#endif
  }

  virtual ~CfgNode() {}

  // Because CFGs contain cycles, nodes support marking during traversal
  // (e.g., for printing or compilation).  The traversal functions will mark
  // unmarked nodes and backtrack if they encounter a marked one.  After a
  // traversal, the graph should be explicitly unmarked by calling Unmark on
  // the entry node.
  bool is_marked() { return is_marked_; }
  virtual void Unmark() = 0;

  // Predicates:

  // True if the node is an instruction block.
  virtual bool is_block() { return false; }

  // Support for fast-compilation mode.  Emit the instructions or control
  // flow represented by the node.
  virtual void Compile(MacroAssembler* masm) = 0;

#ifdef DEBUG
  int number() {
    if (number_ == -1) number_ = CfgGlobals::current()->next_node_number();
    return number_;
  }

  virtual void Print() = 0;
#endif

 protected:
  bool is_marked_;

#ifdef DEBUG
  int number_;
#endif
};


// A block is a single-entry, single-exit block of instructions.
class InstructionBlock : public CfgNode {
 public:
  InstructionBlock() : successor_(NULL), instructions_(4) {}

  virtual ~InstructionBlock() {}

  void Unmark();

  // Cast accessor.
  static InstructionBlock* cast(CfgNode* node) {
    ASSERT(node->is_block());
    return reinterpret_cast<InstructionBlock*>(node);
  }

  bool is_block() { return true; }

  // Accessors.
  CfgNode* successor() { return successor_; }

  void set_successor(CfgNode* succ) {
    ASSERT(successor_ == NULL);
    successor_ = succ;
  }

  ZoneList<Instruction*>* instructions() { return &instructions_; }

  // Support for fast-compilation mode.
  void Compile(MacroAssembler* masm);

  // Add an instruction to the end of the block.
  void Append(Instruction* instr) { instructions_.Add(instr); }

#ifdef DEBUG
  void Print();
#endif

 private:
  CfgNode* successor_;
  ZoneList<Instruction*> instructions_;
};


// An entry node (one per function).
class EntryNode : public CfgNode {
 public:
  explicit EntryNode(InstructionBlock* succ) : successor_(succ) {}

  virtual ~EntryNode() {}

  void Unmark();

  // Support for fast-compilation mode.
  void Compile(MacroAssembler* masm);

#ifdef DEBUG
  void Print();
#endif

 private:
  InstructionBlock* successor_;
};


// An exit node (one per function).
class ExitNode : public CfgNode {
 public:
  ExitNode() {}

  virtual ~ExitNode() {}

  void Unmark();

  // Support for fast-compilation mode.
  void Compile(MacroAssembler* masm);

#ifdef DEBUG
  void Print();
#endif
};


// A CFG consists of a linked structure of nodes.  Nodes are linked by
// pointing to their successors, always beginning with a (single) entry node
// (not necessarily of type EntryNode).  If it is still possible to add
// nodes to the end of the graph (i.e., there is a (single) path that does
// not end with the global exit node), then the CFG has an exit node as
// well.
//
// The empty CFG is represented by a NULL entry and a NULL exit.
//
// We use the term 'open fragment' to mean a CFG whose entry and exits are
// both instruction blocks.  It is always possible to add instructions and
// nodes to the beginning or end of an open fragment.
//
// We use the term 'closed fragment' to mean a CFG whose entry is an
// instruction block and whose exit is NULL (all paths go to the global
// exit).
//
// We use the term 'fragment' to refer to a CFG that is known to be an open
// or closed fragment.
class Cfg : public ZoneObject {
 public:
  // Create an empty CFG fragment.
  Cfg() : entry_(NULL), exit_(NULL) {}

  // Build the CFG for a function.  The returned CFG begins with an
  // EntryNode and all paths end with the ExitNode.
  static Cfg* Build();

  // The entry and exit nodes of the CFG (not necessarily EntryNode and
  // ExitNode).
  CfgNode* entry() { return entry_; }
  CfgNode* exit() { return exit_; }

  // True if the CFG has no nodes.
  bool is_empty() { return entry_ == NULL; }

  // True if the CFG has an available exit node (i.e., it can be appended or
  // concatenated to).
  bool has_exit() { return exit_ != NULL; }

  // Add an EntryNode to a CFG fragment.  It is no longer a fragment
  // (instructions can no longer be prepended).
  void PrependEntryNode();

  // Append an instruction to the end of an open fragment.
  void Append(Instruction* instr);

  // Appends a return instruction to the end of an open fragment and make
  // it a closed fragment (the exit's successor becomes global exit node).
  void AppendReturnInstruction(Value* value);

  // Glue an other CFG fragment to the end of this (open) fragment.
  void Concatenate(Cfg* other);

  // Support for compilation.  Compile the entire CFG.
  Handle<Code> Compile(Handle<Script> script);

#ifdef DEBUG
  // Support for printing.
  void Print();
#endif

 private:
  // Entry and exit nodes.
  CfgNode* entry_;
  CfgNode* exit_;
};


// An ExpressionBuilder traverses an expression and returns an open CFG
// fragment (currently a possibly empty list of instructions represented by
// a singleton instruction block) and the expression's value.
//
// Failure is to build the CFG is indicated by a NULL CFG.
class ExpressionBuilder : public AstVisitor {
 public:
  ExpressionBuilder() : value_(NULL), cfg_(NULL) {}

  // Result accessors.
  Value* value() { return value_; }
  Cfg* cfg() { return cfg_; }

  void Build(Expression* expr) {
    value_ = NULL;
    cfg_ = new Cfg();
    Visit(expr);
  }

  // AST node visitors.
#define DECLARE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

 private:
  Value* value_;
  Cfg* cfg_;
};


// A StatementBuilder maintains a CFG fragment accumulator.  When it visits
// a statement, it concatenates the CFG for the statement to the end of the
// accumulator.
class StatementBuilder : public AstVisitor {
 public:
  StatementBuilder() : cfg_(new Cfg()) {}

  Cfg* cfg() { return cfg_; }

  void VisitStatements(ZoneList<Statement*>* stmts);

  // AST node visitors.
#define DECLARE_VISIT(type) void Visit##type(type* node);
  AST_NODE_LIST(DECLARE_VISIT)
#undef DECLARE_VISIT

 private:
  Cfg* cfg_;
};


} }  // namespace v8::internal

#endif  // V8_CFG_H_
