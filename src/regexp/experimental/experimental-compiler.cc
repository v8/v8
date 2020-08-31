// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/experimental/experimental-compiler.h"

#include "src/zone/zone-list-inl.h"

namespace v8 {
namespace internal {

namespace {

// TODO(mbid, v8:10765): Currently the experimental engine doesn't support
// UTF-16, but this shouldn't be too hard to implement.
constexpr uc32 kMaxSupportedCodepoint = 0xFFFFu;

class CanBeHandledVisitor final : private RegExpVisitor {
  // Visitor to implement `ExperimentalRegExp::CanBeHandled`.
 public:
  static bool Check(RegExpTree* node, JSRegExp::Flags flags, Zone* zone) {
    if (!AreSuitableFlags(flags)) {
      return false;
    }
    CanBeHandledVisitor visitor(zone);
    node->Accept(&visitor, nullptr);
    return visitor.result_;
  }

 private:
  explicit CanBeHandledVisitor(Zone* zone) : zone_(zone) {}

  static bool AreSuitableFlags(JSRegExp::Flags flags) {
    // TODO(mbid, v8:10765): We should be able to support all flags in the
    // future.
    static constexpr JSRegExp::Flags allowed_flags = JSRegExp::kGlobal;
    return (flags & ~allowed_flags) == 0;
  }

  void* VisitDisjunction(RegExpDisjunction* node, void*) override {
    for (RegExpTree* alt : *node->alternatives()) {
      alt->Accept(this, nullptr);
      if (!result_) {
        return nullptr;
      }
    }
    return nullptr;
  }

  void* VisitAlternative(RegExpAlternative* node, void*) override {
    for (RegExpTree* child : *node->nodes()) {
      child->Accept(this, nullptr);
      if (!result_) {
        return nullptr;
      }
    }
    return nullptr;
  }

  void* VisitCharacterClass(RegExpCharacterClass* node, void*) override {
    result_ = result_ && AreSuitableFlags(node->flags());
    for (CharacterRange r : *node->ranges(zone_)) {
      // TODO(mbid, v8:10765): We don't support full unicode yet, so we only
      // allow character ranges that can be specified with two-byte characters.
      if (r.to() > kMaxSupportedCodepoint) {
        result_ = false;
        return nullptr;
      }
    }
    return nullptr;
  }

  void* VisitAssertion(RegExpAssertion* node, void*) override {
    // TODO(mbid, v8:10765): We should be able to support at least some
    // assertions. re2 does, too.
    result_ = false;
    return nullptr;
  }

  void* VisitAtom(RegExpAtom* node, void*) override {
    result_ = result_ && AreSuitableFlags(node->flags());
    return nullptr;
  }

  void* VisitText(RegExpText* node, void*) override {
    for (TextElement& el : *node->elements()) {
      el.tree()->Accept(this, nullptr);
      if (!result_) {
        return nullptr;
      }
    }
    return nullptr;
  }

  void* VisitQuantifier(RegExpQuantifier* node, void*) override {
    // TODO(mbid, v8:10765): Theoretically we can support arbitrary min() and
    // max(), but the size of the automaton grows linearly with finite max().
    // We probably want a cut-off value here, or maybe we can "virtualize" the
    // repetitions.
    // Non-greedy quantifiers are easy to implement, but not supported atm.
    // It's not clear to me how a possessive quantifier would be implemented,
    // we should check whether re2 supports this.
    result_ = result_ && node->min() == 0 &&
              node->max() == RegExpTree::kInfinity && node->is_greedy();
    if (!result_) {
      return nullptr;
    }
    node->body()->Accept(this, nullptr);
    return nullptr;
  }

  void* VisitCapture(RegExpCapture* node, void*) override {
    // TODO(mbid, v8:10765): This can be implemented with the NFA interpreter,
    // but not with the lazy DFA.  See also re2.
    result_ = false;
    return nullptr;
  }

  void* VisitGroup(RegExpGroup* node, void*) override {
    node->body()->Accept(this, nullptr);
    return nullptr;
  }

  void* VisitLookaround(RegExpLookaround* node, void*) override {
    // TODO(mbid, v8:10765): This will be hard to support, but not impossible I
    // think.  See product automata.
    result_ = false;
    return nullptr;
  }

  void* VisitBackReference(RegExpBackReference* node, void*) override {
    // This can't be implemented without backtracking.
    result_ = false;
    return nullptr;
  }

  void* VisitEmpty(RegExpEmpty* node, void*) override { return nullptr; }

 private:
  bool result_ = true;
  Zone* zone_;
};

}  // namespace

bool ExperimentalRegExpCompiler::CanBeHandled(RegExpTree* tree,
                                              JSRegExp::Flags flags,
                                              Zone* zone) {
  DCHECK(FLAG_enable_experimental_regexp_engine);
  return CanBeHandledVisitor::Check(tree, flags, zone);
}

namespace {

class CompileVisitor : private RegExpVisitor {
 public:
  static ZoneList<RegExpInstruction> Compile(RegExpTree* tree,
                                             JSRegExp::Flags flags,
                                             Zone* zone) {
    CompileVisitor compiler(zone);

    tree->Accept(&compiler, nullptr);
    compiler.code_.Add(RegExpInstruction::Accept(), zone);

    return std::move(compiler.code_);
  }

 private:
  // TODO(mbid,v8:10765): Use some upper bound for code_ capacity computed from
  // the `tree` size we're going to compile?
  explicit CompileVisitor(Zone* zone) : zone_(zone), code_(0, zone) {}

  // Generate a disjunction of code fragments compiled by a function `alt_gen`.
  // `alt_gen` is called repeatedly with argument `int i = 0, 1, ..., alt_num -
  // 1` and should push code corresponding to the ith alternative onto `code_`.
  template <class F>
  void CompileDisjunction(int alt_num, F gen_alt) {
    // An alternative a0 | a1 | a2 is compiled into
    //   FORK <a2>
    //   FORK <a1>
    //   <a0>
    //   JMP $end
    //   <a1>
    //   JMP $end
    //   <a2>
    // where $end is the index of the next instruction after <a2>.
    //
    // By the semantics of the FORK instruction (see above at definition and
    // semantics), the forked thread has lower priority than the current
    // thread.  This means that with the code we're generating here, the thread
    // matching the alternative a0 is indeed the thread with the highest
    // priority, followed by the thread for a1 and so on.

    if (alt_num == 0) {
      return;
    }

    // Record the index of the first of the alt_num - 1 fork instructions in the
    // beginning.
    int forks_begin = code_.length();
    // Add FORKs to alts[alt_num - 1], alts[alt_num - 2], ..., alts[1].
    for (int i = alt_num - 1; i != 0; --i) {
      // The FORK's address is patched once we know the address of the ith
      // alternative.
      code_.Add(RegExpInstruction::Fork(-1), zone_);
    }

    // List containing the index of the final JMP instruction after each
    // alternative but the last one.
    ZoneList<int> jmp_indices(alt_num - 1, zone_);

    for (int i = 0; i != alt_num; ++i) {
      if (i != 0) {
        // If this is not the first alternative, we have to patch the
        // corresponding FORK statement in the beginning.
        code_[forks_begin + alt_num - 1 - i].payload.pc = code_.length();
      }
      gen_alt(i);
      if (i != alt_num - 1) {
        // If this is not the last alternative, we have to emit a JMP past the
        // remaining alternatives.  We don't know this address yet, so we have
        // to patch patch it once all alternatives are emitted.
        jmp_indices.Add(code_.length(), zone_);
        code_.Add(RegExpInstruction::Jmp(-1), zone_);
      }
    }

    // All alternatives are emitted.  Now we can patch the JMP instruction
    // after each but the last alternative.
    int end_index = code_.length();
    for (int jmp_index : jmp_indices) {
      code_[jmp_index].payload.pc = end_index;
    }
  }

  void* VisitDisjunction(RegExpDisjunction* node, void*) override {
    ZoneList<RegExpTree*>& alts = *node->alternatives();
    CompileDisjunction(alts.length(),
                       [&](int i) { alts[i]->Accept(this, nullptr); });
    return nullptr;
  }

  void* VisitAlternative(RegExpAlternative* node, void*) override {
    for (RegExpTree* child : *node->nodes()) {
      child->Accept(this, nullptr);
    }
    return nullptr;
  }

  void* VisitAssertion(RegExpAssertion* node, void*) override {
    // TODO(mbid,v8:10765): Support this case.
    UNREACHABLE();
  }

  void* VisitCharacterClass(RegExpCharacterClass* node, void*) override {
    // A character class is compiled as Disjunction over its `CharacterRange`s.
    ZoneList<CharacterRange>* ranges = node->ranges(zone_);
    CharacterRange::Canonicalize(ranges);
    if (node->is_negated()) {
      // Capacity 2 for the common case where we compute the complement of a
      // single interval range that doesn't contain 0 and kMaxCodePoint.
      ZoneList<CharacterRange>* negated =
          zone_->New<ZoneList<CharacterRange>>(2, zone_);
      CharacterRange::Negate(ranges, negated, zone_);
      ranges = negated;
    }

    CompileDisjunction(ranges->length(), [&](int i) {
      // We don't support utf16 for now, so only ranges that can be specified
      // by (complements of) ranges with uc16 bounds.
      STATIC_ASSERT(kMaxSupportedCodepoint <= std::numeric_limits<uc16>::max());

      uc32 from = (*ranges)[i].from();
      DCHECK_LE(from, kMaxSupportedCodepoint);
      uc16 from_uc16 = static_cast<uc16>(from);

      uc32 to = (*ranges)[i].to();
      DCHECK_IMPLIES(to > kMaxSupportedCodepoint, to == String::kMaxCodePoint);
      uc16 to_uc16 = static_cast<uc16>(std::min(to, kMaxSupportedCodepoint));

      RegExpInstruction::Uc16Range range{from_uc16, to_uc16};
      code_.Add(RegExpInstruction::ConsumeRange(range), zone_);
    });
    return nullptr;
  }

  void* VisitAtom(RegExpAtom* node, void*) override {
    for (uc16 c : node->data()) {
      code_.Add(
          RegExpInstruction::ConsumeRange(RegExpInstruction::Uc16Range{c, c}),
          zone_);
    }
    return nullptr;
  }

  void* VisitQuantifier(RegExpQuantifier* node, void*) override {
    // TODO(mbid,v8:10765): For now we support a quantifier of the form /x*/,
    // i.e. greedy match of any number of /x/.  See also the comment in
    // `CanBeHandledVisitor::VisitQuantifier`.
    DCHECK_EQ(node->min(), 0);
    DCHECK_EQ(node->max(), RegExpTree::kInfinity);
    DCHECK(node->is_greedy());

    // The repetition of /x/ is compiled into
    //
    //   a: FORK d
    //   b: <x>
    //   c: JMP a
    //   d: ...
    //
    // Note that a FORKed thread has lower priority than the main thread, so
    // this will indeed match greedily.

    int initial_fork_index = code_.length();
    // The FORK's address is patched once we're done.
    code_.Add(RegExpInstruction::Fork(-1), zone_);
    node->body()->Accept(this, nullptr);
    code_.Add(RegExpInstruction::Jmp(initial_fork_index), zone_);
    int end_index = code_.length();
    code_[initial_fork_index].payload.pc = end_index;
    return nullptr;
  }

  void* VisitCapture(RegExpCapture* node, void*) override {
    // TODO(mbid,v8:10765): Support this case.
    UNREACHABLE();
  }

  void* VisitGroup(RegExpGroup* node, void*) override {
    node->body()->Accept(this, nullptr);
    return nullptr;
  }

  void* VisitLookaround(RegExpLookaround* node, void*) override {
    // TODO(mbid,v8:10765): Support this case.
    UNREACHABLE();
  }

  void* VisitBackReference(RegExpBackReference* node, void*) override {
    UNREACHABLE();
  }

  void* VisitEmpty(RegExpEmpty* node, void*) override { return nullptr; }

  void* VisitText(RegExpText* node, void*) override {
    for (TextElement& text_el : *node->elements()) {
      text_el.tree()->Accept(this, nullptr);
    }
    return nullptr;
  }

 private:
  Zone* zone_;
  ZoneList<RegExpInstruction> code_;
};

}  // namespace

ZoneList<RegExpInstruction> ExperimentalRegExpCompiler::Compile(
    RegExpTree* tree, JSRegExp::Flags flags, Zone* zone) {
  return CompileVisitor::Compile(tree, flags, zone);
}

}  // namespace internal
}  // namespace v8
