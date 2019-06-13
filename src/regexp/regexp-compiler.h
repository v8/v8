// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_COMPILER_H_
#define V8_REGEXP_REGEXP_COMPILER_H_

#include "src/regexp/jsregexp.h"  // TODO(jgruber): Remove if possible.
#include "src/regexp/regexp-macro-assembler-arch.h"

namespace v8 {
namespace internal {

class Isolate;

namespace regexp_compiler_constants {

// The '2' variant is has inclusive from and exclusive to.
// This covers \s as defined in ECMA-262 5.1, 15.10.2.12,
// which include WhiteSpace (7.2) or LineTerminator (7.3) values.
constexpr uc32 kRangeEndMarker = 0x110000;
constexpr int kSpaceRanges[] = {
    '\t',   '\r' + 1, ' ',    ' ' + 1, 0x00A0, 0x00A1, 0x1680,
    0x1681, 0x2000,   0x200B, 0x2028,  0x202A, 0x202F, 0x2030,
    0x205F, 0x2060,   0x3000, 0x3001,  0xFEFF, 0xFF00, kRangeEndMarker};
constexpr int kSpaceRangeCount = arraysize(kSpaceRanges);

constexpr int kWordRanges[] = {'0',     '9' + 1, 'A',     'Z' + 1,        '_',
                               '_' + 1, 'a',     'z' + 1, kRangeEndMarker};
constexpr int kWordRangeCount = arraysize(kWordRanges);
constexpr int kDigitRanges[] = {'0', '9' + 1, kRangeEndMarker};
constexpr int kDigitRangeCount = arraysize(kDigitRanges);
constexpr int kSurrogateRanges[] = {kLeadSurrogateStart,
                                    kLeadSurrogateStart + 1, kRangeEndMarker};
constexpr int kSurrogateRangeCount = arraysize(kSurrogateRanges);
constexpr int kLineTerminatorRanges[] = {0x000A, 0x000B, 0x000D,         0x000E,
                                         0x2028, 0x202A, kRangeEndMarker};
constexpr int kLineTerminatorRangeCount = arraysize(kLineTerminatorRanges);

}  // namespace regexp_compiler_constants

class FrequencyCollator {
 public:
  FrequencyCollator() : total_samples_(0) {
    for (int i = 0; i < RegExpMacroAssembler::kTableSize; i++) {
      frequencies_[i] = CharacterFrequency(i);
    }
  }

  void CountCharacter(int character) {
    int index = (character & RegExpMacroAssembler::kTableMask);
    frequencies_[index].Increment();
    total_samples_++;
  }

  // Does not measure in percent, but rather per-128 (the table size from the
  // regexp macro assembler).
  int Frequency(int in_character) {
    DCHECK((in_character & RegExpMacroAssembler::kTableMask) == in_character);
    if (total_samples_ < 1) return 1;  // Division by zero.
    int freq_in_per128 =
        (frequencies_[in_character].counter() * 128) / total_samples_;
    return freq_in_per128;
  }

 private:
  class CharacterFrequency {
   public:
    CharacterFrequency() : counter_(0), character_(-1) {}
    explicit CharacterFrequency(int character)
        : counter_(0), character_(character) {}

    void Increment() { counter_++; }
    int counter() { return counter_; }
    int character() { return character_; }

   private:
    int counter_;
    int character_;
  };

 private:
  CharacterFrequency frequencies_[RegExpMacroAssembler::kTableSize];
  int total_samples_;
};

class RegExpCompiler {
 public:
  RegExpCompiler(Isolate* isolate, Zone* zone, int capture_count,
                 bool is_one_byte);

  int AllocateRegister() {
    if (next_register_ >= RegExpMacroAssembler::kMaxRegister) {
      reg_exp_too_big_ = true;
      return next_register_;
    }
    return next_register_++;
  }

  // Lookarounds to match lone surrogates for unicode character class matches
  // are never nested. We can therefore reuse registers.
  int UnicodeLookaroundStackRegister() {
    if (unicode_lookaround_stack_register_ == kNoRegister) {
      unicode_lookaround_stack_register_ = AllocateRegister();
    }
    return unicode_lookaround_stack_register_;
  }

  int UnicodeLookaroundPositionRegister() {
    if (unicode_lookaround_position_register_ == kNoRegister) {
      unicode_lookaround_position_register_ = AllocateRegister();
    }
    return unicode_lookaround_position_register_;
  }

  RegExpEngine::CompilationResult Assemble(Isolate* isolate,
                                           RegExpMacroAssembler* assembler,
                                           RegExpNode* start, int capture_count,
                                           Handle<String> pattern);

  inline void AddWork(RegExpNode* node) {
    if (!node->on_work_list() && !node->label()->is_bound()) {
      node->set_on_work_list(true);
      work_list_->push_back(node);
    }
  }

  static const int kImplementationOffset = 0;
  static const int kNumberOfRegistersOffset = 0;
  static const int kCodeOffset = 1;

  RegExpMacroAssembler* macro_assembler() { return macro_assembler_; }
  EndNode* accept() { return accept_; }

  static const int kMaxRecursion = 100;
  inline int recursion_depth() { return recursion_depth_; }
  inline void IncrementRecursionDepth() { recursion_depth_++; }
  inline void DecrementRecursionDepth() { recursion_depth_--; }

  void SetRegExpTooBig() { reg_exp_too_big_ = true; }

  inline bool one_byte() { return one_byte_; }
  inline bool optimize() { return optimize_; }
  inline void set_optimize(bool value) { optimize_ = value; }
  inline bool limiting_recursion() { return limiting_recursion_; }
  inline void set_limiting_recursion(bool value) {
    limiting_recursion_ = value;
  }
  bool read_backward() { return read_backward_; }
  void set_read_backward(bool value) { read_backward_ = value; }
  FrequencyCollator* frequency_collator() { return &frequency_collator_; }

  int current_expansion_factor() { return current_expansion_factor_; }
  void set_current_expansion_factor(int value) {
    current_expansion_factor_ = value;
  }

  Isolate* isolate() const { return isolate_; }
  Zone* zone() const { return zone_; }

  static const int kNoRegister = -1;

 private:
  EndNode* accept_;
  int next_register_;
  int unicode_lookaround_stack_register_;
  int unicode_lookaround_position_register_;
  std::vector<RegExpNode*>* work_list_;
  int recursion_depth_;
  RegExpMacroAssembler* macro_assembler_;
  bool one_byte_;
  bool reg_exp_too_big_;
  bool limiting_recursion_;
  bool optimize_;
  bool read_backward_;
  int current_expansion_factor_;
  FrequencyCollator frequency_collator_;
  Isolate* isolate_;
  Zone* zone_;
};

// Categorizes character ranges into BMP, non-BMP, lead, and trail surrogates.
class UnicodeRangeSplitter {
 public:
  V8_EXPORT_PRIVATE UnicodeRangeSplitter(Zone* zone,
                                         ZoneList<CharacterRange>* base);
  void Call(uc32 from, DispatchTable::Entry entry);

  ZoneList<CharacterRange>* bmp() { return bmp_; }
  ZoneList<CharacterRange>* lead_surrogates() { return lead_surrogates_; }
  ZoneList<CharacterRange>* trail_surrogates() { return trail_surrogates_; }
  ZoneList<CharacterRange>* non_bmp() const { return non_bmp_; }

 private:
  static const int kBase = 0;
  // Separate ranges into
  static const int kBmpCodePoints = 1;
  static const int kLeadSurrogates = 2;
  static const int kTrailSurrogates = 3;
  static const int kNonBmpCodePoints = 4;

  Zone* zone_;
  DispatchTable table_;
  ZoneList<CharacterRange>* bmp_;
  ZoneList<CharacterRange>* lead_surrogates_;
  ZoneList<CharacterRange>* trail_surrogates_;
  ZoneList<CharacterRange>* non_bmp_;
};

// We need to check for the following characters: 0x39C 0x3BC 0x178.
// TODO(jgruber): Move to CharacterRange.
bool RangeContainsLatin1Equivalents(CharacterRange range);

}  // namespace internal
}  // namespace v8

#endif  // V8_REGEXP_REGEXP_COMPILER_H_
