// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/object-stats.h"

#include "src/counters.h"
#include "src/heap/heap-inl.h"
#include "src/isolate.h"
#include "src/macro-assembler.h"
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
  visited_fixed_array_sub_types_.clear();
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
  if (obj->IsScript()) {
    RecordScriptDetails(stats, heap, Script::cast(obj));
  }
}

static bool CanRecordFixedArray(Heap* heap, FixedArrayBase* array) {
  return array->map()->instance_type() == FIXED_ARRAY_TYPE &&
         array->map() != heap->fixed_cow_array_map() &&
         array->map() != heap->fixed_double_array_map() &&
         array != heap->empty_fixed_array() &&
         array != heap->empty_byte_array() &&
         array != heap->empty_literals_array() &&
         array != heap->empty_sloppy_arguments_elements() &&
         array != heap->empty_slow_element_dictionary() &&
         array != heap->empty_descriptor_array() &&
         array != heap->empty_properties_dictionary();
}

static bool SameLiveness(HeapObject* obj1, HeapObject* obj2) {
  return ObjectMarking::Color(obj1) == ObjectMarking::Color(obj2);
}

void ObjectStatsCollector::RecordFixedArrayHelper(
    ObjectStats* stats, Heap* heap, HeapObject* parent, FixedArray* array,
    int subtype, size_t overhead) {
  if (SameLiveness(parent, array) && CanRecordFixedArray(heap, array)) {
    stats->RecordFixedArraySubTypeStats(array, subtype, array->Size(),
                                        overhead);
  }
}

void ObjectStatsCollector::RecordJSObjectDetails(ObjectStats* stats, Heap* heap,
                                                 JSObject* object) {
  DCHECK(object->IsJSObject());

  size_t overhead = 0;
  FixedArrayBase* elements = object->elements();
  if (CanRecordFixedArray(heap, elements)) {
    if (elements->IsDictionary() && SameLiveness(object, elements)) {
      SeededNumberDictionary* dict = SeededNumberDictionary::cast(elements);
      int used = dict->NumberOfElements() * SeededNumberDictionary::kEntrySize;
      CHECK_GE(elements->Size(), used);
      overhead = elements->Size() - used;
      stats->RecordFixedArraySubTypeStats(
          elements, DICTIONARY_ELEMENTS_SUB_TYPE, elements->Size(), overhead);
    } else {
      if (IsFastHoleyElementsKind(object->GetElementsKind())) {
        int used = object->GetFastElementsUsage() * kPointerSize;
        if (object->GetElementsKind() == FAST_HOLEY_DOUBLE_ELEMENTS) used *= 2;
        CHECK_GE(elements->Size(), used);
        overhead = elements->Size() - used;
      }
      stats->RecordFixedArraySubTypeStats(elements, FAST_ELEMENTS_SUB_TYPE,
                                          elements->Size(), overhead);
    }
  }

  overhead = 0;
  FixedArrayBase* properties = object->properties();
  if (CanRecordFixedArray(heap, properties) &&
      SameLiveness(object, properties)) {
    if (properties->IsDictionary()) {
      NameDictionary* dict = NameDictionary::cast(properties);
      int used = dict->NumberOfElements() * NameDictionary::kEntrySize;
      CHECK_GE(properties->Size(), used);
      overhead = properties->Size() - used;
      stats->RecordFixedArraySubTypeStats(properties,
                                          DICTIONARY_PROPERTIES_SUB_TYPE,
                                          properties->Size(), overhead);
    } else {
      stats->RecordFixedArraySubTypeStats(properties, FAST_PROPERTIES_SUB_TYPE,
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
    RecordFixedArrayHelper(stats, heap, obj, table, WEAK_COLLECTION_SUB_TYPE,
                           overhead);
  }
}

void ObjectStatsCollector::RecordScriptDetails(ObjectStats* stats, Heap* heap,
                                               Script* obj) {
  Object* infos = WeakFixedArray::cast(obj->shared_function_infos());
  if (infos->IsWeakFixedArray())
    RecordFixedArrayHelper(stats, heap, obj, WeakFixedArray::cast(infos),
                           SHARED_FUNCTION_INFOS_SUB_TYPE, 0);
}

void ObjectStatsCollector::RecordMapDetails(ObjectStats* stats, Heap* heap,
                                            HeapObject* obj) {
  Map* map_obj = Map::cast(obj);
  DCHECK(obj->map()->instance_type() == MAP_TYPE);
  DescriptorArray* array = map_obj->instance_descriptors();
  if (map_obj->owns_descriptors() && array != heap->empty_descriptor_array() &&
      SameLiveness(map_obj, array)) {
    RecordFixedArrayHelper(stats, heap, map_obj, array,
                           DESCRIPTOR_ARRAY_SUB_TYPE, 0);
    if (array->HasEnumCache()) {
      RecordFixedArrayHelper(stats, heap, array, array->GetEnumCache(),
                             ENUM_CACHE_SUB_TYPE, 0);
    }
    if (array->HasEnumIndicesCache()) {
      RecordFixedArrayHelper(stats, heap, array, array->GetEnumIndicesCache(),
                             ENUM_INDICES_CACHE_SUB_TYPE, 0);
    }
  }

  if (map_obj->has_code_cache()) {
    RecordFixedArrayHelper(stats, heap, map_obj, map_obj->code_cache(),
                           MAP_CODE_CACHE_SUB_TYPE, 0);
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
  RecordFixedArrayHelper(stats, heap, code, code->deoptimization_data(),
                         DEOPTIMIZATION_DATA_SUB_TYPE, 0);
  for (RelocIterator it(code); !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (mode == RelocInfo::EMBEDDED_OBJECT) {
      Object* target = it.rinfo()->target_object();
      if (target->IsFixedArray()) {
        RecordFixedArrayHelper(stats, heap, code, FixedArray::cast(target),
                               EMBEDDED_OBJECT_SUB_TYPE, 0);
      }
    }
  }
}

void ObjectStatsCollector::RecordSharedFunctionInfoDetails(ObjectStats* stats,
                                                           Heap* heap,
                                                           HeapObject* obj) {
  SharedFunctionInfo* sfi = SharedFunctionInfo::cast(obj);
  FixedArray* scope_info = sfi->scope_info();
  RecordFixedArrayHelper(stats, heap, sfi, scope_info, SCOPE_INFO_SUB_TYPE, 0);
  FixedArray* feedback_metadata = sfi->feedback_metadata();
  RecordFixedArrayHelper(stats, heap, sfi, feedback_metadata,
                         TYPE_FEEDBACK_METADATA_SUB_TYPE, 0);

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
        RecordFixedArrayHelper(stats, heap, sfi, literals,
                               LITERALS_ARRAY_SUB_TYPE, 0);
        RecordFixedArrayHelper(stats, heap, sfi, literals->feedback_vector(),
                               TYPE_FEEDBACK_VECTOR_SUB_TYPE, 0);
      }
    }
  }
}

void ObjectStatsCollector::RecordFixedArrayDetails(ObjectStats* stats,
                                                   Heap* heap,
                                                   HeapObject* obj) {
  FixedArray* array = FixedArray::cast(obj);

  // Special fixed arrays.
  int subtype = -1;
  if (array == heap->weak_new_space_object_to_code_list())
    subtype = WEAK_NEW_SPACE_OBJECT_TO_CODE_SUB_TYPE;
  if (array == heap->serialized_templates())
    subtype = SERIALIZED_TEMPLATES_SUB_TYPE;
  if (array == heap->string_table()) subtype = STRING_TABLE_SUB_TYPE;
  if (array == heap->number_string_cache())
    subtype = NUMBER_STRING_CACHE_SUB_TYPE;
  if (array == heap->single_character_string_cache())
    subtype = SINGLE_CHARACTER_STRING_CACHE_SUB_TYPE;
  if (array == heap->string_split_cache())
    subtype = STRING_SPLIT_CACHE_SUB_TYPE;
  if (array == heap->regexp_multiple_cache())
    subtype = REGEXP_MULTIPLE_CACHE_SUB_TYPE;
  if (array->IsContext()) subtype = CONTEXT_SUB_TYPE;
  if (array->map() == heap->fixed_cow_array_map())
    subtype = COPY_ON_WRITE_SUB_TYPE;
  if (subtype != -1) {
    stats->RecordFixedArraySubTypeStats(array, subtype, array->Size(), 0);
  }

  // Special hash maps.
  if (array == heap->weak_object_to_code_table()) {
    WeakHashTable* table = reinterpret_cast<WeakHashTable*>(array);
    int used = table->NumberOfElements() * WeakHashTable::kEntrySize;
    CHECK_GE(array->Size(), used);
    size_t overhead = array->Size() - used;
    stats->RecordFixedArraySubTypeStats(table, OBJECT_TO_CODE_SUB_TYPE,
                                        table->Size(), overhead);
  }
  if (array->IsNativeContext()) {
    Context* native_ctx = Context::cast(array);
    UnseededNumberDictionary* dict =
        native_ctx->template_instantiations_cache();
    int used = dict->NumberOfElements() * UnseededNumberDictionary::kEntrySize;
    size_t overhead = dict->Size() - used;
    RecordFixedArrayHelper(stats, heap, array, dict,
                           TEMPLATE_INSTANTIATIONS_CACHE_SUB_TYPE, overhead);
  }
  if (array == heap->code_stubs()) {
    UnseededNumberDictionary* dict = UnseededNumberDictionary::cast(array);
    int used = dict->NumberOfElements() * UnseededNumberDictionary::kEntrySize;
    size_t overhead = dict->Size() - used;
    stats->RecordFixedArraySubTypeStats(dict, CODE_STUBS_TABLE_SUB_TYPE,
                                        dict->Size(), overhead);
  }
  if (array == heap->intrinsic_function_names()) {
    NameDictionary* dict = NameDictionary::cast(array);
    int used = dict->NumberOfElements() * NameDictionary::kEntrySize;
    size_t overhead = dict->Size() - used;
    stats->RecordFixedArraySubTypeStats(dict, INTRINSIC_FUNCTION_NAMES_SUB_TYPE,
                                        dict->Size(), overhead);
  }
}

}  // namespace internal
}  // namespace v8
