// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/regexp/experimental/experimental.h"

#include <iomanip>
#include <ios>

#include "src/base/optional.h"
#include "src/base/small-vector.h"
#include "src/objects/js-regexp-inl.h"
#include "src/regexp/regexp-ast.h"
#include "src/regexp/regexp-parser.h"
#include "src/utils/ostreams.h"

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

bool ExperimentalRegExp::CanBeHandled(RegExpTree* tree, JSRegExp::Flags flags,
                                      Zone* zone) {
  DCHECK(FLAG_enable_experimental_regexp_engine);
  return CanBeHandledVisitor::Check(tree, flags, zone);
}

void ExperimentalRegExp::Initialize(Isolate* isolate, Handle<JSRegExp> re,
                                    Handle<String> source,
                                    JSRegExp::Flags flags, int capture_count) {
  DCHECK(FLAG_enable_experimental_regexp_engine);
  if (FLAG_trace_experimental_regexp_engine) {
    StdoutStream{} << "Initializing experimental regexp " << *source
                   << std::endl;
  }

  isolate->factory()->SetRegExpExperimentalData(re, source, flags,
                                                capture_count);
}

bool ExperimentalRegExp::IsCompiled(Handle<JSRegExp> re, Isolate* isolate) {
  DCHECK(FLAG_enable_experimental_regexp_engine);

  DCHECK_EQ(re->TypeTag(), JSRegExp::EXPERIMENTAL);
#ifdef VERIFY_HEAP
  re->JSRegExpVerify(isolate);
#endif

  return re->DataAt(JSRegExp::kIrregexpLatin1BytecodeIndex) !=
         Smi::FromInt(JSRegExp::kUninitializedValue);
}

// ----------------------------------------------------------------------------
// Definition and semantics of the EXPERIMENTAL bytecode.
// Background:
// - Russ Cox's blog post series on regular expression matching, in particular
//   https://swtch.com/~rsc/regexp/regexp2.html
// - The re2 regular regexp library: https://github.com/google/re2
//
// This comment describes the bytecode used by the experimental regexp engine
// and its abstract semantics in terms of a VM.  An implementation of the
// semantics that avoids exponential runtime can be found in `NfaInterpreter`.
//
// The experimental bytecode describes a non-deterministic finite automaton. It
// runs on a multithreaded virtual machine (VM), i.e. in several threads
// concurrently.  (These "threads" don't need to be actual operating system
// threads.)  Apart from a list of threads, the VM maintains an immutable
// shared input string which threads can read from.  Each thread is given by a
// program counter (PC, index of the current instruction), a fixed number of
// registers of indices into the input string, and a monotonically increasing
// index which represents the current position within the input string.
//
// For the precise encoding of the instruction set, see the definition `struct
// RegExpInstruction` below.  Currently we support the following instructions:
// - CONSUME_RANGE: Check whether the codepoint of the current character is
//   contained in a non-empty closed interval [min, max] specified in the
//   instruction payload.  Abort this thread if false, otherwise advance the
//   input position by 1 and continue with the next instruction.
// - ACCEPT: Stop this thread and signify the end of a match at the current
//   input position.
// - FORK: If executed by a thread t, spawn a new thread t0 whose register
//   values and input position agree with those of t, but whose PC value is set
//   to the value specified in the instruction payload.  The register values of
//   t and t0 agree directly after the FORK, but they can diverge.  Thread t
//   continues with the instruction directly after the current FORK
//   instruction.
// - JMP: Instead of incrementing the PC value after execution of this
//   instruction by 1, set PC of this thread to the value specified in the
//   instruction payload and continue there.
//
// Special care must be exercised with respect to thread priority.  It is
// possible that more than one thread executes an ACCEPT statement.  The output
// of the program is given by the contents of the matching thread's registers,
// so this is ambiguous in case of multiple matches.  To resolve the ambiguity,
// every implementation of the VM  must output the match that a backtracking
// implementation would output (i.e. behave the same as Irregexp).
//
// A backtracking implementation of the VM maintains a stack of postponed
// threads.  Upon encountering a FORK statement, this VM will create a copy of
// the current thread, set the copy's PC value according to the instruction
// payload, and push it to the stack of postponed threads.  The VM will then
// continue execution of the current thread.
//
// If at some point a thread t executes a MATCH statement, the VM stops and
// outputs the registers of t.  Postponed threads are discarded.  On the other
// hand, if a thread t is aborted because some input character didn't pass a
// check, then the VM pops the topmost postponed thread and continues execution
// with this thread.  If there are no postponed threads, then the VM outputs
// failure, i.e. no matches.
//
// Equivalently, we can describe the behavior of the backtracking VM in terms
// of priority: Threads are linearly ordered by priority, and matches generated
// by threads with high priority must be preferred over matches generated by
// threads with low priority, regardless of the chronological order in which
// matches were found.  If a thread t executes a FORK statement and spawns a
// thread t0, then the priority of t0 is such that the following holds:
// * t0 < t, i.e. t0 has lower priority than t.
// * For all threads u such that u != t and u != t0, we have t0 < u iff t < u,
//   i.e. the t0 compares to other threads the same as t.
// For example, if there are currently 3 threads s, t, u such that s < t < u,
// then after t executes a fork, the thread priorities will be s < t0 < t < u.

namespace {

struct Uc16Range {
  uc16 min;  // Inclusive.
  uc16 max;  // Inclusive.
};

// Bytecode format.
// Currently very simple fixed-size: The opcode is encoded in the first 4
// bytes, the payload takes another 4 bytes.
struct RegExpInstruction {
  enum Opcode : int32_t {
    CONSUME_RANGE,
    FORK,
    JMP,
    ACCEPT,
  };

  static RegExpInstruction ConsumeRange(Uc16Range consume_range) {
    RegExpInstruction result;
    result.opcode = CONSUME_RANGE;
    result.payload.consume_range = consume_range;
    return result;
  }

  static RegExpInstruction Fork(int32_t alt_index) {
    RegExpInstruction result;
    result.opcode = FORK;
    result.payload.pc = alt_index;
    return result;
  }

  static RegExpInstruction Jmp(int32_t alt_index) {
    RegExpInstruction result;
    result.opcode = JMP;
    result.payload.pc = alt_index;
    return result;
  }

  static RegExpInstruction Accept() {
    RegExpInstruction result;
    result.opcode = ACCEPT;
    return result;
  }

  Opcode opcode;
  union {
    // Payload of CONSUME_RANGE:
    Uc16Range consume_range;
    // Payload of FORK and JMP, the next/forked program counter (pc):
    int32_t pc;
  } payload;
  STATIC_ASSERT(sizeof(payload) == 4);
};
STATIC_ASSERT(sizeof(RegExpInstruction) == 8);
// TODO(mbid,v8:10765): This is rather wasteful.  We can fit the opcode in 2-3
// bits, so the remaining 29/30 bits can be used as payload.  Problem: The
// payload of CONSUME_RANGE consists of two 16-bit values `min` and `max`, so
// this wouldn't fit.  We could encode the payload of a CONSUME_RANGE
// instruction by the start of the interval and its length instead, and then
// only allows lengths that fit into 14/13 bits.  A longer range can then be
// encoded as a disjunction of smaller ranges.
//
// Another thought: CONSUME_RANGEs are only valid if the payloads are such that
// min <= max. Thus there are
//
//     2^16 + 2^16 - 1 + ... + 1
//   = 2^16 * (2^16 + 1) / 2
//   = 2^31 + 2^15
//
// valid payloads for a CONSUME_RANGE instruction.  If we want to fit
// instructions into 4 bytes, we would still have almost 2^31 instructions left
// over if we encode everything as tight as possible.  For example, we could
// use another 2^29 values for JMP, another 2^29 for FORK, 1 value for ACCEPT,
// and then still have almost 2^30 instructions left over for something like
// zero-width assertions and captures.

std::ostream& PrintAsciiOrHex(std::ostream& os, uc16 c) {
  if (c < 128 && std::isprint(c)) {
    os << static_cast<char>(c);
  } else {
    os << "0x" << std::hex << static_cast<int>(c);
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const RegExpInstruction& inst) {
  switch (inst.opcode) {
    case RegExpInstruction::CONSUME_RANGE: {
      os << "CONSUME_RANGE [";
      PrintAsciiOrHex(os, inst.payload.consume_range.min);
      os << ", ";
      PrintAsciiOrHex(os, inst.payload.consume_range.max);
      os << "]";
      break;
    }
    case RegExpInstruction::FORK:
      os << "FORK " << inst.payload.pc;
      break;
    case RegExpInstruction::JMP:
      os << "JMP " << inst.payload.pc;
      break;
    case RegExpInstruction::ACCEPT:
      os << "ACCEPT";
      break;
  }
  return os;
}

// The maximum number of digits required to display a non-negative number < n
// in base 10.
int DigitsRequiredBelow(int n) {
  DCHECK_GE(n, 0);

  int result = 1;
  for (int i = 10; i < n; i *= 10) {
    result += 1;
  }
  return result;
}

std::ostream& operator<<(std::ostream& os,
                         Vector<const RegExpInstruction> insts) {
  int inst_num = insts.length();
  int line_digit_num = DigitsRequiredBelow(inst_num);

  for (int i = 0; i != inst_num; ++i) {
    const RegExpInstruction& inst = insts[i];
    os << std::setfill('0') << std::setw(line_digit_num) << i << ": " << inst
       << std::endl;
  }
  return os;
}

Vector<RegExpInstruction> AsInstructionSequence(ByteArray raw_bytes) {
  RegExpInstruction* inst_begin =
      reinterpret_cast<RegExpInstruction*>(raw_bytes.GetDataStartAddress());
  int inst_num = raw_bytes.length() / sizeof(RegExpInstruction);
  DCHECK_EQ(sizeof(RegExpInstruction) * inst_num, raw_bytes.length());
  return Vector<RegExpInstruction>(inst_begin, inst_num);
}

class Compiler : private RegExpVisitor {
 public:
  static Handle<ByteArray> Compile(RegExpTree* tree, Isolate* isolate,
                                   Zone* zone) {
    Compiler compiler(zone);

    tree->Accept(&compiler, nullptr);
    compiler.code_.Add(RegExpInstruction::Accept(), zone);

    int byte_length = sizeof(RegExpInstruction) * compiler.code_.length();
    Handle<ByteArray> array = isolate->factory()->NewByteArray(byte_length);
    MemCopy(array->GetDataStartAddress(), compiler.code_.begin(), byte_length);

    return array;
  }

 private:
  // TODO(mbid,v8:10765): Use some upper bound for code_ capacity computed from
  // the `tree` size we're going to compile?
  explicit Compiler(Zone* zone) : zone_(zone), code_(0, zone) {}

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

      Uc16Range range{from_uc16, to_uc16};
      code_.Add(RegExpInstruction::ConsumeRange(range), zone_);
    });
    return nullptr;
  }

  void* VisitAtom(RegExpAtom* node, void*) override {
    for (uc16 c : node->data()) {
      code_.Add(RegExpInstruction::ConsumeRange(Uc16Range{c, c}), zone_);
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

void ExperimentalRegExp::Compile(Isolate* isolate, Handle<JSRegExp> re) {
  DCHECK_EQ(re->TypeTag(), JSRegExp::EXPERIMENTAL);
#ifdef VERIFY_HEAP
  re->JSRegExpVerify(isolate);
#endif

  Handle<String> source(re->Pattern(), isolate);
  if (FLAG_trace_experimental_regexp_engine) {
    StdoutStream{} << "Compiling experimental regexp " << *source << std::endl;
  }

  Zone zone(isolate->allocator(), ZONE_NAME);

  // Parse and compile the regexp source.
  RegExpCompileData parse_result;
  JSRegExp::Flags flags = re->GetFlags();
  FlatStringReader reader(isolate, source);
  DCHECK(!isolate->has_pending_exception());

  // The pattern was already parsed during initialization, so it should never
  // fail here:
  bool parse_success =
      RegExpParser::ParseRegExp(isolate, &zone, &reader, flags, &parse_result);
  CHECK(parse_success);

  Handle<ByteArray> bytecode =
      Compiler::Compile(parse_result.tree, isolate, &zone);
  re->SetDataAt(JSRegExp::kIrregexpLatin1BytecodeIndex, *bytecode);
  re->SetDataAt(JSRegExp::kIrregexpUC16BytecodeIndex, *bytecode);

  Handle<Code> trampoline = BUILTIN_CODE(isolate, RegExpExperimentalTrampoline);
  re->SetDataAt(JSRegExp::kIrregexpLatin1CodeIndex, *trampoline);
  re->SetDataAt(JSRegExp::kIrregexpUC16CodeIndex, *trampoline);
}

namespace {

// A half-open range in the input string denoting a (sub)match.  Used to access
// output registers of a regexp match grouped by [begin, end) pairs.
struct MatchRange {
  int32_t begin;  // inclusive
  int32_t end;    // exclusive
};

template <class Character>
class NfaInterpreter {
  // Executes a bytecode program in breadth-first mode, without backtracking.
  // `Character` can be instantiated with `uint8_t` or `uc16` for one byte or
  // two byte input strings.
  //
  // In contrast to the backtracking implementation, this has linear time
  // complexity in the length of the input string. Breadth-first mode means
  // that threads are executed in lockstep with respect to their input
  // position, i.e. the threads share a common input index.  This is similar
  // to breadth-first simulation of a non-deterministic finite automaton (nfa),
  // hence the name of the class.
  //
  // To follow the semantics of a backtracking VM implementation, we have to be
  // careful about whether we stop execution when a thread executes ACCEPT.
  // For example, consider execution of the bytecode generated by the regexp
  //
  //   r = /abc|..|[a-c]{10,}/
  //
  // on input "abcccccccccccccc".  Clearly the three alternatives
  // - /abc/
  // - /../
  // - /[a-c]{10,}/
  // all match this input.  A backtracking implementation will report "abc" as
  // match, because it explores the first alternative before the others.
  //
  // However, if we execute breadth first, then we execute the 3 threads
  // - t1, which tries to match /abc/
  // - t2, which tries to match /../
  // - t3, which tries to match /[a-c]{10,}/
  // in lockstep i.e. by iterating over the input and feeding all threads one
  // character at a time.  t2 will execute an ACCEPT after two characters,
  // while t1 will only execute ACCEPT after three characters. Thus we find a
  // match for the second alternative before a match of the first alternative.
  //
  // This shows that we cannot always stop searching as soon as some thread t
  // executes ACCEPT:  If there is a thread u with higher priority than t, then
  // it must be finished first.  If u produces a match, then we can discard the
  // match of t because matches produced by threads with higher priority are
  // preferred over matches of threads with lower priority.  On the other hand,
  // we are allowed to abort all threads with lower priority than t if t
  // produces a match: Such threads can only produce worse matches.  In the
  // example above, we can abort t3 after two characters because of t2's match.
  //
  // Thus the interpreter keeps track of a priority-ordered list of threads.
  // If a thread ACCEPTs, all threads with lower priority are discarded, and
  // the search continues with the threads with higher priority.  If no threads
  // with high priority are left, we return the match that was produced by the
  // ACCEPTing thread with highest priority.
 public:
  NfaInterpreter(Vector<const RegExpInstruction> bytecode,
                 Vector<const Character> input, int32_t input_index)
      : bytecode_(bytecode),
        input_(input),
        input_index_(input_index),
        pc_last_input_index_(bytecode.size()),
        active_threads_(),
        blocked_threads_(),
        best_match_(base::nullopt) {
    DCHECK(!bytecode_.empty());
    DCHECK_GE(input_index_, 0);
    DCHECK_LE(input_index_, input_.length());

    std::fill(pc_last_input_index_.begin(), pc_last_input_index_.end(), -1);
  }

  // Finds up to `max_match_num` matches and writes their boundaries to
  // `matches_out`.  The search begins at the current input index.  Returns the
  // number of matches found.
  int FindMatches(MatchRange* matches_out, int max_match_num) {
    int match_num;
    for (match_num = 0; match_num != max_match_num; ++match_num) {
      base::Optional<MatchRange> match = FindNextMatch();
      if (!match.has_value()) {
        break;
      }

      matches_out[match_num] = *match;
      SetInputIndex(match->end);
    }
    return match_num;
  }

 private:
  // The state of a "thread" executing experimental regexp bytecode.  (Not to
  // be confused with an OS thread.)
  struct InterpreterThread {
    // This thread's program counter, i.e. the index within `bytecode_` of the
    // next instruction to be executed.
    int32_t pc;
    // The index in the input string where this thread started executing.
    int32_t match_begin;
  };

  // Change the current input index for future calls to `FindNextMatch`.
  void SetInputIndex(int new_input_index) {
    DCHECK_GE(input_index_, 0);
    DCHECK_LE(input_index_, input_.length());

    input_index_ = new_input_index;
  }

  // Find the next match, begin search at input_index_;
  base::Optional<MatchRange> FindNextMatch() {
    DCHECK(active_threads_.empty());
    // TODO(mbid,v8:10765): Can we get around resetting `pc_last_input_index_`
    // here? As long as
    //
    //   pc_last_input_index_[pc] < input_index_
    //
    // for all possible program counters pc that are reachable without input
    // from pc = 0 and
    //
    //   pc_last_input_index_[k] <= input_index_
    //
    // for all k > 0 hold I think everything should be fine.  Maybe we can do
    // something about this in `SetInputIndex`.
    std::fill(pc_last_input_index_.begin(), pc_last_input_index_.end(), -1);

    DCHECK(blocked_threads_.empty());
    DCHECK(active_threads_.empty());
    DCHECK_EQ(best_match_, base::nullopt);

    // All threads start at bytecode 0.
    PushActiveThreadUnchecked(InterpreterThread{0, input_index_});
    // Run the initial thread, potentially forking new threads, until every
    // thread is blocked without further input.
    RunActiveThreads();

    // We stop if one of the following conditions hold:
    // - We have exhausted the entire input.
    // - We have found a match at some point, and there are no remaining
    //   threads with higher priority than the thread that produced the match.
    //   Threads with low priority have been aborted earlier, and the remaining
    //   threads are blocked here, so the latter simply means that
    //   `blocked_threads_` is empty.
    while (input_index_ != input_.length() &&
           !(best_match_.has_value() && blocked_threads_.empty())) {
      DCHECK(active_threads_.empty());
      uc16 input_char = input_[input_index_];
      ++input_index_;

      // If we haven't found a match yet, we add a thread with least priority
      // that attempts a match starting after `input_char`.
      if (!best_match_.has_value()) {
        active_threads_.emplace_back(InterpreterThread{0, input_index_});
      }

      // We unblock all blocked_threads_ by feeding them the input char.
      FlushBlockedThreads(input_char);

      // Run all threads until they block or accept.
      RunActiveThreads();
    }

    // Clean up the data structures we used.
    base::Optional<MatchRange> result = best_match_;
    best_match_ = base::nullopt;
    blocked_threads_.clear();
    active_threads_.clear();

    return result;
  }

  // Run an active thread `t` until it executes a CONSUME_RANGE or ACCEPT
  // instruction, or its PC value was already processed.
  // - If processing of `t` can't continue because of CONSUME_RANGE, it is
  //   pushed on `blocked_threads_`.
  // - If `t` executes ACCEPT, set `best_match` according to `t.match_begin` and
  //   the current input index. All remaining `active_threads_` are discarded.
  void RunActiveThread(InterpreterThread t) {
    while (true) {
      RegExpInstruction inst = bytecode_[t.pc];
      switch (inst.opcode) {
        case RegExpInstruction::CONSUME_RANGE: {
          blocked_threads_.emplace_back(t);
          return;
        }
        case RegExpInstruction::FORK: {
          InterpreterThread fork = t;
          fork.pc = inst.payload.pc;
          ++t.pc;

          // t has higher priority than fork.  If t.pc hasn't been processed,we
          // push fork on the active_thread_ stack and continue directly with
          // t.  Otherwise we continue directly with fork if possible.
          if (!IsPcProcessed(t.pc)) {
            MarkPcProcessed(t.pc);
            PushActiveThread(fork);
            break;
          } else if (!IsPcProcessed(fork.pc)) {
            t = fork;
            MarkPcProcessed(t.pc);
            break;
          }
          return;
        }
        case RegExpInstruction::JMP:
          t.pc = inst.payload.pc;
          if (IsPcProcessed(t.pc)) return;
          MarkPcProcessed(t.pc);
          break;
        case RegExpInstruction::ACCEPT:
          best_match_ = MatchRange{t.match_begin, input_index_};
          active_threads_.clear();
          return;
      }
    }
  }

  // Run each active thread until it can't continue without further input.
  // `active_threads_` is empty afterwards.  `blocked_threads_` are sorted from
  // low to high priority.
  void RunActiveThreads() {
    while (!active_threads_.empty()) {
      InterpreterThread t = active_threads_.back();
      active_threads_.pop_back();
      RunActiveThread(t);
    }
  }

  // Unblock all blocked_threads_ by feeding them an `input_char`.  Should only
  // be called with `input_index_` pointing to the character *after*
  // `input_char` so that `pc_last_input_index_` is updated correctly.
  void FlushBlockedThreads(uc16 input_char) {
    // The threads in blocked_threads_ are sorted from high to low priority,
    // but active_threads_ needs to be sorted from low to high priority, so we
    // need to activate blocked threads in reverse order.
    //
    // TODO(mbid,v8:10765): base::SmallVector doesn't support `rbegin()` and
    // `rend()`, should we implement that instead of this awkward iteration?
    // Maybe we could at least use an int i and check for i >= 0, but
    // SmallVectors don't have length() methods.
    for (size_t i = blocked_threads_.size(); i > 0; --i) {
      InterpreterThread t = blocked_threads_[i - 1];
      RegExpInstruction inst = bytecode_[t.pc];
      DCHECK_EQ(inst.opcode, RegExpInstruction::CONSUME_RANGE);
      Uc16Range range = inst.payload.consume_range;
      if (input_char >= range.min && input_char <= range.max) {
        ++t.pc;
        PushActiveThreadUnchecked(t);
      }
    }
    blocked_threads_.clear();
  }

  // It is redundant to have two threads t, t0 execute at the same PC value,
  // because one of t, t0 matches iff the other does.  We can thus discard
  // the one with lower priority.  We check whether a thread executed at some
  // PC value by recording for every possible value of PC what the value of
  // input_index_ was the last time a thread executed at PC. If a thread
  // tries to continue execution at a PC value that we have seen before at
  // the current input index, we abort it. (We execute threads with higher
  // priority first, so the second thread is guaranteed to have lower
  // priority.)
  //
  // Check whether we've seen an active thread with a given pc value since the
  // last increment of `input_index_`.
  bool IsPcProcessed(int pc) {
    DCHECK_LE(pc_last_input_index_[pc], input_index_);
    return pc_last_input_index_[pc] == input_index_;
  }

  // Mark a pc as having been processed since the last increment of
  // `input_index_`.
  void MarkPcProcessed(int pc) {
    DCHECK_LE(pc_last_input_index_[pc], input_index_);
    pc_last_input_index_[pc] = input_index_;
  }

  // Functions to push a thread `t` onto the list of active threads, but only
  // if `t.pc` was not already the pc of some other thread at the current
  // subject index.
  void PushActiveThreadUnchecked(InterpreterThread t) {
    DCHECK(!IsPcProcessed(t.pc));

    MarkPcProcessed(t.pc);
    active_threads_.emplace_back(t);
  }
  void PushActiveThread(InterpreterThread t) {
    if (IsPcProcessed(t.pc)) {
      return;
    }
    PushActiveThreadUnchecked(t);
  }

  Vector<const RegExpInstruction> bytecode_;
  Vector<const Character> input_;
  int input_index_;

  // TODO(mbid,v8:10765): The following `SmallVector`s have somehwat
  // arbitrarily chosen small capacity sizes; should benchmark to find a good
  // value.

  // pc_last_input_index_[k] records the value of input_index_ the last
  // time a thread t such that t.pc == k was activated, i.e. put on
  // active_threads_.  Thus pc_last_input_index.size() == bytecode.size().  See
  // also `RunActiveThread`.
  base::SmallVector<int, 64> pc_last_input_index_;

  // Active threads can potentially (but not necessarily) continue without
  // input.  Sorted from low to high priority.
  base::SmallVector<InterpreterThread, 64> active_threads_;

  // The pc of a blocked thread points to an instruction that consumes a
  // character. Sorted from high to low priority (so the opposite of
  // `active_threads_`).
  base::SmallVector<InterpreterThread, 64> blocked_threads_;

  // The best match found so far during the current search.  If several threads
  // ACCEPTed, then this will be the match of the accepting thread with highest
  // priority.
  base::Optional<MatchRange> best_match_;
};

}  // namespace

// Returns the number of matches.
int32_t ExperimentalRegExp::ExecRaw(JSRegExp regexp, String subject,
                                    int32_t* output_registers,
                                    int32_t output_register_count,
                                    int32_t subject_index) {
  DisallowHeapAllocation no_gc;

  DCHECK(FLAG_enable_experimental_regexp_engine);

  if (FLAG_trace_experimental_regexp_engine) {
    String source = String::cast(regexp.DataAt(JSRegExp::kSourceIndex));
    StdoutStream{} << "Executing experimental regexp " << source << std::endl;
  }

  Vector<RegExpInstruction> bytecode = AsInstructionSequence(
      ByteArray::cast(regexp.DataAt(JSRegExp::kIrregexpLatin1BytecodeIndex)));

  if (FLAG_print_regexp_bytecode) {
    StdoutStream{} << "Bytecode:" << std::endl;
    StdoutStream{} << bytecode << std::endl;
  }

  DCHECK(subject.IsFlat());
  String::FlatContent subject_content = subject.GetFlatContent(no_gc);

  DCHECK_EQ(output_register_count % 2, 0);
  MatchRange* matches = reinterpret_cast<MatchRange*>(output_registers);
  const int32_t max_match_num = output_register_count / 2;

  if (subject_content.IsOneByte()) {
    NfaInterpreter<uint8_t> interpreter(
        bytecode, subject_content.ToOneByteVector(), subject_index);
    return interpreter.FindMatches(matches, max_match_num);
  } else {
    NfaInterpreter<uc16> interpreter(bytecode, subject_content.ToUC16Vector(),
                                     subject_index);
    return interpreter.FindMatches(matches, max_match_num);
  }
}

int32_t ExperimentalRegExp::MatchForCallFromJs(
    Address subject, int32_t start_position, Address input_start,
    Address input_end, int* output_registers, int32_t output_register_count,
    Address backtrack_stack, RegExp::CallOrigin call_origin, Isolate* isolate,
    Address regexp) {
  DCHECK(FLAG_enable_experimental_regexp_engine);

  DCHECK_NOT_NULL(isolate);
  DCHECK_NOT_NULL(output_registers);
  DCHECK(call_origin == RegExp::CallOrigin::kFromJs);

  DisallowHeapAllocation no_gc;
  DisallowJavascriptExecution no_js(isolate);
  DisallowHandleAllocation no_handles;
  DisallowHandleDereference no_deref;

  String subject_string = String::cast(Object(subject));

  JSRegExp regexp_obj = JSRegExp::cast(Object(regexp));

  return ExecRaw(regexp_obj, subject_string, output_registers,
                 output_register_count, start_position);
}

MaybeHandle<Object> ExperimentalRegExp::Exec(
    Isolate* isolate, Handle<JSRegExp> regexp, Handle<String> subject,
    int subject_index, Handle<RegExpMatchInfo> last_match_info) {
  DCHECK(FLAG_enable_experimental_regexp_engine);

  DCHECK_EQ(regexp->TypeTag(), JSRegExp::EXPERIMENTAL);
#ifdef VERIFY_HEAP
  regexp->JSRegExpVerify(isolate);
#endif

  if (!IsCompiled(regexp, isolate)) {
    Compile(isolate, regexp);
  }

  DCHECK(IsCompiled(regexp, isolate));

  subject = String::Flatten(isolate, subject);

  MatchRange match;

  int32_t* output_registers = &match.begin;
  int32_t output_register_count = sizeof(MatchRange) / sizeof(int32_t);

  int capture_count = regexp->CaptureCount();

  int num_matches = ExecRaw(*regexp, *subject, output_registers,
                            output_register_count, subject_index);

  if (num_matches == 0) {
    return isolate->factory()->null_value();
  } else {
    DCHECK_EQ(num_matches, 1);
    return RegExp::SetLastMatchInfo(isolate, last_match_info, subject,
                                    capture_count, output_registers);
    return last_match_info;
  }
}

}  // namespace internal
}  // namespace v8
