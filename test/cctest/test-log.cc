// Copyright 2006-2009 the V8 project authors. All rights reserved.
//
// Tests of logging functions from log.h

#ifdef ENABLE_LOGGING_AND_PROFILING

#include "v8.h"

#include "log.h"
#include "cctest.h"

using v8::internal::Address;
using v8::internal::Logger;

namespace i = v8::internal;

static void SetUp() {
  // Log to memory buffer.
  i::FLAG_logfile = "*";
  i::FLAG_log = true;
  Logger::Setup();
}

static void TearDown() {
  Logger::TearDown();
}


TEST(EmptyLog) {
  SetUp();
  CHECK_EQ(0, Logger::GetLogLines(0, NULL, 0));
  CHECK_EQ(0, Logger::GetLogLines(100, NULL, 0));
  CHECK_EQ(0, Logger::GetLogLines(0, NULL, 100));
  CHECK_EQ(0, Logger::GetLogLines(100, NULL, 100));
  TearDown();
}


TEST(GetMessages) {
  SetUp();
  Logger::StringEvent("aaa", "bbb");
  Logger::StringEvent("cccc", "dddd");
  CHECK_EQ(0, Logger::GetLogLines(0, NULL, 0));
  char log_lines[100];
  memset(log_lines, 0, sizeof(log_lines));
  // Requesting data size which is smaller than first log message length.
  CHECK_EQ(0, Logger::GetLogLines(0, log_lines, 3));
  // See Logger::StringEvent.
  const char* line_1 = "aaa,\"bbb\"\n";
  const int line_1_len = strlen(line_1);
  // Still smaller than log message length.
  CHECK_EQ(0, Logger::GetLogLines(0, log_lines, line_1_len - 1));
  // The exact size.
  CHECK_EQ(line_1_len, Logger::GetLogLines(0, log_lines, line_1_len));
  CHECK_EQ(line_1, log_lines);
  memset(log_lines, 0, sizeof(log_lines));
  // A bit more than the first line length.
  CHECK_EQ(line_1_len, Logger::GetLogLines(0, log_lines, line_1_len + 3));
  CHECK_EQ(line_1, log_lines);
  memset(log_lines, 0, sizeof(log_lines));
  const char* line_2 = "cccc,\"dddd\"\n";
  const int line_2_len = strlen(line_2);
  // Now start with line_2 beginning.
  CHECK_EQ(0, Logger::GetLogLines(line_1_len, log_lines, 0));
  CHECK_EQ(0, Logger::GetLogLines(line_1_len, log_lines, 3));
  CHECK_EQ(0, Logger::GetLogLines(line_1_len, log_lines, line_2_len - 1));
  CHECK_EQ(line_2_len, Logger::GetLogLines(line_1_len, log_lines, line_2_len));
  CHECK_EQ(line_2, log_lines);
  memset(log_lines, 0, sizeof(log_lines));
  CHECK_EQ(line_2_len,
           Logger::GetLogLines(line_1_len, log_lines, line_2_len + 3));
  CHECK_EQ(line_2, log_lines);
  memset(log_lines, 0, sizeof(log_lines));
  // Now get entire buffer contents.
  const char* all_lines = "aaa,\"bbb\"\ncccc,\"dddd\"\n";
  const int all_lines_len = strlen(all_lines);
  CHECK_EQ(all_lines_len, Logger::GetLogLines(0, log_lines, all_lines_len));
  CHECK_EQ(all_lines, log_lines);
  memset(log_lines, 0, sizeof(log_lines));
  CHECK_EQ(all_lines_len, Logger::GetLogLines(0, log_lines, all_lines_len + 3));
  CHECK_EQ(all_lines, log_lines);
  memset(log_lines, 0, sizeof(log_lines));
  TearDown();
}


TEST(BeyondWritePosition) {
  SetUp();
  Logger::StringEvent("aaa", "bbb");
  Logger::StringEvent("cccc", "dddd");
  // See Logger::StringEvent.
  const char* all_lines = "aaa,\"bbb\"\ncccc,\"dddd\"\n";
  const int all_lines_len = strlen(all_lines);
  CHECK_EQ(0, Logger::GetLogLines(all_lines_len, NULL, 1));
  CHECK_EQ(0, Logger::GetLogLines(all_lines_len, NULL, 100));
  CHECK_EQ(0, Logger::GetLogLines(all_lines_len + 1, NULL, 1));
  CHECK_EQ(0, Logger::GetLogLines(all_lines_len + 1, NULL, 100));
  CHECK_EQ(0, Logger::GetLogLines(all_lines_len + 100, NULL, 1));
  CHECK_EQ(0, Logger::GetLogLines(all_lines_len + 100, NULL, 100));
  CHECK_EQ(0, Logger::GetLogLines(10 * 1024 * 1024, NULL, 1));
  CHECK_EQ(0, Logger::GetLogLines(10 * 1024 * 1024, NULL, 100));
  TearDown();
}


TEST(MemoryLoggingTurnedOff) {
  // Log to stdout
  i::FLAG_logfile = "-";
  i::FLAG_log = true;
  Logger::Setup();
  CHECK_EQ(0, Logger::GetLogLines(0, NULL, 0));
  CHECK_EQ(0, Logger::GetLogLines(100, NULL, 0));
  CHECK_EQ(0, Logger::GetLogLines(0, NULL, 100));
  CHECK_EQ(0, Logger::GetLogLines(100, NULL, 100));
  Logger::TearDown();
}


static inline bool IsStringEqualTo(const char* r, const char* s) {
  return strncmp(r, s, strlen(r)) == 0;
}


static bool Consume(const char* str, char** buf) {
  if (IsStringEqualTo(str, *buf)) {
    *buf += strlen(str);
    return true;
  }
  return false;
}


static void ParseAddress(char* start, Address* min_addr, Address* max_addr) {
  Address addr = reinterpret_cast<Address>(strtoul(start, NULL, 16));  // NOLINT
  if (addr < *min_addr) *min_addr = addr;
  if (addr > *max_addr) *max_addr = addr;
}


static Address ConsumeAddress(
    char** start, Address min_addr, Address max_addr) {
  char* end_ptr;
  Address addr =
      reinterpret_cast<Address>(strtoul(*start, &end_ptr, 16));  // NOLINT
  CHECK_GE(addr, min_addr);
  CHECK_GE(max_addr, addr);
  *start = end_ptr;
  return addr;
}


namespace {

// A code entity is a pointer to a position of code-creation event in buffer log
// offset to a point where entity size begins, i.e.: '255,"func"\n'. This makes
// comparing code entities pretty easy.
typedef char* CodeEntityInfo;

// A structure used to return log parsing results.
class ParseLogResult {
 public:
  ParseLogResult()
      : min_addr(reinterpret_cast<Address>(-1)),
        max_addr(reinterpret_cast<Address>(0)),
        entities_map(NULL), entities(NULL),
        max_entities(0) {}

  ~ParseLogResult() {
    // See allocation code below.
    if (entities_map != NULL) {
      i::DeleteArray(entities_map - 1);
    }
    i::DeleteArray(entities);
  }

  void AllocateEntities() {
    // Make sure that the test doesn't operate on a bogus log.
    CHECK_GT(max_entities, 0);
    CHECK_GT(min_addr, 0);
    CHECK_GT(max_addr, min_addr);

    entities = i::NewArray<CodeEntityInfo>(max_entities);
    for (int i = 0; i < max_entities; ++i) {
      entities[i] = NULL;
    }
    // We're adding fake items at [-1] and [size + 1] to simplify
    // comparison code.
    const int map_length = max_addr - min_addr + 1 + 2;  // 2 fakes.
    entities_map = i::NewArray<int>(map_length);
    for (int i = 0; i < map_length; ++i) {
      entities_map[i] = -1;
    }
    entities_map += 1;  // Hide the -1 item, this is compensated on delete.
  }

  // Minimal code entity address.
  Address min_addr;
  // Maximal code entity address.
  Address max_addr;
  // Memory map of entities start addresses. Biased by min_addr.
  int* entities_map;
  // An array of code entities.
  CodeEntityInfo* entities;
  // Maximal entities count. Actual entities count can be lower,
  // empty entity slots are pointing to NULL.
  int max_entities;
};

}  // namespace


typedef void (*ParserBlock)(char* start, char* end, ParseLogResult* result);

static void ParserCycle(
    char* start, char* end, ParseLogResult* result,
    ParserBlock block_creation, ParserBlock block_delete,
    ParserBlock block_move) {

  const char* code_creation = "code-creation,";
  const char* code_delete = "code-delete,";
  const char* code_move = "code-move,";

  const char* lazy_compile = "LazyCompile,";
  const char* script = "Script,";
  const char* function = "Function,";

  while (start < end) {
    if (Consume(code_creation, &start)) {
      if (Consume(lazy_compile, &start)
          || Consume(script, &start)
          || Consume(function, &start)) {
        block_creation(start, end, result);
      }
    } else if (Consume(code_delete, &start)) {
      block_delete(start, end, result);
    } else if (Consume(code_move, &start)) {
      block_move(start, end, result);
    }
    while (start < end && *start != '\n') ++start;
    ++start;
  }
}


static void Pass1CodeCreation(char* start, char* end, ParseLogResult* result) {
  ParseAddress(start, &result->min_addr, &result->max_addr);
  ++result->max_entities;
}


static void Pass1CodeDelete(char* start, char* end, ParseLogResult* result) {
  ParseAddress(start, &result->min_addr, &result->max_addr);
}


static void Pass1CodeMove(char* start, char* end, ParseLogResult* result) {
  // Skip old address.
  while (start < end && *start != ',') ++start;
  CHECK_GT(end, start);
  ++start;  // Skip ','.
  ParseAddress(start, &result->min_addr, &result->max_addr);
}


static void Pass2CodeCreation(char* start, char* end, ParseLogResult* result) {
  Address addr = ConsumeAddress(&start, result->min_addr, result->max_addr);
  CHECK_GT(end, start);
  ++start;  // Skip ','.

  int idx = addr - result->min_addr;
  result->entities_map[idx] = -1;
  for (int i = 0; i < result->max_entities; ++i) {
    // Find an empty slot and fill it.
    if (result->entities[i] == NULL) {
      result->entities[i] = start;
      result->entities_map[idx] = i;
      break;
    }
  }
  // Make sure that a slot was found.
  CHECK_GE(result->entities_map[idx], 0);
}


static void Pass2CodeDelete(char* start, char* end, ParseLogResult* result) {
  Address addr = ConsumeAddress(&start, result->min_addr, result->max_addr);
  int idx = addr - result->min_addr;
  // There can be code deletes that are not related to JS code.
  if (result->entities_map[idx] >= 0) {
    result->entities[result->entities_map[idx]] = NULL;
    result->entities_map[idx] = -1;
  }
}


static void Pass2CodeMove(char* start, char* end, ParseLogResult* result) {
  Address from_addr = ConsumeAddress(
      &start, result->min_addr, result->max_addr);
  CHECK_GT(end, start);
  ++start;  // Skip ','.
  Address to_addr = ConsumeAddress(&start, result->min_addr, result->max_addr);
  CHECK_GT(end, start);

  int from_idx = from_addr - result->min_addr;
  int to_idx = to_addr - result->min_addr;
  // There can be code moves that are not related to JS code.
  if (from_idx != to_idx && result->entities_map[from_idx] >= 0) {
    CHECK_EQ(-1, result->entities_map[to_idx]);
    result->entities_map[to_idx] = result->entities_map[from_idx];
    result->entities_map[from_idx] = -1;
  };
}


static void ParseLog(char* start, char* end, ParseLogResult* result) {
  // Pass 1: Calculate boundaries of addresses and entities count.
  ParserCycle(start, end, result,
              Pass1CodeCreation, Pass1CodeDelete, Pass1CodeMove);

  printf("min_addr: %p, max_addr: %p, entities: %d\n",
         result->min_addr, result->max_addr, result->max_entities);

  result->AllocateEntities();

  // Pass 2: Fill in code entries data.
  ParserCycle(start, end, result,
              Pass2CodeCreation, Pass2CodeDelete, Pass2CodeMove);
}


static inline void PrintCodeEntityInfo(CodeEntityInfo entity) {
  const int max_len = 50;
  if (entity != NULL) {
    char* eol = strchr(entity, '\n');
    int len = eol - entity;
    len = len <= max_len ? len : max_len;
    printf("%-*.*s ", max_len, len, entity);
  } else {
    printf("%*s", max_len + 1, "");
  }
}


static void PrintCodeEntitiesInfo(
    bool is_equal, Address addr,
    CodeEntityInfo l_entity, CodeEntityInfo r_entity) {
  printf("%c %p ", is_equal ? ' ' : '*', addr);
  PrintCodeEntityInfo(l_entity);
  PrintCodeEntityInfo(r_entity);
  printf("\n");
}


static inline int StrChrLen(const char* s, char c) {
  return strchr(s, c) - s;
}


static bool AreFuncSizesEqual(CodeEntityInfo ref_s, CodeEntityInfo new_s) {
  int ref_len = StrChrLen(ref_s, ',');
  int new_len = StrChrLen(new_s, ',');
  return ref_len == new_len && strncmp(ref_s, new_s, ref_len) == 0;
}


static bool AreFuncNamesEqual(CodeEntityInfo ref_s, CodeEntityInfo new_s) {
  // Skip size.
  ref_s = strchr(ref_s, ',') + 1;
  new_s = strchr(new_s, ',') + 1;
  int ref_len = StrChrLen(ref_s, '\n');
  int new_len = StrChrLen(new_s, '\n');
  // If reference is anonymous (""), it's OK to have anything in new.
  if (ref_len == 2) return true;
  // A special case for ErrorPrototype. Haven't yet figured out why they
  // are different.
  const char* error_prototype = "\"ErrorPrototype";
  if (IsStringEqualTo(error_prototype, ref_s)
      && IsStringEqualTo(error_prototype, new_s)) {
    return true;
  }
  // Built-in objects have problems too.
  const char* built_ins[] = {
      "\"Boolean\"", "\"Function\"", "\"Number\"",
      "\"Object\"", "\"Script\"", "\"String\""
  };
  for (size_t i = 0; i < sizeof(built_ins) / sizeof(*built_ins); ++i) {
    if (IsStringEqualTo(built_ins[i], new_s)) {
      return true;
    }
  }
  return ref_len == new_len && strncmp(ref_s, new_s, ref_len) == 0;
}


static bool AreEntitiesEqual(CodeEntityInfo ref_e, CodeEntityInfo new_e) {
  if (ref_e == NULL && new_e != NULL) return true;
  if (ref_e != NULL && new_e != NULL) {
    return AreFuncSizesEqual(ref_e, new_e) && AreFuncNamesEqual(ref_e, new_e);
  }
  if (ref_e != NULL && new_e == NULL) {
    // args_count entities (argument adapters) are not found by heap traversal,
    // but they are not needed because they doesn't contain any code.
    ref_e = strchr(ref_e, ',') + 1;
    const char* args_count = "\"args_count:";
    return IsStringEqualTo(args_count, ref_e);
  }
  return false;
}


// Test that logging of code create / move / delete events
// is equivalent to traversal of a resulting heap.
TEST(EquivalenceOfLoggingAndTraversal) {
  i::FLAG_logfile = "*";
  i::FLAG_log = true;
  i::FLAG_log_code = true;

  // Make sure objects move.
  bool saved_always_compact = i::FLAG_always_compact;
  if (!i::FLAG_never_compact) {
    i::FLAG_always_compact = true;
  }

  v8::HandleScope scope;
  v8::Handle<v8::Value> global_object = v8::Handle<v8::Value>();
  v8::Persistent<v8::Context> env = v8::Context::New(
      0, v8::Handle<v8::ObjectTemplate>(), global_object);
  env->Enter();

  // Compile and run a function that creates other functions.
  v8::Local<v8::Script> script = v8::Script::Compile(v8::String::New(
      "(function f(obj) {\n"
      "  obj.test =\n"
      "    (function a(j) { return function b() { return j; } })(100);\n"
      "})(this);"));
  script->Run();
  i::Heap::CollectAllGarbage();

  i::EmbeddedVector<char, 204800> buffer;
  int log_size;
  ParseLogResult ref_result;

  // Retrieve the log.
  {
    // Make sure that no GCs occur prior to LogCompiledFunctions call.
    i::AssertNoAllocation no_alloc;

    log_size = Logger::GetLogLines(0, buffer.start(), buffer.length());
    CHECK_GT(log_size, 0);
    CHECK_GT(buffer.length(), log_size);

    // Fill a map of compiled code objects.
    ParseLog(buffer.start(), buffer.start() + log_size, &ref_result);
  }

  // Iterate heap to find compiled functions, will write to log.
  i::Logger::LogCompiledFunctions();
  char* new_log_start = buffer.start() + log_size;
  const int new_log_size = Logger::GetLogLines(
      log_size, new_log_start, buffer.length() - log_size);
  CHECK_GT(new_log_size, 0);
  CHECK_GT(buffer.length(), log_size + new_log_size);

  // Fill an equivalent map of compiled code objects.
  ParseLogResult new_result;
  ParseLog(new_log_start, new_log_start + new_log_size, &new_result);

  // Test their actual equivalence.
  bool results_equal = true;
  int ref_idx = -1, new_idx = -1, ref_inc = 1, new_inc = 1;
  while (ref_inc > 0 || new_inc > 0) {
    const Address ref_addr = ref_result.min_addr + ref_idx;
    const Address new_addr = new_result.min_addr + new_idx;
    ref_inc = ref_addr <= ref_result.max_addr && ref_addr <= new_addr ? 1 : 0;
    new_inc = new_addr <= new_result.max_addr && new_addr <= ref_addr ? 1 : 0;
    const int ref_item = ref_result.entities_map[ref_idx];
    const int new_item = new_result.entities_map[new_idx];
    if (ref_item != -1 || new_item != -1) {
      CodeEntityInfo ref_entity =
          ref_item != -1 ? ref_result.entities[ref_item] : NULL;
      CodeEntityInfo new_entity =
          new_item != -1 ? new_result.entities[new_item] : NULL;
      const bool equal = AreEntitiesEqual(ref_entity, new_entity);
      if (!equal) results_equal = false;
      PrintCodeEntitiesInfo(
          equal, ref_inc != 0 ? ref_addr : new_addr,
          ref_entity, new_entity);
    }
    ref_idx += ref_inc;
    new_idx += new_inc;
  }
  // Make sure that all log data is written prior crash due to CHECK failure.
  fflush(stdout);
  CHECK(results_equal);

  env->Exit();
  v8::V8::Dispose();
  i::FLAG_always_compact = saved_always_compact;
}


#endif  // ENABLE_LOGGING_AND_PROFILING
