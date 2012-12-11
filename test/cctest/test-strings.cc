// Copyright 2012 the V8 project authors. All rights reserved.

// Check that we can traverse very deep stacks of ConsStrings using
// StringInputBuffer.  Check that Get(int) works on very deep stacks
// of ConsStrings.  These operations may not be very fast, but they
// should be possible without getting errors due to too deep recursion.

#include <stdlib.h>

#include "v8.h"

#include "api.h"
#include "factory.h"
#include "objects.h"
#include "cctest.h"
#include "zone-inl.h"

// Adapted from http://en.wikipedia.org/wiki/Multiply-with-carry
class RandomNumberGenerator {
 public:
  RandomNumberGenerator() {
    init();
  }

  void init(uint32_t seed = 0x5688c73e) {
    static const uint32_t phi = 0x9e3779b9;
    c = 362436;
    i = kQSize-1;
    Q[0] = seed;
    Q[1] = seed + phi;
    Q[2] = seed + phi + phi;
    for (unsigned j = 3; j < kQSize; j++) {
      Q[j] = Q[j - 3] ^ Q[j - 2] ^ phi ^ j;
    }
  }

  uint32_t next() {
    uint64_t a = 18782;
    uint32_t r = 0xfffffffe;
    i = (i + 1) & (kQSize-1);
    uint64_t t = a * Q[i] + c;
    c = (t >> 32);
    uint32_t x = static_cast<uint32_t>(t + c);
    if (x < c) {
      x++;
      c++;
    }
    return (Q[i] = r - x);
  }

  uint32_t next(int max) {
    return next() % max;
  }

  bool next(double threshold) {
    ASSERT(threshold >= 0.0 && threshold <= 1.0);
    if (threshold == 1.0) return true;
    if (threshold == 0.0) return false;
    uint32_t value = next() % 100000;
    return threshold > static_cast<double>(value)/100000.0;
  }

 private:
  static const uint32_t kQSize = 4096;
  uint32_t Q[kQSize];
  uint32_t c;
  uint32_t i;
};


using namespace v8::internal;

static v8::Persistent<v8::Context> env;


static void InitializeVM() {
  if (env.IsEmpty()) {
    v8::HandleScope scope;
    const char* extensions[] = { "v8/print" };
    v8::ExtensionConfiguration config(1, extensions);
    env = v8::Context::New(&config);
  }
  v8::HandleScope scope;
  env->Enter();
}


static const int NUMBER_OF_BUILDING_BLOCKS = 256;
static const int DEEP_DEPTH = 8 * 1024;
static const int SUPER_DEEP_DEPTH = 80 * 1024;


class Resource: public v8::String::ExternalStringResource,
                public ZoneObject {
 public:
  explicit Resource(Vector<const uc16> string): data_(string.start()) {
    length_ = string.length();
  }
  virtual const uint16_t* data() const { return data_; }
  virtual size_t length() const { return length_; }

 private:
  const uc16* data_;
  size_t length_;
};


class AsciiResource: public v8::String::ExternalAsciiStringResource,
                public ZoneObject {
 public:
  explicit AsciiResource(Vector<const char> string): data_(string.start()) {
    length_ = string.length();
  }
  virtual const char* data() const { return data_; }
  virtual size_t length() const { return length_; }

 private:
  const char* data_;
  size_t length_;
};


static void InitializeBuildingBlocks(Handle<String>* building_blocks,
                                     int bb_length,
                                     bool long_blocks,
                                     RandomNumberGenerator* rng) {
  // A list of pointers that we don't have any interest in cleaning up.
  // If they are reachable from a root then leak detection won't complain.
  Zone* zone = Isolate::Current()->runtime_zone();
  for (int i = 0; i < bb_length; i++) {
    int len = rng->next(16);
    int slice_head_chars = 0;
    int slice_tail_chars = 0;
    int slice_depth = 0;
    for (int j = 0; j < 3; j++) {
      if (rng->next(0.35)) slice_depth++;
    }
    // Must truncate something for a slice string. Loop until
    // at least one end will be sliced.
    while (slice_head_chars == 0 && slice_tail_chars == 0) {
      slice_head_chars = rng->next(15);
      slice_tail_chars = rng->next(12);
    }
    if (long_blocks) {
      // Generate building blocks which will never be merged
      len += ConsString::kMinLength + 1;
    } else if (len > 14) {
      len += 1234;
    }
    // Don't slice 0 length strings.
    if (len == 0) slice_depth = 0;
    int slice_length = slice_depth*(slice_head_chars + slice_tail_chars);
    len += slice_length;
    switch (rng->next(4)) {
      case 0: {
        uc16 buf[2000];
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x10000);
        }
        building_blocks[i] =
            FACTORY->NewStringFromTwoByte(Vector<const uc16>(buf, len));
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 1: {
        char buf[2000];
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x80);
        }
        building_blocks[i] =
            FACTORY->NewStringFromAscii(Vector<const char>(buf, len));
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 2: {
        uc16* buf = zone->NewArray<uc16>(len);
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(0x10000);
        }
        Resource* resource = new(zone) Resource(Vector<const uc16>(buf, len));
        building_blocks[i] = FACTORY->NewExternalStringFromTwoByte(resource);
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
      case 3: {
        char* buf = zone->NewArray<char>(len);
        for (int j = 0; j < len; j++) {
          buf[j] = rng->next(128);
        }
        AsciiResource* resource =
            new(zone) AsciiResource(Vector<const char>(buf, len));
        building_blocks[i] = FACTORY->NewExternalStringFromAscii(resource);
        for (int j = 0; j < len; j++) {
          CHECK_EQ(buf[j], building_blocks[i]->Get(j));
        }
        break;
      }
    }
    for (int j = slice_depth; j > 0; j--) {
      building_blocks[i] = FACTORY->NewSubString(
          building_blocks[i],
          slice_head_chars,
          building_blocks[i]->length() - slice_tail_chars);
    }
    CHECK(len == building_blocks[i]->length() + slice_length);
  }
}


static Handle<String> ConstructLeft(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS],
    int depth) {
  Handle<String> answer = FACTORY->NewStringFromAscii(CStrVector(""));
  for (int i = 0; i < depth; i++) {
    answer = FACTORY->NewConsString(
        answer,
        building_blocks[i % NUMBER_OF_BUILDING_BLOCKS]);
  }
  return answer;
}


static Handle<String> ConstructRight(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS],
    int depth) {
  Handle<String> answer = FACTORY->NewStringFromAscii(CStrVector(""));
  for (int i = depth - 1; i >= 0; i--) {
    answer = FACTORY->NewConsString(
        building_blocks[i % NUMBER_OF_BUILDING_BLOCKS],
        answer);
  }
  return answer;
}


static Handle<String> ConstructBalancedHelper(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS],
    int from,
    int to) {
  CHECK(to > from);
  if (to - from == 1) {
    return building_blocks[from % NUMBER_OF_BUILDING_BLOCKS];
  }
  if (to - from == 2) {
    return FACTORY->NewConsString(
        building_blocks[from % NUMBER_OF_BUILDING_BLOCKS],
        building_blocks[(from+1) % NUMBER_OF_BUILDING_BLOCKS]);
  }
  Handle<String> part1 =
    ConstructBalancedHelper(building_blocks, from, from + ((to - from) / 2));
  Handle<String> part2 =
    ConstructBalancedHelper(building_blocks, from + ((to - from) / 2), to);
  return FACTORY->NewConsString(part1, part2);
}


static Handle<String> ConstructBalanced(
    Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS]) {
  return ConstructBalancedHelper(building_blocks, 0, DEEP_DEPTH);
}


static StringInputBuffer buffer;
static ConsStringIteratorOp cons_string_iterator_op_1;
static ConsStringIteratorOp cons_string_iterator_op_2;

static void Traverse(Handle<String> s1, Handle<String> s2) {
  int i = 0;
  buffer.Reset(*s1);
  StringCharacterStream character_stream_1(*s1, 0, &cons_string_iterator_op_1);
  StringCharacterStream character_stream_2(*s2, 0, &cons_string_iterator_op_2);
  StringInputBuffer buffer2(*s2);
  while (buffer.has_more()) {
    CHECK(buffer2.has_more());
    CHECK(character_stream_1.HasMore());
    CHECK(character_stream_2.HasMore());
    uint16_t c = buffer.GetNext();
    CHECK_EQ(c, buffer2.GetNext());
    CHECK_EQ(c, character_stream_1.GetNext());
    CHECK_EQ(c, character_stream_2.GetNext());
    i++;
  }
  CHECK(!character_stream_1.HasMore());
  CHECK(!character_stream_2.HasMore());
  CHECK_EQ(s1->length(), i);
  CHECK_EQ(s2->length(), i);
}


static void TraverseFirst(Handle<String> s1, Handle<String> s2, int chars) {
  int i = 0;
  buffer.Reset(*s1);
  StringInputBuffer buffer2(*s2);
  StringCharacterStream character_stream_1(*s1, 0, &cons_string_iterator_op_1);
  StringCharacterStream character_stream_2(*s2, 0, &cons_string_iterator_op_2);
  while (buffer.has_more() && i < chars) {
    CHECK(buffer2.has_more());
    CHECK(character_stream_1.HasMore());
    CHECK(character_stream_2.HasMore());
    uint16_t c = buffer.GetNext();
    CHECK_EQ(c, buffer2.GetNext());
    CHECK_EQ(c, character_stream_1.GetNext());
    CHECK_EQ(c, character_stream_2.GetNext());
    i++;
  }
  s1->Get(s1->length() - 1);
  s2->Get(s2->length() - 1);
}


TEST(Traverse) {
  printf("TestTraverse\n");
  InitializeVM();
  v8::HandleScope scope;
  Handle<String> building_blocks[NUMBER_OF_BUILDING_BLOCKS];
  ZoneScope zone(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);
  RandomNumberGenerator rng;
  rng.init();
  InitializeBuildingBlocks(
      building_blocks, NUMBER_OF_BUILDING_BLOCKS, false, &rng);
  Handle<String> flat = ConstructBalanced(building_blocks);
  FlattenString(flat);
  Handle<String> left_asymmetric = ConstructLeft(building_blocks, DEEP_DEPTH);
  Handle<String> right_asymmetric = ConstructRight(building_blocks, DEEP_DEPTH);
  Handle<String> symmetric = ConstructBalanced(building_blocks);
  printf("1\n");
  Traverse(flat, symmetric);
  printf("2\n");
  Traverse(flat, left_asymmetric);
  printf("3\n");
  Traverse(flat, right_asymmetric);
  printf("4\n");
  Handle<String> left_deep_asymmetric =
      ConstructLeft(building_blocks, SUPER_DEEP_DEPTH);
  Handle<String> right_deep_asymmetric =
      ConstructRight(building_blocks, SUPER_DEEP_DEPTH);
  printf("5\n");
  TraverseFirst(left_asymmetric, left_deep_asymmetric, 1050);
  printf("6\n");
  TraverseFirst(left_asymmetric, right_deep_asymmetric, 65536);
  printf("7\n");
  FlattenString(left_asymmetric);
  printf("10\n");
  Traverse(flat, left_asymmetric);
  printf("11\n");
  FlattenString(right_asymmetric);
  printf("12\n");
  Traverse(flat, right_asymmetric);
  printf("14\n");
  FlattenString(symmetric);
  printf("15\n");
  Traverse(flat, symmetric);
  printf("16\n");
  FlattenString(left_deep_asymmetric);
  printf("18\n");
}


class ConsStringStats {
 public:
  ConsStringStats() {
    Reset();
  }
  void Reset();
  void VerifyEqual(const ConsStringStats& that) const;
  unsigned leaves_;
  unsigned empty_leaves_;
  unsigned chars_;
  unsigned left_traversals_;
  unsigned right_traversals_;
 private:
  DISALLOW_COPY_AND_ASSIGN(ConsStringStats);
};


void ConsStringStats::Reset() {
  leaves_ = 0;
  empty_leaves_ = 0;
  chars_ = 0;
  left_traversals_ = 0;
  right_traversals_ = 0;
}


void ConsStringStats::VerifyEqual(const ConsStringStats& that) const {
  CHECK(this->leaves_ == that.leaves_);
  CHECK(this->empty_leaves_ == that.empty_leaves_);
  CHECK(this->chars_ == that.chars_);
  CHECK(this->left_traversals_ == that.left_traversals_);
  CHECK(this->right_traversals_ == that.right_traversals_);
}


class ConsStringGenerationData {
 public:
  ConsStringGenerationData();
  void Reset();
  // Input variables.
  double early_termination_threshold_;
  double leftness_;
  double rightness_;
  double empty_leaf_threshold_;
  unsigned max_leaves_;
  // Cached data.
  Handle<String> building_blocks_[NUMBER_OF_BUILDING_BLOCKS];
  String* empty_string_;
  RandomNumberGenerator rng_;
  // Stats.
  ConsStringStats stats_;
  unsigned early_terminations_;
 private:
  DISALLOW_COPY_AND_ASSIGN(ConsStringGenerationData);
};


ConsStringGenerationData::ConsStringGenerationData() {
  rng_.init();
  InitializeBuildingBlocks(
      building_blocks_, NUMBER_OF_BUILDING_BLOCKS, true, &rng_);
  empty_string_ = Isolate::Current()->heap()->empty_string();
  Reset();
}


void ConsStringGenerationData::Reset() {
  early_termination_threshold_ = 0.01;
  leftness_ = 0.75;
  rightness_ = 0.75;
  empty_leaf_threshold_ = 0.02;
  max_leaves_ = 1000;
  stats_.Reset();
  early_terminations_ = 0;
}


void VerifyConsString(ConsString* cons_string, ConsStringStats* stats) {
  int left_length = cons_string->first()->length();
  int right_length = cons_string->second()->length();
  CHECK(cons_string->length() == left_length + right_length);
  // Check left side.
  if (cons_string->first()->IsConsString()) {
    stats->left_traversals_++;
    VerifyConsString(ConsString::cast(cons_string->first()), stats);
  } else {
    CHECK_NE(left_length, 0);
    stats->leaves_++;
    stats->chars_ += left_length;
  }
  // Check right side.
  if (cons_string->second()->IsConsString()) {
    stats->right_traversals_++;
    VerifyConsString(ConsString::cast(cons_string->second()), stats);
  } else {
    if (right_length == 0) stats->empty_leaves_++;
    stats->leaves_++;
    stats->chars_ += right_length;
  }
}


void VerifyConsStringWithOperator(
    ConsString* cons_string, ConsStringStats* stats) {
  // Init op.
  ConsStringIteratorOp op;
  op.Reset();
  // Use response for initial search and on blown stack.
  ConsStringIteratorOp::ContinueResponse response;
  response.string_ = cons_string;
  response.offset_ = 0;
  response.type_ = cons_string->map()->instance_type();
  response.length_ = (uint32_t) cons_string->length();
  while (true) {
    String* string = op.Operate(ConsString::cast(response.string_),
                                &response.offset_,
                                &response.type_,
                                &response.length_);
    CHECK(string != NULL);
    while (true) {
      // Accumulate stats.
      stats->leaves_++;
      stats->chars_ += string->length();
      // Check for completion.
      bool keep_going_fast_check = op.HasMore();
      bool keep_going = op.ContinueOperation(&response);
      if (!keep_going) return;
      // Verify no false positives for fast check.
      CHECK(keep_going_fast_check);
      CHECK(response.string_ != NULL);
      // Blew stack. Restart outer loop.
      if (response.string_->IsConsString()) break;
      string = response.string_;
    }
  };
}


void VerifyConsString(Handle<String> root, ConsStringGenerationData* data) {
  // Verify basic data.
  CHECK(root->IsConsString());
  CHECK((unsigned)root->length() == data->stats_.chars_);
  // Recursive verify.
  ConsStringStats stats;
  VerifyConsString(ConsString::cast(*root), &stats);
  stats.VerifyEqual(data->stats_);
  // Iteratively verify.
  stats.Reset();
  VerifyConsStringWithOperator(ConsString::cast(*root), &stats);
  // Don't see these. Must copy over.
  stats.empty_leaves_ = data->stats_.empty_leaves_;
  stats.left_traversals_ = data->stats_.left_traversals_;
  stats.right_traversals_ = data->stats_.right_traversals_;
  // Adjust total leaves to compensate.
  stats.leaves_ += stats.empty_leaves_;
  stats.VerifyEqual(data->stats_);
}


static Handle<String> ConstructRandomString(ConsStringGenerationData* data,
                                            unsigned max_recursion) {
  // Compute termination characteristics.
  bool terminate = false;
  bool flat = data->rng_.next(data->empty_leaf_threshold_);
  bool terminate_early = data->rng_.next(data->early_termination_threshold_);
  if (terminate_early) data->early_terminations_++;
  // The obvious condition.
  terminate |= max_recursion == 0;
  // Flat cons string terminate by definition.
  terminate |= flat;
  // Cap for max leaves.
  terminate |= data->stats_.leaves_ >= data->max_leaves_;
  // Roll the dice.
  terminate |= terminate_early;
  // Compute termination characteristics for each side.
  bool terminate_left = terminate || !data->rng_.next(data->leftness_);
  bool terminate_right = terminate || !data->rng_.next(data->rightness_);
  // Generate left string.
  Handle<String> left;
  if (terminate_left) {
    left = data->building_blocks_[data->rng_.next(NUMBER_OF_BUILDING_BLOCKS)];
    data->stats_.leaves_++;
    data->stats_.chars_ += left->length();
  } else {
    left = ConstructRandomString(data, max_recursion - 1);
    data->stats_.left_traversals_++;
  }
  // Generate right string.
  Handle<String> right;
  if (terminate_right) {
    right = data->building_blocks_[data->rng_.next(NUMBER_OF_BUILDING_BLOCKS)];
    data->stats_.leaves_++;
    data->stats_.chars_ += right->length();
  } else {
    right = ConstructRandomString(data, max_recursion - 1);
    data->stats_.right_traversals_++;
  }
  // Build the cons string.
  Handle<String> root = FACTORY->NewConsString(left, right);
  CHECK(root->IsConsString() && !root->IsFlat());
  // Special work needed for flat string.
  if (flat) {
    data->stats_.empty_leaves_++;
    FlattenString(root);
    CHECK(root->IsConsString() && root->IsFlat());
  }
  return root;
}


static const int kCharacterStreamRandomCases = 150;
static const int kCharacterStreamEdgeCases =
    kCharacterStreamRandomCases + 5;


static Handle<String> BuildConsStrings(int testCase,
                                       ConsStringGenerationData* data) {
  // For random constructions, need to reset the generator.
  data->rng_.init();
  for (int j = 0; j < testCase * 50; j++) {
    data->rng_.next();
  }
  Handle<String> string;
  switch (testCase) {
    case 0:
      return ConstructBalanced(data->building_blocks_);
    case 1:
      return ConstructLeft(data->building_blocks_, DEEP_DEPTH);
    case 2:
      return ConstructRight(data->building_blocks_, DEEP_DEPTH);
    case 3:
      return ConstructLeft(data->building_blocks_, 10);
    case 4:
      return ConstructRight(data->building_blocks_, 10);
    case 5:
      return FACTORY->NewConsString(
          data->building_blocks_[0], data->building_blocks_[1]);
    default:
      if (testCase >= kCharacterStreamEdgeCases) {
        CHECK(false);
        return string;
      }
      // Random test case.
      data->Reset();
      string = ConstructRandomString(data, 200);
      AssertNoAllocation no_alloc;
      VerifyConsString(string, data);
#ifdef DEBUG
      printf(
          "%s: [%d], %s: [%d], %s: [%d], %s: [%d], %s: [%d], %s: [%d]\n",
          "leaves", data->stats_.leaves_,
          "empty", data->stats_.empty_leaves_,
          "chars", data->stats_.chars_,
          "lefts", data->stats_.left_traversals_,
          "rights", data->stats_.right_traversals_,
          "early_terminations", data->early_terminations_);
#endif
      return string;
    }
}


static void VerifyCharacterStream(
    String* flat_string, String* cons_string) {
  // Do not want to test ConString traversal on flat string.
  CHECK(flat_string->IsFlat());
  CHECK(!flat_string->IsConsString());
  CHECK(cons_string->IsConsString());
  // TODO(dcarney) Test stream reset as well.
  int length = flat_string->length();
  // Iterate start search in multiple places in the string.
  int outer_iterations = length > 20 ? 20 : length;
  for (int j = 0; j <= outer_iterations; j++) {
    int offset = length * j / outer_iterations;
    if (offset < 0) offset = 0;
    // Want to test the offset == length case.
    if (offset > length) offset = length;
    StringCharacterStream flat_stream(
        flat_string, (unsigned) offset, &cons_string_iterator_op_1);
    StringCharacterStream cons_stream(
        cons_string, (unsigned) offset, &cons_string_iterator_op_2);
    for (int i = offset; i < length; i++) {
      uint16_t c = flat_string->Get(i);
      CHECK(flat_stream.HasMore());
      CHECK(cons_stream.HasMore());
      CHECK_EQ(c, flat_stream.GetNext());
      CHECK_EQ(c, cons_stream.GetNext());
    }
    CHECK(!flat_stream.HasMore());
    CHECK(!cons_stream.HasMore());
  }
}


TEST(StringCharacterStreamEdgeCases) {
  printf("TestStringCharacterStreamEdgeCases\n");
  InitializeVM();
  Isolate* isolate = Isolate::Current();
  HandleScope outer_scope(isolate);
  ZoneScope zone(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);
  ConsStringGenerationData data;
  for (int i = 0; i < kCharacterStreamEdgeCases; i++) {
    printf("%d\n", i);
    isolate->heap()->CollectAllGarbage(
        Heap::kNoGCFlags, "must not allocate in loop");
    AlwaysAllocateScope always_allocate;
    HandleScope inner_scope(isolate);
    Handle<String> cons_string = BuildConsStrings(i, &data);
    Handle<String> flat_string = BuildConsStrings(i, &data);
    FlattenString(flat_string);
    AssertNoAllocation no_alloc;
    CHECK(flat_string->IsConsString() && flat_string->IsFlat());
    VerifyCharacterStream(ConsString::cast(*flat_string)->first(),
        *cons_string);
  }
}


static const int DEEP_ASCII_DEPTH = 100000;


TEST(DeepAscii) {
  printf("TestDeepAscii\n");
  InitializeVM();
  v8::HandleScope scope;

  char* foo = NewArray<char>(DEEP_ASCII_DEPTH);
  for (int i = 0; i < DEEP_ASCII_DEPTH; i++) {
    foo[i] = "foo "[i % 4];
  }
  Handle<String> string =
      FACTORY->NewStringFromAscii(Vector<const char>(foo, DEEP_ASCII_DEPTH));
  Handle<String> foo_string = FACTORY->NewStringFromAscii(CStrVector("foo"));
  for (int i = 0; i < DEEP_ASCII_DEPTH; i += 10) {
    string = FACTORY->NewConsString(string, foo_string);
  }
  Handle<String> flat_string = FACTORY->NewConsString(string, foo_string);
  FlattenString(flat_string);

  for (int i = 0; i < 500; i++) {
    TraverseFirst(flat_string, string, DEEP_ASCII_DEPTH);
  }
  DeleteArray<char>(foo);
}


TEST(Utf8Conversion) {
  // Smoke test for converting strings to utf-8.
  InitializeVM();
  v8::HandleScope handle_scope;
  // A simple ascii string
  const char* ascii_string = "abcdef12345";
  int len =
      v8::String::New(ascii_string,
                      StrLength(ascii_string))->Utf8Length();
  CHECK_EQ(StrLength(ascii_string), len);
  // A mixed ascii and non-ascii string
  // U+02E4 -> CB A4
  // U+0064 -> 64
  // U+12E4 -> E1 8B A4
  // U+0030 -> 30
  // U+3045 -> E3 81 85
  const uint16_t mixed_string[] = {0x02E4, 0x0064, 0x12E4, 0x0030, 0x3045};
  // The characters we expect to be output
  const unsigned char as_utf8[11] = {0xCB, 0xA4, 0x64, 0xE1, 0x8B, 0xA4, 0x30,
      0xE3, 0x81, 0x85, 0x00};
  // The number of bytes expected to be written for each length
  const int lengths[12] = {0, 0, 2, 3, 3, 3, 6, 7, 7, 7, 10, 11};
  const int char_lengths[12] = {0, 0, 1, 2, 2, 2, 3, 4, 4, 4, 5, 5};
  v8::Handle<v8::String> mixed = v8::String::New(mixed_string, 5);
  CHECK_EQ(10, mixed->Utf8Length());
  // Try encoding the string with all capacities
  char buffer[11];
  const char kNoChar = static_cast<char>(-1);
  for (int i = 0; i <= 11; i++) {
    // Clear the buffer before reusing it
    for (int j = 0; j < 11; j++)
      buffer[j] = kNoChar;
    int chars_written;
    int written = mixed->WriteUtf8(buffer, i, &chars_written);
    CHECK_EQ(lengths[i], written);
    CHECK_EQ(char_lengths[i], chars_written);
    // Check that the contents are correct
    for (int j = 0; j < lengths[i]; j++)
      CHECK_EQ(as_utf8[j], static_cast<unsigned char>(buffer[j]));
    // Check that the rest of the buffer hasn't been touched
    for (int j = lengths[i]; j < 11; j++)
      CHECK_EQ(kNoChar, buffer[j]);
  }
}


TEST(ExternalShortStringAdd) {
  ZoneScope zonescope(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);

  InitializeVM();
  v8::HandleScope handle_scope;
  Zone* zone = Isolate::Current()->runtime_zone();

  // Make sure we cover all always-flat lengths and at least one above.
  static const int kMaxLength = 20;
  CHECK_GT(kMaxLength, i::ConsString::kMinLength);

  // Allocate two JavaScript arrays for holding short strings.
  v8::Handle<v8::Array> ascii_external_strings =
      v8::Array::New(kMaxLength + 1);
  v8::Handle<v8::Array> non_ascii_external_strings =
      v8::Array::New(kMaxLength + 1);

  // Generate short ascii and non-ascii external strings.
  for (int i = 0; i <= kMaxLength; i++) {
    char* ascii = zone->NewArray<char>(i + 1);
    for (int j = 0; j < i; j++) {
      ascii[j] = 'a';
    }
    // Terminating '\0' is left out on purpose. It is not required for external
    // string data.
    AsciiResource* ascii_resource =
        new(zone) AsciiResource(Vector<const char>(ascii, i));
    v8::Local<v8::String> ascii_external_string =
        v8::String::NewExternal(ascii_resource);

    ascii_external_strings->Set(v8::Integer::New(i), ascii_external_string);
    uc16* non_ascii = zone->NewArray<uc16>(i + 1);
    for (int j = 0; j < i; j++) {
      non_ascii[j] = 0x1234;
    }
    // Terminating '\0' is left out on purpose. It is not required for external
    // string data.
    Resource* resource = new(zone) Resource(Vector<const uc16>(non_ascii, i));
    v8::Local<v8::String> non_ascii_external_string =
      v8::String::NewExternal(resource);
    non_ascii_external_strings->Set(v8::Integer::New(i),
                                    non_ascii_external_string);
  }

  // Add the arrays with the short external strings in the global object.
  v8::Handle<v8::Object> global = env->Global();
  global->Set(v8_str("external_ascii"), ascii_external_strings);
  global->Set(v8_str("external_non_ascii"), non_ascii_external_strings);
  global->Set(v8_str("max_length"), v8::Integer::New(kMaxLength));

  // Add short external ascii and non-ascii strings checking the result.
  static const char* source =
    "function test() {"
    "  var ascii_chars = 'aaaaaaaaaaaaaaaaaaaa';"
    "  var non_ascii_chars = '\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234\\u1234';"  //NOLINT
    "  if (ascii_chars.length != max_length) return 1;"
    "  if (non_ascii_chars.length != max_length) return 2;"
    "  var ascii = Array(max_length + 1);"
    "  var non_ascii = Array(max_length + 1);"
    "  for (var i = 0; i <= max_length; i++) {"
    "    ascii[i] = ascii_chars.substring(0, i);"
    "    non_ascii[i] = non_ascii_chars.substring(0, i);"
    "  };"
    "  for (var i = 0; i <= max_length; i++) {"
    "    if (ascii[i] != external_ascii[i]) return 3;"
    "    if (non_ascii[i] != external_non_ascii[i]) return 4;"
    "    for (var j = 0; j < i; j++) {"
    "      if (external_ascii[i] !="
    "          (external_ascii[j] + external_ascii[i - j])) return 5;"
    "      if (external_non_ascii[i] !="
    "          (external_non_ascii[j] + external_non_ascii[i - j])) return 6;"
    "      if (non_ascii[i] != (non_ascii[j] + non_ascii[i - j])) return 7;"
    "      if (ascii[i] != (ascii[j] + ascii[i - j])) return 8;"
    "      if (ascii[i] != (external_ascii[j] + ascii[i - j])) return 9;"
    "      if (ascii[i] != (ascii[j] + external_ascii[i - j])) return 10;"
    "      if (non_ascii[i] !="
    "          (external_non_ascii[j] + non_ascii[i - j])) return 11;"
    "      if (non_ascii[i] !="
    "          (non_ascii[j] + external_non_ascii[i - j])) return 12;"
    "    }"
    "  }"
    "  return 0;"
    "};"
    "test()";
  CHECK_EQ(0, CompileRun(source)->Int32Value());
}


TEST(CachedHashOverflow) {
  // We incorrectly allowed strings to be tagged as array indices even if their
  // values didn't fit in the hash field.
  // See http://code.google.com/p/v8/issues/detail?id=728
  ZoneScope zone(Isolate::Current()->runtime_zone(), DELETE_ON_EXIT);

  InitializeVM();
  v8::HandleScope handle_scope;
  // Lines must be executed sequentially. Combining them into one script
  // makes the bug go away.
  const char* lines[] = {
      "var x = [];",
      "x[4] = 42;",
      "var s = \"1073741828\";",
      "x[s];",
      "x[s] = 37;",
      "x[4];",
      "x[s];",
      NULL
  };

  Handle<Smi> fortytwo(Smi::FromInt(42));
  Handle<Smi> thirtyseven(Smi::FromInt(37));
  Handle<Object> results[] = {
      FACTORY->undefined_value(),
      fortytwo,
      FACTORY->undefined_value(),
      FACTORY->undefined_value(),
      thirtyseven,
      fortytwo,
      thirtyseven  // Bug yielded 42 here.
  };

  const char* line;
  for (int i = 0; (line = lines[i]); i++) {
    printf("%s\n", line);
    v8::Local<v8::Value> result =
        v8::Script::Compile(v8::String::New(line))->Run();
    CHECK_EQ(results[i]->IsUndefined(), result->IsUndefined());
    CHECK_EQ(results[i]->IsNumber(), result->IsNumber());
    if (result->IsNumber()) {
      CHECK_EQ(Smi::cast(results[i]->ToSmi()->ToObjectChecked())->value(),
               result->ToInt32()->Value());
    }
  }
}


TEST(SliceFromCons) {
  FLAG_string_slices = true;
  InitializeVM();
  v8::HandleScope scope;
  Handle<String> string =
      FACTORY->NewStringFromAscii(CStrVector("parentparentparent"));
  Handle<String> parent = FACTORY->NewConsString(string, string);
  CHECK(parent->IsConsString());
  CHECK(!parent->IsFlat());
  Handle<String> slice = FACTORY->NewSubString(parent, 1, 25);
  // After slicing, the original string becomes a flat cons.
  CHECK(parent->IsFlat());
  CHECK(slice->IsSlicedString());
  CHECK_EQ(SlicedString::cast(*slice)->parent(),
           ConsString::cast(*parent)->first());
  CHECK(SlicedString::cast(*slice)->parent()->IsSeqString());
  CHECK(slice->IsFlat());
}


class AsciiVectorResource : public v8::String::ExternalAsciiStringResource {
 public:
  explicit AsciiVectorResource(i::Vector<const char> vector)
      : data_(vector) {}
  virtual ~AsciiVectorResource() {}
  virtual size_t length() const { return data_.length(); }
  virtual const char* data() const { return data_.start(); }
 private:
  i::Vector<const char> data_;
};


TEST(SliceFromExternal) {
  FLAG_string_slices = true;
  InitializeVM();
  v8::HandleScope scope;
  AsciiVectorResource resource(
      i::Vector<const char>("abcdefghijklmnopqrstuvwxyz", 26));
  Handle<String> string = FACTORY->NewExternalStringFromAscii(&resource);
  CHECK(string->IsExternalString());
  Handle<String> slice = FACTORY->NewSubString(string, 1, 25);
  CHECK(slice->IsSlicedString());
  CHECK(string->IsExternalString());
  CHECK_EQ(SlicedString::cast(*slice)->parent(), *string);
  CHECK(SlicedString::cast(*slice)->parent()->IsExternalString());
  CHECK(slice->IsFlat());
}


TEST(TrivialSlice) {
  // This tests whether a slice that contains the entire parent string
  // actually creates a new string (it should not).
  FLAG_string_slices = true;
  InitializeVM();
  HandleScope scope;
  v8::Local<v8::Value> result;
  Handle<String> string;
  const char* init = "var str = 'abcdefghijklmnopqrstuvwxyz';";
  const char* check = "str.slice(0,26)";
  const char* crosscheck = "str.slice(1,25)";

  CompileRun(init);

  result = CompileRun(check);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(!string->IsSlicedString());

  string = FACTORY->NewSubString(string, 0, 26);
  CHECK(!string->IsSlicedString());
  result = CompileRun(crosscheck);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSlicedString());
  CHECK_EQ("bcdefghijklmnopqrstuvwxy", *(string->ToCString()));
}


TEST(SliceFromSlice) {
  // This tests whether a slice that contains the entire parent string
  // actually creates a new string (it should not).
  FLAG_string_slices = true;
  InitializeVM();
  HandleScope scope;
  v8::Local<v8::Value> result;
  Handle<String> string;
  const char* init = "var str = 'abcdefghijklmnopqrstuvwxyz';";
  const char* slice = "var slice = str.slice(1,-1); slice";
  const char* slice_from_slice = "slice.slice(1,-1);";

  CompileRun(init);
  result = CompileRun(slice);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSlicedString());
  CHECK(SlicedString::cast(*string)->parent()->IsSeqString());
  CHECK_EQ("bcdefghijklmnopqrstuvwxy", *(string->ToCString()));

  result = CompileRun(slice_from_slice);
  CHECK(result->IsString());
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSlicedString());
  CHECK(SlicedString::cast(*string)->parent()->IsSeqString());
  CHECK_EQ("cdefghijklmnopqrstuvwx", *(string->ToCString()));
}


TEST(AsciiArrayJoin) {
  // Set heap limits.
  static const int K = 1024;
  v8::ResourceConstraints constraints;
  constraints.set_max_young_space_size(256 * K);
  constraints.set_max_old_space_size(4 * K * K);
  v8::SetResourceConstraints(&constraints);

  // String s is made of 2^17 = 131072 'c' characters and a is an array
  // starting with 'bad', followed by 2^14 times the string s. That means the
  // total length of the concatenated strings is 2^31 + 3. So on 32bit systems
  // summing the lengths of the strings (as Smis) overflows and wraps.
  static const char* join_causing_out_of_memory =
      "var two_14 = Math.pow(2, 14);"
      "var two_17 = Math.pow(2, 17);"
      "var s = Array(two_17 + 1).join('c');"
      "var a = ['bad'];"
      "for (var i = 1; i <= two_14; i++) a.push(s);"
      "a.join("");";

  v8::HandleScope scope;
  LocalContext context;
  v8::V8::IgnoreOutOfMemoryException();
  v8::Local<v8::Script> script =
      v8::Script::Compile(v8::String::New(join_causing_out_of_memory));
  v8::Local<v8::Value> result = script->Run();

  // Check for out of memory state.
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


static void CheckException(const char* source) {
  // An empty handle is returned upon exception.
  CHECK(CompileRun(source).IsEmpty());
}


TEST(RobustSubStringStub) {
  // This tests whether the SubStringStub can handle unsafe arguments.
  // If not recognized, those unsafe arguments lead to out-of-bounds reads.
  FLAG_allow_natives_syntax = true;
  InitializeVM();
  HandleScope scope;
  v8::Local<v8::Value> result;
  Handle<String> string;
  CompileRun("var short = 'abcdef';");

  // Invalid indices.
  CheckException("%_SubString(short,     0,    10000);");
  CheckException("%_SubString(short, -1234,        5);");
  CheckException("%_SubString(short,     5,        2);");
  // Special HeapNumbers.
  CheckException("%_SubString(short,     1, Infinity);");
  CheckException("%_SubString(short,   NaN,        5);");
  // String arguments.
  CheckException("%_SubString(short,    '2',     '5');");
  // Ordinary HeapNumbers can be handled (in runtime).
  result = CompileRun("%_SubString(short, Math.sqrt(4), 5.1);");
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK_EQ("cde", *(string->ToCString()));

  CompileRun("var long = 'abcdefghijklmnopqrstuvwxyz';");
  // Invalid indices.
  CheckException("%_SubString(long,     0,    10000);");
  CheckException("%_SubString(long, -1234,       17);");
  CheckException("%_SubString(long,    17,        2);");
  // Special HeapNumbers.
  CheckException("%_SubString(long,     1, Infinity);");
  CheckException("%_SubString(long,   NaN,       17);");
  // String arguments.
  CheckException("%_SubString(long,    '2',    '17');");
  // Ordinary HeapNumbers within bounds can be handled (in runtime).
  result = CompileRun("%_SubString(long, Math.sqrt(4), 17.1);");
  string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK_EQ("cdefghijklmnopq", *(string->ToCString()));

  // Test that out-of-bounds substring of a slice fails when the indices
  // would have been valid for the underlying string.
  CompileRun("var slice = long.slice(1, 15);");
  CheckException("%_SubString(slice, 0, 17);");
}


TEST(RegExpOverflow) {
  // Result string has the length 2^32, causing a 32-bit integer overflow.
  InitializeVM();
  HandleScope scope;
  LocalContext context;
  v8::V8::IgnoreOutOfMemoryException();
  v8::Local<v8::Value> result = CompileRun(
      "var a = 'a';                     "
      "for (var i = 0; i < 16; i++) {   "
      "  a += a;                        "
      "}                                "
      "a.replace(/a/g, a);              ");
  CHECK(result.IsEmpty());
  CHECK(context->HasOutOfMemoryException());
}


TEST(StringReplaceAtomTwoByteResult) {
  InitializeVM();
  HandleScope scope;
  LocalContext context;
  v8::Local<v8::Value> result = CompileRun(
      "var subject = 'ascii~only~string~'; "
      "var replace = '\x80';            "
      "subject.replace(/~/g, replace);  ");
  CHECK(result->IsString());
  Handle<String> string = v8::Utils::OpenHandle(v8::String::Cast(*result));
  CHECK(string->IsSeqTwoByteString());

  v8::Local<v8::String> expected = v8_str("ascii\x80only\x80string\x80");
  CHECK(expected->Equals(result));
}


TEST(IsAscii) {
  CHECK(String::IsAscii(static_cast<char*>(NULL), 0));
  CHECK(String::IsAscii(static_cast<uc16*>(NULL), 0));
}
