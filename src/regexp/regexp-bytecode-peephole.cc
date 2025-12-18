// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/regexp-bytecode-peephole.h"

#include <limits>
#include <optional>

#include "src/flags/flags.h"
#include "src/objects/fixed-array-inl.h"
#include "src/regexp/regexp-bytecode-generator-inl.h"
#include "src/regexp/regexp-bytecode-generator.h"
#include "src/regexp/regexp-bytecodes-inl.h"
#include "src/regexp/regexp-bytecodes.h"
#include "src/utils/memcopy.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

namespace {

class BytecodeArgument {
 public:
  BytecodeArgument(int offset, int length) : offset_(offset), length_(length) {}

  int offset() const { return offset_; }
  int length() const { return length_; }

 private:
  // TODO(jgruber): This should store {offset,type} as well.
  // TODO(jgruber): Consider changing offset_ to be relative to the current
  // bytecode instead of the start of the bytecode sequence that is being
  // optimized. It is confusing that src/dst offsets have different semantics.
  int offset_;
  int length_;
};

// Describes a bytecode operand for use in a peephole sequence.
struct OpInfo {
  uint16_t offset;
  RegExpBytecodeOperandType type;
  constexpr int size() const { return RegExpBytecodes::Size(type); }

  // Usage:
  //   OpInfo::Get<RegExpBytecodeOperands<BYTECODE>,
  //   RegExpBytecodeOperands<BYTECODE>::Operand::OPERAND>()
  template <class kBytecodeOperands, auto kOperand>
  static OpInfo Get() {
    static constexpr int kOffset = kBytecodeOperands::Offset(kOperand);
    static constexpr RegExpBytecodeOperandType kType =
        kBytecodeOperands::Type(kOperand);
    DCHECK_LE(static_cast<uint32_t>(kOffset),
              std::numeric_limits<decltype(offset)>::max());
    return {kOffset, kType};
  }
};
static_assert(sizeof(OpInfo) <= kSystemPointerSize);  // Passed by value.

class BytecodeArgumentMapping : public BytecodeArgument {
 public:
  enum class Type : uint8_t { kDefault, kOffsetAfterSequence };

  BytecodeArgumentMapping(int offset, int length, OpInfo op_info)
      : BytecodeArgument(offset, length),
        type_(Type::kDefault),
        op_info_(op_info) {}

  BytecodeArgumentMapping(Type type, OpInfo op_info)
      : BytecodeArgument(-1, -1), type_(type), op_info_(op_info) {
    DCHECK_NE(type, Type::kDefault);
  }

  Type type() const { return type_; }
  int new_offset() const { return op_info_.offset; }
  RegExpBytecodeOperandType new_operand_type() const { return op_info_.type; }
  int new_length() const { return op_info_.size(); }

 private:
  Type type_;
  OpInfo op_info_;
};

struct BytecodeArgumentCheck : public BytecodeArgument {
  enum CheckType { kCheckAddress = 0, kCheckValue };
  CheckType type;
  int check_offset;
  int check_length;

  BytecodeArgumentCheck(int offset, int length, int check_offset)
      : BytecodeArgument(offset, length),
        type(kCheckAddress),
        check_offset(check_offset) {}
  BytecodeArgumentCheck(int offset, int length, int check_offset,
                        int check_length)
      : BytecodeArgument(offset, length),
        type(kCheckValue),
        check_offset(check_offset),
        check_length(check_length) {}
};

// Trie-Node for storing bytecode sequences we want to optimize.
class BytecodeSequenceNode {
 public:
  BytecodeSequenceNode(std::optional<RegExpBytecode> bytecode, Zone* zone);
  // Adds a new node as child of the current node if it isn't a child already.
  BytecodeSequenceNode& FollowedBy(RegExpBytecode bytecode);
  // Marks the end of a sequence and sets optimized bytecode to replace all
  // bytecodes of the sequence with.
  BytecodeSequenceNode& ReplaceWith(RegExpBytecode bytecode);
  // Maps arguments of bytecodes in the sequence to the optimized bytecode.
  // Order of invocation determines order of arguments in the optimized
  // bytecode.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. after ReplaceWith()).
  // to_op_info: Operand info of the argument in the optimized bytecode.
  // from_bytecode_sequence_index: Zero-based index of the referred bytecode
  // within the sequence (e.g. the bytecode passed to CreateSequence() has
  // index 0).
  // from_op_info: Operand info of the argument in the referred bytecode.
  BytecodeSequenceNode& MapArgument(OpInfo to_op_info,
                                    int from_bytecode_sequence_index,
                                    OpInfo from_op_info);

  // Emits the offset after the whole sequence.
  // This should be used for every sequence that doesn't end in an unconditional
  // jump. The offset isn't statically known, as bytecodes might be preserved
  // after the sequence if they were jump targets from bytecodes outside the
  // sequence. The emitted offset is after these potentially preserved
  // bytecodes.
  BytecodeSequenceNode& EmitOffsetAfterSequence(OpInfo op_info);
  // Verifies that we've created mappings in the order they are specified.
  bool BytecodeArgumentMappingCreatedInOrder(OpInfo op_info);
  // Adds a check to the sequence node making it only a valid sequence when the
  // argument of the current bytecode at the specified offset matches the offset
  // to check against.
  // op_info: Operand info of the argument to check.
  // check_byte_offset: Zero-based offset relative to the beginning of the
  // sequence that needs to match the value given by argument_offset. (e.g.
  // check_byte_offset 0 matches the address of the first bytecode in the
  // sequence).
  BytecodeSequenceNode& IfArgumentEqualsOffset(OpInfo op_info,
                                               int check_byte_offset);

  // Adds a check to the sequence node making it only a valid sequence when the
  // argument of the current bytecode at the specified offset matches the
  // argument of another bytecode in the sequence.
  // This is similar to IfArgumentEqualsOffset, except that this method matches
  // the values of both arguments.
  BytecodeSequenceNode& IfArgumentEqualsValueAtOffset(
      OpInfo this_op_info, int other_bytecode_index_in_sequence,
      OpInfo other_op_info);

  // Marks an argument as unused.
  // All arguments that are not mapped explicitly have to be marked as unused.
  // bytecode_index_in_sequence: Zero-based index of the referred bytecode
  // within the sequence (e.g. the bytecode passed to CreateSequence() has
  // index 0).
  // op_info: Operand info of the argument to ignore.
  BytecodeSequenceNode& IgnoreArgument(int bytecode_index_in_sequence,
                                       OpInfo op_info);
  // Checks if the current node is valid for the sequence. I.e. all conditions
  // set by IfArgumentEqualsOffset and IfArgumentEquals are fulfilled by this
  // node for the actual bytecode sequence.
  bool CheckArguments(const uint8_t* bytecode, int pc);
  // Returns whether this node marks the end of a valid sequence (i.e. can be
  // replaced with an optimized bytecode).
  bool IsSequence() const;
  // Returns the length of the sequence in bytes.
  int SequenceLength() const;
  // Returns the optimized bytecode for the node.
  RegExpBytecode OptimizedBytecode() const;
  // Returns the child of the current node matching the given bytecode or
  // nullptr if no such child is found.
  BytecodeSequenceNode* Find(RegExpBytecode bytecode) const;
  // Returns number of arguments mapped to the current node.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  size_t ArgumentSize() const;
  // Returns the argument-mapping of the argument at index.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  BytecodeArgumentMapping ArgumentMapping(size_t index) const;
  // Returns an iterator to begin of ignored arguments.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  ZoneLinkedList<BytecodeArgument>::iterator ArgumentIgnoredBegin() const;
  // Returns an iterator to end of ignored arguments.
  // Invoking this method is only allowed on nodes that mark the end of a valid
  // sequence (i.e. if IsSequence())
  ZoneLinkedList<BytecodeArgument>::iterator ArgumentIgnoredEnd() const;
  // Returns whether the current node has ignored argument or not.
  bool HasIgnoredArguments() const;

 private:
  // Returns a node in the sequence specified by its index within the sequence.
  BytecodeSequenceNode& GetNodeByIndexInSequence(int index_in_sequence);
  Zone* zone() const;

  std::optional<RegExpBytecode> bytecode_;
  std::optional<RegExpBytecode> bytecode_replacement_;
  int index_in_sequence_;
  int start_offset_;
  BytecodeSequenceNode* parent_;
  ZoneUnorderedMap<RegExpBytecode, BytecodeSequenceNode*> children_;
  ZoneVector<BytecodeArgumentMapping>* argument_mapping_;
  ZoneLinkedList<BytecodeArgumentCheck>* argument_check_;
  ZoneLinkedList<BytecodeArgument>* argument_ignored_;

  Zone* zone_;
};

class RegExpBytecodePeephole {
 public:
  RegExpBytecodePeephole(Zone* zone, size_t buffer_size,
                         const ZoneUnorderedMap<int, int>& jump_edges);

  // Parses bytecode and fills the internal buffer with the potentially
  // optimized bytecode. Returns true when optimizations were performed, false
  // otherwise.
  bool OptimizeBytecode(const uint8_t* bytecode, int length);
  // Copies the internal bytecode buffer to another buffer. The caller is
  // responsible for allocating/freeing the memory.
  void CopyOptimizedBytecode(uint8_t* to_address) const;
  int Length() const;

 private:
  // Sets up all sequences that are going to be used.
  void DefineStandardSequences();
  // Starts a new bytecode sequence.
  BytecodeSequenceNode& CreateSequence(RegExpBytecode bytecode);
  // Checks for optimization candidates at pc and emits optimized bytecode to
  // the internal buffer. Returns the length of replaced bytecodes in bytes.
  int TryOptimizeSequence(const uint8_t* bytecode, int bytecode_length,
                          int start_pc);
  // Emits optimized bytecode to the internal buffer. start_pc points to the
  // start of the sequence in bytecode and last_node is the last
  // BytecodeSequenceNode of the matching sequence found.
  void EmitOptimization(int start_pc, const uint8_t* bytecode,
                        const BytecodeSequenceNode& last_node);
  // Adds a relative jump source fixup at pos.
  // Jump source fixups are used to find offsets in the new bytecode that
  // contain jump sources.
  void AddJumpSourceFixup(int fixup, int pos);
  // Adds a relative jump destination fixup at pos.
  // Jump destination fixups are used to find offsets in the new bytecode that
  // can be jumped to.
  void AddJumpDestinationFixup(int fixup, int pos);
  // Sets an absolute jump destination fixup at pos.
  void SetJumpDestinationFixup(int fixup, int pos);
  // Prepare internal structures used to fixup jumps.
  void PrepareJumpStructures(const ZoneUnorderedMap<int, int>& jump_edges);
  // Updates all jump targets in the new bytecode.
  void FixJumps();
  // Update a single jump.
  void FixJump(int jump_source, int jump_destination);
  void AddSentinelFixups(int pos);
  void EmitArgument(int start_pc, const uint8_t* bytecode,
                    BytecodeArgumentMapping arg);
  int pc() const;
  Zone* zone() const;

  RegExpBytecodeWriter writer_;
  BytecodeSequenceNode* sequences_;
  // TODO(jgruber): We should also replace all of these raw offsets with
  // OpInfo. That should allow us to not expose the "raw" Emit publicly in the
  // Writer.
  // Jumps used in old bytecode.
  // Key: Jump source (offset where destination is stored in old bytecode)
  // Value: Destination
  ZoneMap<int, int> jump_edges_;
  // Jumps used in new bytecode.
  // Key: Jump source (offset where destination is stored in new bytecode)
  // Value: Destination
  ZoneMap<int, int> jump_edges_mapped_;
  // Number of times a jump destination is used within the bytecode.
  // Key: Jump destination (offset in old bytecode).
  // Value: Number of times jump destination is used.
  ZoneMap<int, int> jump_usage_counts_;
  // Maps offsets in old bytecode to fixups of sources (delta to new bytecode).
  // Key: Offset in old bytecode from where the fixup is valid.
  // Value: Delta to map jump source from old bytecode to new bytecode in bytes.
  ZoneMap<int, int> jump_source_fixups_;
  // Maps offsets in old bytecode to fixups of destinations (delta to new
  // bytecode).
  // Key: Offset in old bytecode from where the fixup is valid.
  // Value: Delta to map jump destinations from old bytecode to new bytecode in
  // bytes.
  ZoneMap<int, int> jump_destination_fixups_;

  Zone* zone_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(RegExpBytecodePeephole);
};

template <typename T>
T GetValue(const uint8_t* buffer, int pos) {
  DCHECK(IsAligned(reinterpret_cast<Address>(buffer + pos), alignof(T)));
  return *reinterpret_cast<const T*>(buffer + pos);
}

int32_t GetArgumentValue(const uint8_t* bytecode, int offset, int length) {
  switch (length) {
    case 1:
      return GetValue<uint8_t>(bytecode, offset);
    case 2:
      return GetValue<int16_t>(bytecode, offset);
    case 4:
      return GetValue<int32_t>(bytecode, offset);
    default:
      UNREACHABLE();
  }
}

BytecodeSequenceNode::BytecodeSequenceNode(
    std::optional<RegExpBytecode> bytecode, Zone* zone)
    : bytecode_(bytecode),
      bytecode_replacement_(std::nullopt),
      index_in_sequence_(0),
      start_offset_(0),
      parent_(nullptr),
      children_(ZoneUnorderedMap<RegExpBytecode, BytecodeSequenceNode*>(zone)),
      argument_mapping_(zone->New<ZoneVector<BytecodeArgumentMapping>>(zone)),
      argument_check_(zone->New<ZoneLinkedList<BytecodeArgumentCheck>>(zone)),
      argument_ignored_(zone->New<ZoneLinkedList<BytecodeArgument>>(zone)),
      zone_(zone) {}

BytecodeSequenceNode& BytecodeSequenceNode::FollowedBy(
    RegExpBytecode bytecode) {
  if (children_.find(bytecode) == children_.end()) {
    BytecodeSequenceNode* new_node =
        zone()->New<BytecodeSequenceNode>(bytecode, zone());
    // If node is not the first in the sequence, set offsets and parent.
    if (bytecode_.has_value()) {
      new_node->start_offset_ =
          start_offset_ + RegExpBytecodes::Size(bytecode_.value());
      new_node->index_in_sequence_ = index_in_sequence_ + 1;
      new_node->parent_ = this;
    }
    children_[bytecode] = new_node;
  }

  return *children_[bytecode];
}

BytecodeSequenceNode& BytecodeSequenceNode::ReplaceWith(
    RegExpBytecode bytecode) {
  bytecode_replacement_ = bytecode;

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::MapArgument(
    OpInfo to_op_info, int from_bytecode_sequence_index, OpInfo from_op_info) {
  int src_offset = from_op_info.offset;
  int src_size = from_op_info.size();

  DCHECK_LE(from_bytecode_sequence_index, index_in_sequence_);
  DCHECK(BytecodeArgumentMappingCreatedInOrder(to_op_info));

  BytecodeSequenceNode& ref_node =
      GetNodeByIndexInSequence(from_bytecode_sequence_index);
  DCHECK_LT(src_offset, RegExpBytecodes::Size(ref_node.bytecode_.value()));

  int offset_from_start_of_sequence = ref_node.start_offset_ + src_offset;
  argument_mapping_->push_back(BytecodeArgumentMapping{
      offset_from_start_of_sequence, src_size, to_op_info});
  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::EmitOffsetAfterSequence(
    OpInfo op_info) {
  DCHECK(BytecodeArgumentMappingCreatedInOrder(op_info));
  argument_mapping_->push_back(BytecodeArgumentMapping{
      BytecodeArgumentMapping::Type::kOffsetAfterSequence, op_info});
  return *this;
}

bool BytecodeSequenceNode::BytecodeArgumentMappingCreatedInOrder(
    OpInfo op_info) {
  DCHECK(IsSequence());
  if (argument_mapping_->empty()) return true;

  const BytecodeArgumentMapping& m = argument_mapping_->back();
  int offset_after_last = m.new_offset() + m.new_length();
  // TODO(jgruber): It'd be more precise to distinguish between special and
  // basic operand types, but we currently don't expose that information
  // except through templates.
  int dst_size = op_info.size();
  int alignment = std::min(dst_size, kBytecodeAlignment);
  return RoundUp(offset_after_last, alignment) == op_info.offset;
}

BytecodeSequenceNode& BytecodeSequenceNode::IfArgumentEqualsOffset(
    OpInfo op_info, int check_byte_offset) {
  int size = op_info.size();
  int offset = op_info.offset;

  DCHECK_LT(offset, RegExpBytecodes::Size(bytecode_.value()));
  DCHECK(size == 1 || size == 2 || size == 4);

  int offset_from_start_of_sequence = start_offset_ + offset;

  argument_check_->push_back(BytecodeArgumentCheck{
      offset_from_start_of_sequence, size, check_byte_offset});

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::IfArgumentEqualsValueAtOffset(
    OpInfo this_op_info, int other_bytecode_index_in_sequence,
    OpInfo other_op_info) {
  int size_1 = this_op_info.size();
  int size_2 = other_op_info.size();

  DCHECK_LT(this_op_info.offset, RegExpBytecodes::Size(bytecode_.value()));
  DCHECK_LE(other_bytecode_index_in_sequence, index_in_sequence_);
  DCHECK_EQ(size_1, size_2);

  BytecodeSequenceNode& ref_node =
      GetNodeByIndexInSequence(other_bytecode_index_in_sequence);
  DCHECK_LT(other_op_info.offset,
            RegExpBytecodes::Size(ref_node.bytecode_.value()));

  int offset_from_start_of_sequence = start_offset_ + this_op_info.offset;
  int other_offset_from_start_of_sequence =
      ref_node.start_offset_ + other_op_info.offset;

  argument_check_->push_back(
      BytecodeArgumentCheck{offset_from_start_of_sequence, size_1,
                            other_offset_from_start_of_sequence, size_2});

  return *this;
}

BytecodeSequenceNode& BytecodeSequenceNode::IgnoreArgument(
    int bytecode_index_in_sequence, OpInfo op_info) {
  int size = op_info.size();
  int offset = op_info.offset;

  DCHECK(IsSequence());
  DCHECK_LE(bytecode_index_in_sequence, index_in_sequence_);

  BytecodeSequenceNode& ref_node =
      GetNodeByIndexInSequence(bytecode_index_in_sequence);
  DCHECK_LT(offset, RegExpBytecodes::Size(ref_node.bytecode_.value()));

  int offset_from_start_of_sequence = ref_node.start_offset_ + offset;

  argument_ignored_->push_back(
      BytecodeArgument{offset_from_start_of_sequence, size});

  return *this;
}

bool BytecodeSequenceNode::CheckArguments(const uint8_t* bytecode, int pc) {
  bool is_valid = true;
  for (auto check_iter = argument_check_->begin();
       check_iter != argument_check_->end() && is_valid; check_iter++) {
    auto value = GetArgumentValue(bytecode, pc + check_iter->offset(),
                                  check_iter->length());
    if (check_iter->type == BytecodeArgumentCheck::kCheckAddress) {
      is_valid &= value == pc + check_iter->check_offset;
    } else if (check_iter->type == BytecodeArgumentCheck::kCheckValue) {
      auto other_value = GetArgumentValue(
          bytecode, pc + check_iter->check_offset, check_iter->check_length);
      is_valid &= value == other_value;
    } else {
      UNREACHABLE();
    }
  }
  return is_valid;
}

bool BytecodeSequenceNode::IsSequence() const {
  return bytecode_replacement_.has_value();
}

int BytecodeSequenceNode::SequenceLength() const {
  return start_offset_ + RegExpBytecodes::Size(bytecode_.value());
}

RegExpBytecode BytecodeSequenceNode::OptimizedBytecode() const {
  return bytecode_replacement_.value();
}

BytecodeSequenceNode* BytecodeSequenceNode::Find(
    RegExpBytecode bytecode) const {
  auto found = children_.find(bytecode);
  if (found == children_.end()) return nullptr;
  return found->second;
}

size_t BytecodeSequenceNode::ArgumentSize() const {
  DCHECK(IsSequence());
  return argument_mapping_->size();
}

BytecodeArgumentMapping BytecodeSequenceNode::ArgumentMapping(
    size_t index) const {
  DCHECK(IsSequence());
  DCHECK(argument_mapping_ != nullptr);
  DCHECK_LT(index, argument_mapping_->size());

  return argument_mapping_->at(index);
}

ZoneLinkedList<BytecodeArgument>::iterator
BytecodeSequenceNode::ArgumentIgnoredBegin() const {
  DCHECK(IsSequence());
  DCHECK(argument_ignored_ != nullptr);
  return argument_ignored_->begin();
}

ZoneLinkedList<BytecodeArgument>::iterator
BytecodeSequenceNode::ArgumentIgnoredEnd() const {
  DCHECK(IsSequence());
  DCHECK(argument_ignored_ != nullptr);
  return argument_ignored_->end();
}

bool BytecodeSequenceNode::HasIgnoredArguments() const {
  return argument_ignored_ != nullptr;
}

BytecodeSequenceNode& BytecodeSequenceNode::GetNodeByIndexInSequence(
    int index_in_sequence) {
  DCHECK_LE(index_in_sequence, index_in_sequence_);

  if (index_in_sequence < index_in_sequence_) {
    DCHECK(parent_ != nullptr);
    return parent_->GetNodeByIndexInSequence(index_in_sequence);
  } else {
    return *this;
  }
}

Zone* BytecodeSequenceNode::zone() const { return zone_; }

RegExpBytecodePeephole::RegExpBytecodePeephole(
    Zone* zone, size_t buffer_size,
    const ZoneUnorderedMap<int, int>& jump_edges)
    : writer_(zone),
      sequences_(zone->New<BytecodeSequenceNode>(std::nullopt, zone)),
      jump_edges_(zone),
      jump_edges_mapped_(zone),
      jump_usage_counts_(zone),
      jump_source_fixups_(zone),
      jump_destination_fixups_(zone),
      zone_(zone) {
  writer_.buffer().reserve(buffer_size);
  PrepareJumpStructures(jump_edges);
  DefineStandardSequences();
  // Sentinel fixups at beginning of bytecode (position -1) so we don't have to
  // check for end of iterator inside the fixup loop.
  // In general fixups are deltas of original offsets of jump
  // sources/destinations (in the old bytecode) to find them in the new
  // bytecode. All jump targets are fixed after the new bytecode is fully
  // emitted in the internal buffer.
  AddSentinelFixups(-1);
  // Sentinel fixups at end of (old) bytecode so we don't have to check for
  // end of iterator inside the fixup loop.
  DCHECK_LE(buffer_size, std::numeric_limits<int>::max());
  AddSentinelFixups(static_cast<int>(buffer_size));
}

void RegExpBytecodePeephole::DefineStandardSequences() {
  using B = RegExpBytecode;
#define I(BYTECODE, OPERAND)                    \
  OpInfo::Get<RegExpBytecodeOperands<BYTECODE>, \
              RegExpBytecodeOperands<BYTECODE>::Operand::OPERAND>()
#define T(OPERAND) I(Target, OPERAND)

  // Commonly used sequences can be found by creating regexp bytecode traces
  // (--trace-regexp-bytecodes) and using v8/tools/regexp-sequences.py.

  {
    static constexpr auto Target = B::kSkipUntilBitInTable;
    CreateSequence(B::kLoadCurrentCharacter)
        .FollowedBy(B::kCheckBitInTable)
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 0, I(B::kLoadCurrentCharacter, cp_offset))
        .MapArgument(T(advance_by), 2, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(table), 1, I(B::kCheckBitInTable, table))
        .MapArgument(T(on_match), 1, I(B::kCheckBitInTable, on_bit_set))
        .MapArgument(T(on_no_match), 0, I(B::kLoadCurrentCharacter, on_failure))
        .IgnoreArgument(2, I(B::kAdvanceCpAndGoto, on_goto));
  }

  {
    static constexpr auto Target = B::kSkipUntilCharPosChecked;
    CreateSequence(B::kCheckPosition)
        .FollowedBy(B::kLoadCurrentCharacterUnchecked)
        .FollowedBy(B::kCheckCharacter)
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 1,
                     I(B::kLoadCurrentCharacterUnchecked, cp_offset))
        .MapArgument(T(advance_by), 3, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(character), 2, I(B::kCheckCharacter, character))
        .MapArgument(T(eats_at_least), 0, I(B::kCheckPosition, cp_offset))
        .MapArgument(T(on_match), 2, I(B::kCheckCharacter, on_equal))
        .MapArgument(T(on_no_match), 0, I(B::kCheckPosition, on_failure))
        .IgnoreArgument(3, I(B::kAdvanceCpAndGoto, on_goto));
  }

  {
    static constexpr auto Target = B::kSkipUntilCharAnd;
    CreateSequence(B::kCheckPosition)
        .FollowedBy(B::kLoadCurrentCharacterUnchecked)
        .FollowedBy(B::kCheckCharacterAfterAnd)
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 1,
                     I(B::kLoadCurrentCharacterUnchecked, cp_offset))
        .MapArgument(T(advance_by), 3, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(character), 2, I(B::kCheckCharacterAfterAnd, character))
        .MapArgument(T(mask), 2, I(B::kCheckCharacterAfterAnd, mask))
        .MapArgument(T(eats_at_least), 0, I(B::kCheckPosition, cp_offset))
        .MapArgument(T(on_match), 2, I(B::kCheckCharacterAfterAnd, on_equal))
        .MapArgument(T(on_no_match), 0, I(B::kCheckPosition, on_failure))
        .IgnoreArgument(3, I(B::kAdvanceCpAndGoto, on_goto));
  }

  // TODO(pthier): It might make sense for short sequences like this one to only
  // optimize them if the resulting optimization is not longer than the current
  // one. This could be the case if there are jumps inside the sequence and we
  // have to replicate parts of the sequence. A method to mark such sequences
  // might be useful.
  {
    static constexpr auto Target = B::kSkipUntilChar;
    CreateSequence(B::kLoadCurrentCharacter)
        .FollowedBy(B::kCheckCharacter)
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 0, I(B::kLoadCurrentCharacter, cp_offset))
        .MapArgument(T(advance_by), 2, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(character), 1, I(B::kCheckCharacter, character))
        .MapArgument(T(on_match), 1, I(B::kCheckCharacter, on_equal))
        .MapArgument(T(on_no_match), 0, I(B::kLoadCurrentCharacter, on_failure))
        .IgnoreArgument(2, I(B::kAdvanceCpAndGoto, on_goto));
  }

  {
    static constexpr auto Target = B::kSkipUntilCharOrChar;
    CreateSequence(B::kLoadCurrentCharacter)
        .FollowedBy(B::kCheckCharacter)
        .FollowedBy(B::kCheckCharacter)
        .IfArgumentEqualsValueAtOffset(I(B::kCheckCharacter, on_equal), 1,
                                       I(B::kCheckCharacter, on_equal))
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 0, I(B::kLoadCurrentCharacter, cp_offset))
        .MapArgument(T(advance_by), 3, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(char1), 1, I(B::kCheckCharacter, character))
        .MapArgument(T(char2), 2, I(B::kCheckCharacter, character))
        .MapArgument(T(on_match), 1, I(B::kCheckCharacter, on_equal))
        .MapArgument(T(on_no_match), 0, I(B::kLoadCurrentCharacter, on_failure))
        .IgnoreArgument(2, I(B::kCheckCharacter, on_equal))
        .IgnoreArgument(3, I(B::kAdvanceCpAndGoto, on_goto));
  }

  {
    static constexpr auto Target = B::kSkipUntilGtOrNotBitInTable;
    CreateSequence(B::kLoadCurrentCharacter)
        .FollowedBy(B::kCheckCharacterGT)
        // Sequence is only valid if the jump target of kCheckCharacterGT is the
        // first bytecode AFTER the whole sequence.
        .IfArgumentEqualsOffset(I(B::kCheckCharacterGT, on_greater), 56)
        .FollowedBy(B::kCheckBitInTable)
        // Sequence is only valid if the jump target of kCheckBitInTable is
        // the kAdvanceCpAndGoto bytecode at the end of the sequence.
        .IfArgumentEqualsOffset(I(B::kCheckBitInTable, on_bit_set), 48)
        .FollowedBy(B::kGoTo)
        // Sequence is only valid if the jump target of kGoTo is the same as the
        // jump target of kCheckCharacterGT (i.e. both jump to the first
        // bytecode AFTER the whole sequence.
        .IfArgumentEqualsValueAtOffset(I(B::kGoTo, label), 1,
                                       I(B::kCheckCharacterGT, on_greater))
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 0, I(B::kLoadCurrentCharacter, cp_offset))
        .MapArgument(T(advance_by), 4, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(character), 1, I(B::kCheckCharacterGT, limit))
        .MapArgument(T(table), 2, I(B::kCheckBitInTable, table))
        .MapArgument(T(on_match), 1, I(B::kCheckCharacterGT, on_greater))
        .MapArgument(T(on_no_match), 0, I(B::kLoadCurrentCharacter, on_failure))
        .IgnoreArgument(2, I(B::kCheckBitInTable, on_bit_set))
        .IgnoreArgument(3, I(B::kGoTo, label))
        .IgnoreArgument(4, I(B::kAdvanceCpAndGoto, on_goto));
  }
  {
    static constexpr auto Target = B::kSkipUntilOneOfMasked;
    CreateSequence(B::kCheckPosition)
        .FollowedBy(B::kLoad4CurrentCharsUnchecked)
        .FollowedBy(B::kAndCheck4Chars)
        // Jump target is the offset of the next AndCheck4Chars (right after
        // AdvanceCpAndGoto).
        .IfArgumentEqualsOffset(I(B::kAndCheck4Chars, on_equal), 0x24)
        .FollowedBy(B::kAdvanceCpAndGoto)
        .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto), 0)
        .FollowedBy(B::kAndCheck4Chars)
        .FollowedBy(B::kAndCheckNot4Chars)
        // Jump target is AdvanceCpAndGoto.
        .IfArgumentEqualsOffset(I(B::kAndCheckNot4Chars, on_not_equal), 0x1c)
        .ReplaceWith(Target)
        .MapArgument(T(cp_offset), 1,
                     I(B::kLoad4CurrentCharsUnchecked, cp_offset))
        .MapArgument(T(advance_by), 3, I(B::kAdvanceCpAndGoto, by))
        .MapArgument(T(both_chars), 2, I(B::kAndCheck4Chars, characters))
        .MapArgument(T(both_mask), 2, I(B::kAndCheck4Chars, mask))
        .MapArgument(T(max_offset), 0, I(B::kCheckPosition, cp_offset))
        .MapArgument(T(chars1), 4, I(B::kAndCheck4Chars, characters))
        .MapArgument(T(mask1), 4, I(B::kAndCheck4Chars, mask))
        .MapArgument(T(chars2), 5, I(B::kAndCheckNot4Chars, characters))
        .MapArgument(T(mask2), 5, I(B::kAndCheckNot4Chars, mask))
        .MapArgument(T(on_match1), 4, I(B::kAndCheck4Chars, on_equal))
        .EmitOffsetAfterSequence(T(on_match2))
        .MapArgument(T(on_failure), 0, I(B::kCheckPosition, on_failure))
        .IgnoreArgument(3, I(B::kAdvanceCpAndGoto, on_goto))
        .IgnoreArgument(2, I(B::kAndCheck4Chars, on_equal));
  }
  // TODO(jgruber): kSkipUntilBitInTable is itself both a
  // peephole-generated bc, AND a standard bytecode. Either we run to a fixed
  // point, or we need to be careful around ordering (and specify the seq based
  // on basic bytecodes).
  //
  // The original bytecode sequence for kSkipUntilOneOfMasked3 is:
  //
  // sequence offset name
  // bc0   0  SkipUntilBitInTable
  // bc1  20  CheckPosition
  // bc2  28  Load4CurrentCharsUnchecked
  // bc3  2c  AndCheck4Chars
  // bc4  3c  AdvanceCpAndGoto
  // bc5  48  Load4CurrentChars
  // bc6  4c  AndCheck4Chars
  // bc7  5c  AndCheck4Chars
  // bc8  6c  AndCheckNot4Chars
  {
    static constexpr int kOffsetOfBc0SkipUntilBitInTable = 0x0;
    static constexpr int kOffsetOfBc1CheckCurrentPosition = 0x20;
    static constexpr int kOffsetOfBc4AdvanceBcAndGoto = 0x3c;
    static constexpr auto Target = B::kSkipUntilOneOfMasked3;
    BytecodeSequenceNode& s0 =
        CreateSequence(B::kSkipUntilBitInTable)
            .IfArgumentEqualsOffset(I(B::kSkipUntilBitInTable, on_no_match),
                                    kOffsetOfBc1CheckCurrentPosition)
            .IfArgumentEqualsOffset(I(B::kSkipUntilBitInTable, on_no_match),
                                    kOffsetOfBc1CheckCurrentPosition);

    DCHECK_EQ(s0.SequenceLength(), 0x20);
    DCHECK_EQ(s0.SequenceLength(), kOffsetOfBc1CheckCurrentPosition);
    static constexpr int kOffsetOfBc5Load4CurrentChars = 0x44;
    BytecodeSequenceNode& s1 =
        s0.FollowedBy(B::kCheckPosition)
            .FollowedBy(B::kLoad4CurrentCharsUnchecked)
            .FollowedBy(B::kAndCheck4Chars)
            .IfArgumentEqualsOffset(I(B::kAndCheck4Chars, on_equal),
                                    kOffsetOfBc5Load4CurrentChars);

    DCHECK_EQ(s1.SequenceLength(), 0x3c);
    DCHECK_EQ(s1.SequenceLength(), kOffsetOfBc4AdvanceBcAndGoto);
    BytecodeSequenceNode& s2 =
        s1.FollowedBy(B::kAdvanceCpAndGoto)
            .IfArgumentEqualsOffset(I(B::kAdvanceCpAndGoto, on_goto),
                                    kOffsetOfBc0SkipUntilBitInTable);

    DCHECK_EQ(s2.SequenceLength(), 0x44);
    DCHECK_EQ(s2.SequenceLength(), kOffsetOfBc5Load4CurrentChars);
    BytecodeSequenceNode& s3 =
        s2.FollowedBy(B::kLoad4CurrentChars)
            .IfArgumentEqualsOffset(I(B::kLoad4CurrentChars, on_failure),
                                    kOffsetOfBc4AdvanceBcAndGoto)
            .FollowedBy(B::kAndCheck4Chars)
            .FollowedBy(B::kAndCheck4Chars)
            .FollowedBy(B::kAndCheckNot4Chars)
            .IfArgumentEqualsOffset(I(B::kAndCheckNot4Chars, on_not_equal),
                                    kOffsetOfBc4AdvanceBcAndGoto);

    s3.ReplaceWith(Target)
        .MapArgument(T(bc0_cp_offset), 0, I(B::kSkipUntilBitInTable, cp_offset))
        .MapArgument(T(bc0_advance_by), 0,
                     I(B::kSkipUntilBitInTable, advance_by))
        .MapArgument(T(bc0_table), 0, I(B::kSkipUntilBitInTable, table))
        .IgnoreArgument(0, I(B::kSkipUntilBitInTable, on_match))
        .IgnoreArgument(0, I(B::kSkipUntilBitInTable, on_no_match))
        .MapArgument(T(bc1_cp_offset), 1, I(B::kCheckPosition, cp_offset))
        .MapArgument(T(bc1_on_failure), 1, I(B::kCheckPosition, on_failure))
        .MapArgument(T(bc2_cp_offset), 2,
                     I(B::kLoad4CurrentCharsUnchecked, cp_offset))
        .MapArgument(T(bc3_characters), 3, I(B::kAndCheck4Chars, characters))
        .MapArgument(T(bc3_mask), 3, I(B::kAndCheck4Chars, mask))
        .IgnoreArgument(3, I(B::kAndCheck4Chars, on_equal))
        .MapArgument(T(bc4_by), 4, I(B::kAdvanceCpAndGoto, by))
        .IgnoreArgument(4, I(B::kAdvanceCpAndGoto, on_goto))
        .MapArgument(T(bc5_cp_offset), 5, I(B::kLoad4CurrentChars, cp_offset))
        .IgnoreArgument(5, I(B::kLoad4CurrentChars, on_failure))
        .MapArgument(T(bc6_characters), 6, I(B::kAndCheck4Chars, characters))
        .MapArgument(T(bc6_mask), 6, I(B::kAndCheck4Chars, mask))
        .MapArgument(T(bc6_on_equal), 6, I(B::kAndCheck4Chars, on_equal))
        .MapArgument(T(bc7_characters), 7, I(B::kAndCheck4Chars, characters))
        .MapArgument(T(bc7_mask), 7, I(B::kAndCheck4Chars, mask))
        .MapArgument(T(bc7_on_equal), 7, I(B::kAndCheck4Chars, on_equal))
        .MapArgument(T(bc8_characters), 8, I(B::kAndCheckNot4Chars, characters))
        .MapArgument(T(bc8_mask), 8, I(B::kAndCheckNot4Chars, mask))
        .IgnoreArgument(8, I(B::kAndCheckNot4Chars, on_not_equal))
        .EmitOffsetAfterSequence(T(fallthrough_jump_target));
  }

#undef I
#undef T
}
bool RegExpBytecodePeephole::OptimizeBytecode(const uint8_t* bytecode,
                                              int length) {
  int old_pc = 0;
  bool did_optimize = false;

  while (old_pc < length) {
    int replaced_len = TryOptimizeSequence(bytecode, length, old_pc);
    if (replaced_len > 0) {
      old_pc += replaced_len;
      did_optimize = true;
    } else {
      int bc_len = RegExpBytecodes::Size(bytecode[old_pc]);
      writer_.EmitRawBytecodeStream(bytecode + old_pc, bc_len);
      old_pc += bc_len;
    }
  }

  if (did_optimize) {
    FixJumps();
  }

  return did_optimize;
}

void RegExpBytecodePeephole::CopyOptimizedBytecode(uint8_t* to_address) const {
  MemCopy(to_address, writer_.buffer().begin(), Length());
}

int RegExpBytecodePeephole::Length() const { return pc(); }

BytecodeSequenceNode& RegExpBytecodePeephole::CreateSequence(
    RegExpBytecode bytecode) {
  DCHECK(sequences_ != nullptr);

  return sequences_->FollowedBy(bytecode);
}

int RegExpBytecodePeephole::TryOptimizeSequence(const uint8_t* bytecode,
                                                int bytecode_length,
                                                int start_pc) {
  BytecodeSequenceNode* seq_node = sequences_;
  BytecodeSequenceNode* valid_seq_end = nullptr;

  int current_pc = start_pc;

  // Check for the longest valid sequence matching any of the pre-defined
  // sequences in the Trie data structure.
  while (current_pc < bytecode_length) {
    seq_node = seq_node->Find(RegExpBytecodes::FromByte(bytecode[current_pc]));
    if (seq_node == nullptr) break;
    if (!seq_node->CheckArguments(bytecode, start_pc)) break;

    if (seq_node->IsSequence()) valid_seq_end = seq_node;
    current_pc += RegExpBytecodes::Size(bytecode[current_pc]);
  }

  if (valid_seq_end) {
    EmitOptimization(start_pc, bytecode, *valid_seq_end);
    return valid_seq_end->SequenceLength();
  }

  return 0;
}

void RegExpBytecodePeephole::EmitOptimization(
    int start_pc, const uint8_t* bytecode,
    const BytecodeSequenceNode& last_node) {
  int optimized_start_pc = pc();
  // Jump sources that are mapped or marked as unused will be deleted at the end
  // of this method. We don't delete them immediately as we might need the
  // information when we have to preserve bytecodes at the end.
  // TODO(pthier): Replace with a stack-allocated data structure.
  ZoneLinkedList<int> delete_jumps(zone());
  // List of offsets in the optimized sequence that need to be patched to the
  // offset value right after the optimized sequence.
  ZoneLinkedList<uint32_t> after_sequence_offsets(zone());

  const RegExpBytecode bc = last_node.OptimizedBytecode();
  writer_.EmitBytecode(bc);

  for (size_t arg_idx = 0; arg_idx < last_node.ArgumentSize(); arg_idx++) {
    BytecodeArgumentMapping arg_map = last_node.ArgumentMapping(arg_idx);
    if (arg_map.type() == BytecodeArgumentMapping::Type::kDefault) {
      int arg_pos = start_pc + arg_map.offset();
      // If we map any jump source we mark the old source for deletion and
      // insert a new jump.
      auto jump_edge_iter = jump_edges_.find(arg_pos);
      if (jump_edge_iter != jump_edges_.end()) {
        int jump_source = jump_edge_iter->first;
        int jump_destination = jump_edge_iter->second;
        // Add new jump edge add current position.
        jump_edges_mapped_.emplace(optimized_start_pc + arg_map.new_offset(),
                                   jump_destination);
        // Mark old jump edge for deletion.
        delete_jumps.push_back(jump_source);
        // Decrement usage count of jump destination.
        auto jump_count_iter = jump_usage_counts_.find(jump_destination);
        DCHECK(jump_count_iter != jump_usage_counts_.end());
        int& usage_count = jump_count_iter->second;
        --usage_count;
      }
      // TODO(pthier): DCHECK that mapped arguments are never sources of jumps
      // to destinations inside the sequence.
      EmitArgument(start_pc, bytecode, arg_map);
    } else {
      DCHECK_EQ(arg_map.type(),
                BytecodeArgumentMapping::Type::kOffsetAfterSequence);
      after_sequence_offsets.push_back(optimized_start_pc +
                                       arg_map.new_offset());
      // Reserve space to overwrite later with the pc after this sequence.
      writer_.Emit<uint32_t>(0, arg_map.new_offset());
    }
  }

  // Final alignment.
  writer_.Finalize(bc);

  DCHECK_EQ(pc(), optimized_start_pc +
                      RegExpBytecodes::Size(last_node.OptimizedBytecode()));

  // Remove jumps from arguments we ignore.
  if (last_node.HasIgnoredArguments()) {
    for (auto ignored_arg = last_node.ArgumentIgnoredBegin();
         ignored_arg != last_node.ArgumentIgnoredEnd(); ignored_arg++) {
      auto jump_edge_iter = jump_edges_.find(start_pc + ignored_arg->offset());
      if (jump_edge_iter != jump_edges_.end()) {
        int jump_source = jump_edge_iter->first;
        int jump_destination = jump_edge_iter->second;
        // Mark old jump edge for deletion.
        delete_jumps.push_back(jump_source);
        // Decrement usage count of jump destination.
        auto jump_count_iter = jump_usage_counts_.find(jump_destination);
        DCHECK(jump_count_iter != jump_usage_counts_.end());
        int& usage_count = jump_count_iter->second;
        --usage_count;
      }
    }
  }

  int fixup_length = RegExpBytecodes::Size(bc) - last_node.SequenceLength();

  // Check if there are any jumps inside the old sequence.
  // If so we have to keep the bytecodes that are jumped to around.
  auto jump_destination_candidate = jump_usage_counts_.upper_bound(start_pc);
  int jump_candidate_destination = jump_destination_candidate->first;
  int jump_candidate_count = jump_destination_candidate->second;
  // Jump destinations only jumped to from inside the sequence will be ignored.
  while (jump_destination_candidate != jump_usage_counts_.end() &&
         jump_candidate_count == 0) {
    ++jump_destination_candidate;
    jump_candidate_destination = jump_destination_candidate->first;
    jump_candidate_count = jump_destination_candidate->second;
  }

  int preserve_from = start_pc + last_node.SequenceLength();
  if (jump_destination_candidate != jump_usage_counts_.end() &&
      jump_candidate_destination < start_pc + last_node.SequenceLength()) {
    preserve_from = jump_candidate_destination;
    // Check if any jump in the sequence we are preserving has a jump
    // destination inside the optimized sequence before the current position we
    // want to preserve. If so we have to preserve all bytecodes starting at
    // this jump destination.
    for (auto jump_iter = jump_edges_.lower_bound(preserve_from);
         jump_iter != jump_edges_.end() &&
         jump_iter->first /* jump source */ <
             start_pc + last_node.SequenceLength();
         ++jump_iter) {
      int jump_destination = jump_iter->second;
      if (jump_destination > start_pc && jump_destination < preserve_from) {
        preserve_from = jump_destination;
      }
    }

    // We preserve everything to the end of the sequence. This is conservative
    // since it would be enough to preserve all bytecodes up to an unconditional
    // jump.
    int preserve_length = start_pc + last_node.SequenceLength() - preserve_from;
    fixup_length += preserve_length;
    // Jumps after the start of the preserved sequence need fixup.
    AddJumpSourceFixup(fixup_length,
                       start_pc + last_node.SequenceLength() - preserve_length);
    // All jump targets after the start of the optimized sequence need to be
    // fixed relative to the length of the optimized sequence including
    // bytecodes we preserved.
    AddJumpDestinationFixup(fixup_length, start_pc + 1);
    // Jumps to the sequence we preserved need absolute fixup as they could
    // occur before or after the sequence.
    SetJumpDestinationFixup(pc() - preserve_from, preserve_from);
    writer_.EmitRawBytecodeStream(bytecode + preserve_from, preserve_length);
  } else {
    AddJumpDestinationFixup(fixup_length, start_pc + 1);
    // Jumps after the end of the old sequence need fixup.
    AddJumpSourceFixup(fixup_length, start_pc + last_node.SequenceLength());
  }

  // Delete jumps we definitely don't need anymore
  for (int del : delete_jumps) {
    if (del < preserve_from) {
      jump_edges_.erase(del);
    }
  }

  for (uint32_t offset : after_sequence_offsets) {
    DCHECK_EQ(writer_.buffer()[offset], 0);
    writer_.OverwriteValue<uint32_t>(offset, pc());
  }
}

void RegExpBytecodePeephole::AddJumpSourceFixup(int fixup, int pos) {
  auto previous_fixup = jump_source_fixups_.lower_bound(pos);
  DCHECK(previous_fixup != jump_source_fixups_.end());
  DCHECK(previous_fixup != jump_source_fixups_.begin());

  int previous_fixup_value = (--previous_fixup)->second;
  jump_source_fixups_[pos] = previous_fixup_value + fixup;
}

void RegExpBytecodePeephole::AddJumpDestinationFixup(int fixup, int pos) {
  auto previous_fixup = jump_destination_fixups_.lower_bound(pos);
  DCHECK(previous_fixup != jump_destination_fixups_.end());
  DCHECK(previous_fixup != jump_destination_fixups_.begin());

  int previous_fixup_value = (--previous_fixup)->second;
  jump_destination_fixups_[pos] = previous_fixup_value + fixup;
}

void RegExpBytecodePeephole::SetJumpDestinationFixup(int fixup, int pos) {
  auto previous_fixup = jump_destination_fixups_.lower_bound(pos);
  DCHECK(previous_fixup != jump_destination_fixups_.end());
  DCHECK(previous_fixup != jump_destination_fixups_.begin());

  int previous_fixup_value = (--previous_fixup)->second;
  jump_destination_fixups_.emplace(pos, fixup);
  jump_destination_fixups_.emplace(pos + 1, previous_fixup_value);
}

void RegExpBytecodePeephole::PrepareJumpStructures(
    const ZoneUnorderedMap<int, int>& jump_edges) {
  for (auto jump_edge : jump_edges) {
    int jump_source = jump_edge.first;
    int jump_destination = jump_edge.second;

    jump_edges_.emplace(jump_source, jump_destination);
    jump_usage_counts_[jump_destination]++;
  }
}

void RegExpBytecodePeephole::FixJumps() {
  int position_fixup = 0;
  // Next position where fixup changes.
  auto next_source_fixup = jump_source_fixups_.lower_bound(0);
  int next_source_fixup_offset = next_source_fixup->first;
  int next_source_fixup_value = next_source_fixup->second;

  for (auto jump_edge : jump_edges_) {
    int jump_source = jump_edge.first;
    int jump_destination = jump_edge.second;
    while (jump_source >= next_source_fixup_offset) {
      position_fixup = next_source_fixup_value;
      ++next_source_fixup;
      next_source_fixup_offset = next_source_fixup->first;
      next_source_fixup_value = next_source_fixup->second;
    }
    jump_source += position_fixup;

    FixJump(jump_source, jump_destination);
  }

  // Mapped jump edges don't need source fixups, as the position already is an
  // offset in the new bytecode.
  for (auto jump_edge : jump_edges_mapped_) {
    int jump_source = jump_edge.first;
    int jump_destination = jump_edge.second;

    FixJump(jump_source, jump_destination);
  }
}

void RegExpBytecodePeephole::FixJump(int jump_source, int jump_destination) {
  int fixed_jump_destination =
      jump_destination +
      (--jump_destination_fixups_.upper_bound(jump_destination))->second;
  DCHECK_LT(fixed_jump_destination, Length());
#ifdef DEBUG
  // TODO(pthier): This check could be better if we track the bytecodes
  // actually used and check if we jump to one of them.
  uint8_t jump_bc = writer_.buffer()[fixed_jump_destination];
  DCHECK_GT(jump_bc, 0);
  DCHECK_LT(jump_bc, RegExpBytecodes::kCount);
#endif

  if (jump_destination != fixed_jump_destination) {
    writer_.OverwriteValue<uint32_t>(jump_source, fixed_jump_destination);
  }
}

void RegExpBytecodePeephole::AddSentinelFixups(int pos) {
  jump_source_fixups_.emplace(pos, 0);
  jump_destination_fixups_.emplace(pos, 0);
}

void RegExpBytecodePeephole::EmitArgument(int start_pc, const uint8_t* bytecode,
                                          BytecodeArgumentMapping arg) {
  const RegExpBytecodeOperandType type = arg.new_operand_type();

  switch (type) {
#define CASE(Name, ...)                                                        \
  case RegExpBytecodeOperandType::k##Name: {                                   \
    DCHECK_LE(arg.length(), kInt32Size);                                       \
    using CType =                                                              \
        RegExpOperandTypeTraits<RegExpBytecodeOperandType::k##Name>::kCType;   \
    CType value = static_cast<CType>(                                          \
        GetArgumentValue(bytecode, start_pc + arg.offset(), arg.length()));    \
    writer_.EmitOperand<RegExpBytecodeOperandType::k##Name>(value,             \
                                                            arg.new_offset()); \
  } break;
    BASIC_BYTECODE_OPERAND_TYPE_LIST(CASE)
    BASIC_BYTECODE_OPERAND_TYPE_LIMITS_LIST(CASE)
#undef CASE
    case RegExpBytecodeOperandType::kBitTable: {
      DCHECK_EQ(arg.length(), 16);
      writer_.EmitOperand<RegExpBytecodeOperandType::kBitTable>(
          bytecode + start_pc + arg.offset(), arg.new_offset());
    } break;
    default:
      UNREACHABLE();
  }
}

int RegExpBytecodePeephole::pc() const { return writer_.pc(); }

Zone* RegExpBytecodePeephole::zone() const { return zone_; }

}  // namespace

// static
DirectHandle<TrustedByteArray>
RegExpBytecodePeepholeOptimization::OptimizeBytecode(
    Isolate* isolate, Zone* zone, DirectHandle<String> source,
    const uint8_t* bytecode, int length,
    const ZoneUnorderedMap<int, int>& jump_edges) {
  RegExpBytecodePeephole peephole(zone, length, jump_edges);
  bool did_optimize = peephole.OptimizeBytecode(bytecode, length);
  DirectHandle<TrustedByteArray> array =
      isolate->factory()->NewTrustedByteArray(peephole.Length());
  peephole.CopyOptimizedBytecode(array->begin());

  if (did_optimize && v8_flags.trace_regexp_peephole_optimization) {
    PrintF("Original Bytecode:\n");
    RegExpBytecodeDisassemble(bytecode, length, source->ToCString().get());
    PrintF("Optimized Bytecode:\n");
    RegExpBytecodeDisassemble(array->begin(), peephole.Length(),
                              source->ToCString().get());
  }

  return array;
}

}  // namespace internal
}  // namespace v8
