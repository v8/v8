// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_MATCHERS_H_
#define V8_COMPILER_NODE_MATCHERS_H_

#include "src/compiler/generic-node.h"
#include "src/compiler/generic-node-inl.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/unique.h"

namespace v8 {
namespace internal {
namespace compiler {

// A pattern matcher for nodes.
struct NodeMatcher {
  explicit NodeMatcher(Node* node) : node_(node) {}

  Node* node() const { return node_; }
  const Operator* op() const { return node()->op(); }
  IrOpcode::Value opcode() const { return node()->opcode(); }

  bool HasProperty(Operator::Property property) const {
    return op()->HasProperty(property);
  }
  Node* InputAt(int index) const { return node()->InputAt(index); }

#define DEFINE_IS_OPCODE(Opcode) \
  bool Is##Opcode() const { return opcode() == IrOpcode::k##Opcode; }
  ALL_OP_LIST(DEFINE_IS_OPCODE)
#undef DEFINE_IS_OPCODE

 private:
  Node* node_;
};


// A pattern matcher for abitrary value constants.
template <typename T, IrOpcode::Value kOpcode>
struct ValueMatcher : public NodeMatcher {
  explicit ValueMatcher(Node* node)
      : NodeMatcher(node), value_(), has_value_(opcode() == kOpcode) {
    if (has_value_) {
      value_ = OpParameter<T>(node);
    }
  }

  bool HasValue() const { return has_value_; }
  const T& Value() const {
    DCHECK(HasValue());
    return value_;
  }

  bool Is(const T& value) const {
    return this->HasValue() && this->Value() == value;
  }

  bool IsInRange(const T& low, const T& high) const {
    return this->HasValue() && low <= this->Value() && this->Value() <= high;
  }

 private:
  T value_;
  bool has_value_;
};


// A pattern matcher for integer constants.
template <typename T, IrOpcode::Value kOpcode>
struct IntMatcher FINAL : public ValueMatcher<T, kOpcode> {
  explicit IntMatcher(Node* node) : ValueMatcher<T, kOpcode>(node) {}

  bool IsPowerOf2() const {
    return this->HasValue() && this->Value() > 0 &&
           (this->Value() & (this->Value() - 1)) == 0;
  }
};

typedef IntMatcher<int32_t, IrOpcode::kInt32Constant> Int32Matcher;
typedef IntMatcher<uint32_t, IrOpcode::kInt32Constant> Uint32Matcher;
typedef IntMatcher<int64_t, IrOpcode::kInt64Constant> Int64Matcher;
typedef IntMatcher<uint64_t, IrOpcode::kInt64Constant> Uint64Matcher;
#if V8_HOST_ARCH_32_BIT
typedef Int32Matcher IntPtrMatcher;
typedef Uint32Matcher UintPtrMatcher;
#else
typedef Int64Matcher IntPtrMatcher;
typedef Uint64Matcher UintPtrMatcher;
#endif


// A pattern matcher for floating point constants.
template <typename T, IrOpcode::Value kOpcode>
struct FloatMatcher FINAL : public ValueMatcher<T, kOpcode> {
  explicit FloatMatcher(Node* node) : ValueMatcher<T, kOpcode>(node) {}

  bool IsNaN() const { return this->HasValue() && std::isnan(this->Value()); }
};

typedef FloatMatcher<float, IrOpcode::kFloat32Constant> Float32Matcher;
typedef FloatMatcher<double, IrOpcode::kFloat64Constant> Float64Matcher;
typedef FloatMatcher<double, IrOpcode::kNumberConstant> NumberMatcher;


// A pattern matcher for heap object constants.
template <typename T>
struct HeapObjectMatcher FINAL
    : public ValueMatcher<Unique<T>, IrOpcode::kHeapConstant> {
  explicit HeapObjectMatcher(Node* node)
      : ValueMatcher<Unique<T>, IrOpcode::kHeapConstant>(node) {}
};


// For shorter pattern matching code, this struct matches both the left and
// right hand sides of a binary operation and can put constants on the right
// if they appear on the left hand side of a commutative operation.
template <typename Left, typename Right>
struct BinopMatcher : public NodeMatcher {
  explicit BinopMatcher(Node* node)
      : NodeMatcher(node), left_(InputAt(0)), right_(InputAt(1)) {
    if (HasProperty(Operator::kCommutative)) PutConstantOnRight();
  }

  const Left& left() const { return left_; }
  const Right& right() const { return right_; }

  bool IsFoldable() const { return left().HasValue() && right().HasValue(); }
  bool LeftEqualsRight() const { return left().node() == right().node(); }

 protected:
  void SwapInputs() {
    std::swap(left_, right_);
    node()->ReplaceInput(0, left().node());
    node()->ReplaceInput(1, right().node());
  }

 private:
  void PutConstantOnRight() {
    if (left().HasValue() && !right().HasValue()) {
      SwapInputs();
    }
  }

  Left left_;
  Right right_;
};

typedef BinopMatcher<Int32Matcher, Int32Matcher> Int32BinopMatcher;
typedef BinopMatcher<Uint32Matcher, Uint32Matcher> Uint32BinopMatcher;
typedef BinopMatcher<Int64Matcher, Int64Matcher> Int64BinopMatcher;
typedef BinopMatcher<Uint64Matcher, Uint64Matcher> Uint64BinopMatcher;
typedef BinopMatcher<IntPtrMatcher, IntPtrMatcher> IntPtrBinopMatcher;
typedef BinopMatcher<UintPtrMatcher, UintPtrMatcher> UintPtrBinopMatcher;
typedef BinopMatcher<Float64Matcher, Float64Matcher> Float64BinopMatcher;
typedef BinopMatcher<NumberMatcher, NumberMatcher> NumberBinopMatcher;

struct Int32AddMatcher : public Int32BinopMatcher {
  explicit Int32AddMatcher(Node* node)
      : Int32BinopMatcher(node), scale_exponent_(-1) {
    PutScaledInputOnLeft();
  }

  bool HasScaledInput() const { return scale_exponent_ != -1; }
  Node* ScaledInput() const {
    DCHECK(HasScaledInput());
    return left().node()->InputAt(0);
  }
  int ScaleExponent() const {
    DCHECK(HasScaledInput());
    return scale_exponent_;
  }

 private:
  int GetInputScaleExponent(Node* node) const {
    if (node->opcode() == IrOpcode::kWord32Shl) {
      Int32BinopMatcher m(node);
      if (m.right().HasValue()) {
        int32_t value = m.right().Value();
        if (value >= 0 && value <= 3) {
          return value;
        }
      }
    } else if (node->opcode() == IrOpcode::kInt32Mul) {
      Int32BinopMatcher m(node);
      if (m.right().HasValue()) {
        int32_t value = m.right().Value();
        if (value == 1) {
          return 0;
        } else if (value == 2) {
          return 1;
        } else if (value == 4) {
          return 2;
        } else if (value == 8) {
          return 3;
        }
      }
    }
    return -1;
  }

  void PutScaledInputOnLeft() {
    scale_exponent_ = GetInputScaleExponent(right().node());
    if (scale_exponent_ >= 0) {
      int left_scale_exponent = GetInputScaleExponent(left().node());
      if (left_scale_exponent == -1) {
        SwapInputs();
      } else {
        scale_exponent_ = left_scale_exponent;
      }
    } else {
      scale_exponent_ = GetInputScaleExponent(left().node());
      if (scale_exponent_ == -1) {
        if (right().opcode() == IrOpcode::kInt32Add &&
            left().opcode() != IrOpcode::kInt32Add) {
          SwapInputs();
        }
      }
    }
  }

  int scale_exponent_;
};

struct ScaledWithOffsetMatcher {
  explicit ScaledWithOffsetMatcher(Node* node)
      : matches_(false),
        scaled_(NULL),
        scale_exponent_(0),
        offset_(NULL),
        constant_(NULL) {
    if (node->opcode() != IrOpcode::kInt32Add) return;

    // The Int32AddMatcher canonicalizes the order of constants and scale
    // factors that are used as inputs, so instead of enumerating all possible
    // patterns by brute force, checking for node clusters using the following
    // templates in the following order suffices to find all of the interesting
    // cases (S = scaled input, O = offset input, C = constant input):
    // (S + (O + C))
    // (S + (O + O))
    // (S + C)
    // (S + O)
    // ((S + C) + O)
    // ((S + O) + C)
    // ((O + C) + O)
    // ((O + O) + C)
    // (O + C)
    // (O + O)
    Int32AddMatcher base_matcher(node);
    Node* left = base_matcher.left().node();
    Node* right = base_matcher.right().node();
    if (base_matcher.HasScaledInput() && left->OwnedBy(node)) {
      scaled_ = base_matcher.ScaledInput();
      scale_exponent_ = base_matcher.ScaleExponent();
      if (right->opcode() == IrOpcode::kInt32Add && right->OwnedBy(node)) {
        Int32AddMatcher right_matcher(right);
        if (right_matcher.right().HasValue()) {
          // (S + (O + C))
          offset_ = right_matcher.left().node();
          constant_ = right_matcher.right().node();
        } else {
          // (S + (O + O))
          offset_ = right;
        }
      } else if (base_matcher.right().HasValue()) {
        // (S + C)
        constant_ = right;
      } else {
        // (S + O)
        offset_ = right;
      }
    } else {
      if (left->opcode() == IrOpcode::kInt32Add && left->OwnedBy(node)) {
        Int32AddMatcher left_matcher(left);
        Node* left_left = left_matcher.left().node();
        Node* left_right = left_matcher.right().node();
        if (left_matcher.HasScaledInput() && left_left->OwnedBy(left)) {
          scaled_ = left_matcher.ScaledInput();
          scale_exponent_ = left_matcher.ScaleExponent();
          if (left_matcher.right().HasValue()) {
            // ((S + C) + O)
            constant_ = left_right;
            offset_ = right;
          } else if (base_matcher.right().HasValue()) {
            // ((S + O) + C)
            offset_ = left_right;
            constant_ = right;
          } else {
            // (O + O)
            scaled_ = left;
            offset_ = right;
          }
        } else {
          if (left_matcher.right().HasValue()) {
            // ((O + C) + O)
            scaled_ = left_left;
            constant_ = left_right;
            offset_ = right;
          } else if (base_matcher.right().HasValue()) {
            // ((O + O) + C)
            scaled_ = left_left;
            offset_ = left_right;
            constant_ = right;
          } else {
            // (O + O)
            scaled_ = left;
            offset_ = right;
          }
        }
      } else {
        if (base_matcher.right().HasValue()) {
          // (O + C)
          offset_ = left;
          constant_ = right;
        } else {
          // (O + O)
          offset_ = left;
          scaled_ = right;
        }
      }
    }
    matches_ = true;
  }

  bool matches() const { return matches_; }
  Node* scaled() const { return scaled_; }
  int scale_exponent() const { return scale_exponent_; }
  Node* offset() const { return offset_; }
  Node* constant() const { return constant_; }

 private:
  bool matches_;

 protected:
  Node* scaled_;
  int scale_exponent_;
  Node* offset_;
  Node* constant_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_NODE_MATCHERS_H_
