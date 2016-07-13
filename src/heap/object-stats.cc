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
  memset(over_allocated_, 0, sizeof(over_allocated_));
  memset(size_histogram_, 0, sizeof(size_histogram_));
  memset(over_allocated_histogram_, 0, sizeof(over_allocated_histogram_));
  if (clear_last_time_stats) {
    memset(object_counts_last_time_, 0, sizeof(object_counts_last_time_));
    memset(object_sizes_last_time_, 0, sizeof(object_sizes_last_time_));
  }
}

static void PrintJSONArray(size_t* array, const int len) {
  PrintF("[ ");
  for (int i = 0; i < len; i++) {
    PrintF("%zu", array[i]);
    if (i != (len - 1)) PrintF(", ");
  }
  PrintF(" ]");
}

void ObjectStats::PrintJSON(const char* key) {
  double time = isolate()->time_millis_since_init();
  int gc_count = heap()->gc_count();

#define PRINT_KEY_AND_ID()                                     \
  PrintF("\"isolate\": \"%p\", \"id\": %d, \"key\": \"%s\", ", \
         reinterpret_cast<void*>(isolate()), gc_count, key);

  // gc_descriptor
  PrintF("{ ");
  PRINT_KEY_AND_ID();
  PrintF("\"type\": \"gc_descriptor\", \"time\": %f }\n", time);
  // bucket_sizes
  PrintF("{ ");
  PRINT_KEY_AND_ID();
  PrintF("\"type\": \"bucket_sizes\", \"sizes\": [ ");
  for (int i = 0; i < kNumberOfBuckets; i++) {
    PrintF("%d", 1 << (kFirstBucketShift + i));
    if (i != (kNumberOfBuckets - 1)) PrintF(", ");
  }
  PrintF(" ] }\n");
// instance_type_data
#define PRINT_INSTANCE_TYPE_DATA(name, index)                         \
  PrintF("{ ");                                                       \
  PRINT_KEY_AND_ID();                                                 \
  PrintF("\"type\": \"instance_type_data\", ");                       \
  PrintF("\"instance_type\": %d, ", index);                           \
  PrintF("\"instance_type_name\": \"%s\", ", name);                   \
  PrintF("\"overall\": %zu, ", object_sizes_[index]);                 \
  PrintF("\"count\": %zu, ", object_counts_[index]);                  \
  PrintF("\"over_allocated\": %zu, ", over_allocated_[index]);        \
  PrintF("\"histogram\": ");                                          \
  PrintJSONArray(size_histogram_[index], kNumberOfBuckets);           \
  PrintF(",");                                                        \
  PrintF("\"over_allocated_histogram\": ");                           \
  PrintJSONArray(over_allocated_histogram_[index], kNumberOfBuckets); \
  PrintF(" }\n");

#define INSTANCE_TYPE_WRAPPER(name) PRINT_INSTANCE_TYPE_DATA(#name, name)
#define CODE_KIND_WRAPPER(name)            \
  PRINT_INSTANCE_TYPE_DATA("*CODE_" #name, \
                           FIRST_CODE_KIND_SUB_TYPE + Code::name)
#define FIXED_ARRAY_SUB_INSTANCE_TYPE_WRAPPER(name) \
  PRINT_INSTANCE_TYPE_DATA("*FIXED_ARRAY_" #name,   \
                           FIRST_FIXED_ARRAY_SUB_TYPE + name)
#define CODE_AGE_WRAPPER(name) \
  PRINT_INSTANCE_TYPE_DATA(    \
      "*CODE_AGE_" #name,      \
      FIRST_CODE_AGE_SUB_TYPE + Code::k##name##CodeAge - Code::kFirstCodeAge)

  INSTANCE_TYPE_LIST(INSTANCE_TYPE_WRAPPER)
  CODE_KIND_LIST(CODE_KIND_WRAPPER)
  FIXED_ARRAY_SUB_INSTANCE_TYPE_LIST(FIXED_ARRAY_SUB_INSTANCE_TYPE_WRAPPER)
  CODE_AGE_LIST_COMPLETE(CODE_AGE_WRAPPER)

#undef INSTANCE_TYPE_WRAPPER
#undef CODE_KIND_WRAPPER
#undef FIXED_ARRAY_SUB_INSTANCE_TYPE_WRAPPER
#undef CODE_AGE_WRAPPER
#undef PRINT_INSTANCE_TYPE_DATA
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

void ObjectStatsCollector::CollectStatistics(ObjectStats* stats,
                                             HeapObject* obj) {
  Map* map = obj->map();
  Heap* heap = obj->GetHeap();

  // Record for the InstanceType.
  int object_size = obj->Size();
  stats->RecordObjectStats(map->instance_type(), object_size);

  // Record specific sub types where possible.
  if (obj->IsMap()) {
    RecordMapDetails(stats, heap, obj);
  }
  if (obj->IsCode()) {
    RecordCodeDetails(stats, heap, obj);
  }
  if (obj->IsSharedFunctionInfo()) {
    RecordSharedFunctionInfoDetails(stats, heap, obj);
  }
  if (obj->IsFixedArray()) {
    RecordFixedArrayDetails(stats, heap, obj);
  }
  if (obj->IsJSObject()) {
    RecordJSObjectDetails(stats, heap, JSObject::cast(obj));
  }
  if (obj->IsJSWeakCollection()) {
    RecordJSWeakCollectionDetails(stats, heap, JSWeakCollection::cast(obj));
  }
}

static bool CanRecordFixedArray(Heap* heap, FixedArrayBase* array) {
  return array->map() != heap->fixed_cow_array_map() &&
         array->map() != heap->fixed_double_array_map() &&
         array != heap->empty_fixed_array();
}

void ObjectStatsCollector::RecordJSObjectDetails(ObjectStats* stats, Heap* heap,
                                                 JSObject* object) {
  DCHECK(object->IsJSObject());

  size_t overhead = 0;
  FixedArrayBase* elements = object->elements();
  if (CanRecordFixedArray(heap, elements)) {
    if (elements->IsDictionary()) {
      SeededNumberDictionary* dict = object->element_dictionary();
      int used = dict->NumberOfElements() * SeededNumberDictionary::kEntrySize;
      CHECK_GE(elements->Size(), used);
      overhead = elements->Size() - used;
      stats->RecordFixedArraySubTypeStats(DICTIONARY_ELEMENTS_SUB_TYPE,
                                          elements->Size(), overhead);
    } else {
      if (IsFastHoleyElementsKind(object->GetElementsKind())) {
        int used = object->GetFastElementsUsage() * kPointerSize;
        if (object->GetElementsKind() == FAST_HOLEY_DOUBLE_ELEMENTS) used *= 2;
        CHECK_GE(elements->Size(), used);
        overhead = elements->Size() - used;
      }
      stats->RecordFixedArraySubTypeStats(FAST_ELEMENTS_SUB_TYPE,
                                          elements->Size(), overhead);
    }
  }

  overhead = 0;
  FixedArrayBase* properties = object->properties();
  if (CanRecordFixedArray(heap, properties)) {
    if (properties->IsDictionary()) {
      NameDictionary* dict = object->property_dictionary();
      int used = dict->NumberOfElements() * NameDictionary::kEntrySize;
      CHECK_GE(properties->Size(), used);
      overhead = properties->Size() - used;
      stats->RecordFixedArraySubTypeStats(DICTIONARY_PROPERTIES_SUB_TYPE,
                                          properties->Size(), overhead);
    } else {
      stats->RecordFixedArraySubTypeStats(FAST_PROPERTIES_SUB_TYPE,
                                          properties->Size(), overhead);
    }
  }
}

void ObjectStatsCollector::RecordJSWeakCollectionDetails(
    ObjectStats* stats, Heap* heap, JSWeakCollection* obj) {
  if (obj->table()->IsHashTable()) {
    ObjectHashTable* table = ObjectHashTable::cast(obj->table());
    int used = table->NumberOfElements() * ObjectHashTable::kEntrySize;
    size_t overhead = table->Size() - used;
    stats->RecordFixedArraySubTypeStats(WEAK_COLLECTION_SUB_TYPE, table->Size(),
                                        overhead);
  }
}

void ObjectStatsCollector::RecordMapDetails(ObjectStats* stats, Heap* heap,
                                            HeapObject* obj) {
  Map* map_obj = Map::cast(obj);
  DCHECK(obj->map()->instance_type() == MAP_TYPE);
  DescriptorArray* array = map_obj->instance_descriptors();
  if (map_obj->owns_descriptors() && array != heap->empty_descriptor_array()) {
    int fixed_array_size = array->Size();
    stats->RecordFixedArraySubTypeStats(DESCRIPTOR_ARRAY_SUB_TYPE,
                                        fixed_array_size, 0);
    if (array->HasEnumCache()) {
      stats->RecordFixedArraySubTypeStats(ENUM_CACHE_SUB_TYPE,
                                          array->GetEnumCache()->Size(), 0);
    }
    if (array->HasEnumIndicesCache()) {
      stats->RecordFixedArraySubTypeStats(
          ENUM_INDICES_CACHE_SUB_TYPE, array->GetEnumIndicesCache()->Size(), 0);
    }
  }

  if (map_obj->has_code_cache()) {
    FixedArray* cache = map_obj->code_cache();
    stats->RecordFixedArraySubTypeStats(MAP_CODE_CACHE_SUB_TYPE, cache->Size(),
                                        0);
  }
}

void ObjectStatsCollector::RecordCodeDetails(ObjectStats* stats, Heap* heap,
                                             HeapObject* obj) {
  int object_size = obj->Size();
  DCHECK(obj->map()->instance_type() == CODE_TYPE);
  Code* code_obj = Code::cast(obj);
  stats->RecordCodeSubTypeStats(code_obj->kind(), code_obj->GetAge(),
                                object_size);
  Code* code = Code::cast(obj);
  if (code->deoptimization_data() != heap->empty_fixed_array()) {
    stats->RecordFixedArraySubTypeStats(DEOPTIMIZATION_DATA_SUB_TYPE,
                                        code->deoptimization_data()->Size(), 0);
  }
  FixedArrayBase* reloc_info =
      reinterpret_cast<FixedArrayBase*>(code->unchecked_relocation_info());
  if (reloc_info != heap->empty_fixed_array()) {
    stats->RecordFixedArraySubTypeStats(RELOC_INFO_SUB_TYPE,
                                        code->relocation_info()->Size(), 0);
  }
  FixedArrayBase* source_pos_table =
      reinterpret_cast<FixedArrayBase*>(code->source_position_table());
  if (source_pos_table != heap->empty_fixed_array()) {
    stats->RecordFixedArraySubTypeStats(SOURCE_POS_SUB_TYPE,
                                        source_pos_table->Size(), 0);
  }
}

void ObjectStatsCollector::RecordSharedFunctionInfoDetails(ObjectStats* stats,
                                                           Heap* heap,
                                                           HeapObject* obj) {
  SharedFunctionInfo* sfi = SharedFunctionInfo::cast(obj);
  if (sfi->scope_info() != heap->empty_fixed_array()) {
    stats->RecordFixedArraySubTypeStats(SCOPE_INFO_SUB_TYPE,
                                        sfi->scope_info()->Size(), 0);
  }
  if (sfi->feedback_metadata() != heap->empty_fixed_array()) {
    stats->RecordFixedArraySubTypeStats(TYPE_FEEDBACK_METADATA_SUB_TYPE,
                                        sfi->feedback_metadata()->Size(), 0);
  }
  if (!sfi->OptimizedCodeMapIsCleared()) {
    FixedArray* optimized_code_map = sfi->optimized_code_map();
    // Optimized code map should be small, so skip accounting.
    int len = optimized_code_map->length();
    for (int i = SharedFunctionInfo::kEntriesStart; i < len;
         i += SharedFunctionInfo::kEntryLength) {
      Object* slot =
          optimized_code_map->get(i + SharedFunctionInfo::kLiteralsOffset);
      LiteralsArray* literals = nullptr;
      if (slot->IsWeakCell()) {
        WeakCell* cell = WeakCell::cast(slot);
        if (!cell->cleared()) {
          literals = LiteralsArray::cast(cell->value());
        }
      } else {
        literals = LiteralsArray::cast(slot);
      }
      if (literals != nullptr) {
        stats->RecordFixedArraySubTypeStats(LITERALS_ARRAY_SUB_TYPE,
                                            literals->Size(), 0);
        TypeFeedbackVector* tfv = literals->feedback_vector();

        stats->RecordFixedArraySubTypeStats(TYPE_FEEDBACK_VECTOR_SUB_TYPE,
                                            tfv->Size(), 0);
      }
    }
  }
}

void ObjectStatsCollector::RecordFixedArrayDetails(ObjectStats* stats,
                                                   Heap* heap,
                                                   HeapObject* obj) {
  FixedArray* fixed_array = FixedArray::cast(obj);
  if (fixed_array == heap->string_table()) {
    stats->RecordFixedArraySubTypeStats(STRING_TABLE_SUB_TYPE,
                                        fixed_array->Size(), 0);
  }
  if (fixed_array == heap->weak_object_to_code_table()) {
    WeakHashTable* table = reinterpret_cast<WeakHashTable*>(fixed_array);
    int used = table->NumberOfElements() * WeakHashTable::kEntrySize;
    CHECK_GE(fixed_array->Size(), used);
    size_t overhead = fixed_array->Size() - used;
    stats->RecordFixedArraySubTypeStats(OBJECT_TO_CODE_SUB_TYPE,
                                        fixed_array->Size(), overhead);
  }
  if (obj->IsContext()) {
    stats->RecordFixedArraySubTypeStats(CONTEXT_SUB_TYPE, fixed_array->Size(),
                                        0);
  }
  if (fixed_array->map() == heap->fixed_cow_array_map()) {
    stats->RecordFixedArraySubTypeStats(COPY_ON_WRITE_SUB_TYPE,
                                        fixed_array->Size(), 0);
  }
}

}  // namespace internal
}  // namespace v8
