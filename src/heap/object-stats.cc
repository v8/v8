// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/object-stats.h"

#include "src/counters.h"
#include "src/heap/heap-inl.h"
#include "src/isolate.h"
#include "src/utils.h"

namespace v8 {
namespace internal {

static base::LazyMutex object_stats_mutex = LAZY_MUTEX_INITIALIZER;


void ObjectStats::ClearObjectStats(bool clear_last_time_stats) {
  memset(object_counts_, 0, sizeof(object_counts_));
  memset(object_sizes_, 0, sizeof(object_sizes_));
  if (clear_last_time_stats) {
    memset(object_counts_last_time_, 0, sizeof(object_counts_last_time_));
    memset(object_sizes_last_time_, 0, sizeof(object_sizes_last_time_));
  }
}


void ObjectStats::TraceObjectStat(const char* name, int count, int size,
                                  double time) {
  int ms_count = heap()->ms_count();
  PrintIsolate(isolate(),
               "heap:%p, time:%f, gc:%d, type:%s, count:%d, size:%d\n",
               static_cast<void*>(heap()), time, ms_count, name, count, size);
}


void ObjectStats::TraceObjectStats() {
  base::LockGuard<base::Mutex> lock_guard(object_stats_mutex.Pointer());
  int index;
  int count;
  int size;
  int total_size = 0;
  double time = isolate()->time_millis_since_init();
#define TRACE_OBJECT_COUNT(name)                     \
  count = static_cast<int>(object_counts_[name]);    \
  size = static_cast<int>(object_sizes_[name]) / KB; \
  total_size += size;                                \
  TraceObjectStat(#name, count, size, time);
  INSTANCE_TYPE_LIST(TRACE_OBJECT_COUNT)
#undef TRACE_OBJECT_COUNT
#define TRACE_OBJECT_COUNT(name)                      \
  index = FIRST_CODE_KIND_SUB_TYPE + Code::name;      \
  count = static_cast<int>(object_counts_[index]);    \
  size = static_cast<int>(object_sizes_[index]) / KB; \
  TraceObjectStat("*CODE_" #name, count, size, time);
  CODE_KIND_LIST(TRACE_OBJECT_COUNT)
#undef TRACE_OBJECT_COUNT
#define TRACE_OBJECT_COUNT(name)                      \
  index = FIRST_FIXED_ARRAY_SUB_TYPE + name;          \
  count = static_cast<int>(object_counts_[index]);    \
  size = static_cast<int>(object_sizes_[index]) / KB; \
  TraceObjectStat("*FIXED_ARRAY_" #name, count, size, time);
  FIXED_ARRAY_SUB_INSTANCE_TYPE_LIST(TRACE_OBJECT_COUNT)
#undef TRACE_OBJECT_COUNT
#define TRACE_OBJECT_COUNT(name)                                              \
  index =                                                                     \
      FIRST_CODE_AGE_SUB_TYPE + Code::k##name##CodeAge - Code::kFirstCodeAge; \
  count = static_cast<int>(object_counts_[index]);                            \
  size = static_cast<int>(object_sizes_[index]) / KB;                         \
  TraceObjectStat("*CODE_AGE_" #name, count, size, time);
  CODE_AGE_LIST_COMPLETE(TRACE_OBJECT_COUNT)
#undef TRACE_OBJECT_COUNT
}


void ObjectStats::CheckpointObjectStats() {
  base::LockGuard<base::Mutex> lock_guard(object_stats_mutex.Pointer());
  Counters* counters = isolate()->counters();
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)              \
  counters->count_of_##name()->Increment(                \
      static_cast<int>(object_counts_[name]));           \
  counters->count_of_##name()->Decrement(                \
      static_cast<int>(object_counts_last_time_[name])); \
  counters->size_of_##name()->Increment(                 \
      static_cast<int>(object_sizes_[name]));            \
  counters->size_of_##name()->Decrement(                 \
      static_cast<int>(object_sizes_last_time_[name]));
  INSTANCE_TYPE_LIST(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT
  int index;
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)               \
  index = FIRST_CODE_KIND_SUB_TYPE + Code::name;          \
  counters->count_of_CODE_TYPE_##name()->Increment(       \
      static_cast<int>(object_counts_[index]));           \
  counters->count_of_CODE_TYPE_##name()->Decrement(       \
      static_cast<int>(object_counts_last_time_[index])); \
  counters->size_of_CODE_TYPE_##name()->Increment(        \
      static_cast<int>(object_sizes_[index]));            \
  counters->size_of_CODE_TYPE_##name()->Decrement(        \
      static_cast<int>(object_sizes_last_time_[index]));
  CODE_KIND_LIST(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)               \
  index = FIRST_FIXED_ARRAY_SUB_TYPE + name;              \
  counters->count_of_FIXED_ARRAY_##name()->Increment(     \
      static_cast<int>(object_counts_[index]));           \
  counters->count_of_FIXED_ARRAY_##name()->Decrement(     \
      static_cast<int>(object_counts_last_time_[index])); \
  counters->size_of_FIXED_ARRAY_##name()->Increment(      \
      static_cast<int>(object_sizes_[index]));            \
  counters->size_of_FIXED_ARRAY_##name()->Decrement(      \
      static_cast<int>(object_sizes_last_time_[index]));
  FIXED_ARRAY_SUB_INSTANCE_TYPE_LIST(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT
#define ADJUST_LAST_TIME_OBJECT_COUNT(name)                                   \
  index =                                                                     \
      FIRST_CODE_AGE_SUB_TYPE + Code::k##name##CodeAge - Code::kFirstCodeAge; \
  counters->count_of_CODE_AGE_##name()->Increment(                            \
      static_cast<int>(object_counts_[index]));                               \
  counters->count_of_CODE_AGE_##name()->Decrement(                            \
      static_cast<int>(object_counts_last_time_[index]));                     \
  counters->size_of_CODE_AGE_##name()->Increment(                             \
      static_cast<int>(object_sizes_[index]));                                \
  counters->size_of_CODE_AGE_##name()->Decrement(                             \
      static_cast<int>(object_sizes_last_time_[index]));
  CODE_AGE_LIST_COMPLETE(ADJUST_LAST_TIME_OBJECT_COUNT)
#undef ADJUST_LAST_TIME_OBJECT_COUNT

  MemCopy(object_counts_last_time_, object_counts_, sizeof(object_counts_));
  MemCopy(object_sizes_last_time_, object_sizes_, sizeof(object_sizes_));
  ClearObjectStats();
}


Isolate* ObjectStats::isolate() { return heap()->isolate(); }

}  // namespace internal
}  // namespace v8
