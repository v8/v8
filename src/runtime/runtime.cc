// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime.h"

#include <iomanip>

#include "src/assembler.h"
#include "src/contexts.h"
#include "src/handles-inl.h"
#include "src/heap/heap.h"
#include "src/isolate.h"
#include "src/runtime/runtime-utils.h"

namespace v8 {
namespace internal {

// Header of runtime functions.
#define F(name, number_of_args, result_size)                    \
  Object* Runtime_##name(int args_length, Object** args_object, \
                         Isolate* isolate);
FOR_EACH_INTRINSIC_RETURN_OBJECT(F)
#undef F

#define P(name, number_of_args, result_size)                       \
  ObjectPair Runtime_##name(int args_length, Object** args_object, \
                            Isolate* isolate);
FOR_EACH_INTRINSIC_RETURN_PAIR(P)
#undef P

#define T(name, number_of_args, result_size)                         \
  ObjectTriple Runtime_##name(int args_length, Object** args_object, \
                              Isolate* isolate);
FOR_EACH_INTRINSIC_RETURN_TRIPLE(T)
#undef T


#define F(name, number_of_args, result_size)                                  \
  {                                                                           \
    Runtime::k##name, Runtime::RUNTIME, #name, FUNCTION_ADDR(Runtime_##name), \
        number_of_args, result_size                                           \
  }                                                                           \
  ,


#define I(name, number_of_args, result_size)                       \
  {                                                                \
    Runtime::kInline##name, Runtime::INLINE, "_" #name,            \
        FUNCTION_ADDR(Runtime_##name), number_of_args, result_size \
  }                                                                \
  ,

static const Runtime::Function kIntrinsicFunctions[] = {
  FOR_EACH_INTRINSIC(F)
  FOR_EACH_INTRINSIC(I)
};

#undef I
#undef F


void Runtime::InitializeIntrinsicFunctionNames(Isolate* isolate,
                                               Handle<NameDictionary> dict) {
  DCHECK(dict->NumberOfElements() == 0);
  HandleScope scope(isolate);
  for (int i = 0; i < kNumFunctions; ++i) {
    const char* name = kIntrinsicFunctions[i].name;
    if (name == NULL) continue;
    Handle<NameDictionary> new_dict = NameDictionary::Add(
        dict, isolate->factory()->InternalizeUtf8String(name),
        Handle<Smi>(Smi::FromInt(i), isolate), PropertyDetails::Empty());
    // The dictionary does not need to grow.
    CHECK(new_dict.is_identical_to(dict));
  }
}


const Runtime::Function* Runtime::FunctionForName(Handle<String> name) {
  Heap* heap = name->GetHeap();
  int entry = heap->intrinsic_function_names()->FindEntry(name);
  if (entry != kNotFound) {
    Object* smi_index = heap->intrinsic_function_names()->ValueAt(entry);
    int function_index = Smi::cast(smi_index)->value();
    return &(kIntrinsicFunctions[function_index]);
  }
  return NULL;
}


const Runtime::Function* Runtime::FunctionForEntry(Address entry) {
  for (size_t i = 0; i < arraysize(kIntrinsicFunctions); ++i) {
    if (entry == kIntrinsicFunctions[i].entry) {
      return &(kIntrinsicFunctions[i]);
    }
  }
  return NULL;
}


const Runtime::Function* Runtime::FunctionForId(Runtime::FunctionId id) {
  return &(kIntrinsicFunctions[static_cast<int>(id)]);
}


const Runtime::Function* Runtime::RuntimeFunctionTable(Isolate* isolate) {
  if (isolate->external_reference_redirector()) {
    // When running with the simulator we need to provide a table which has
    // redirected runtime entry addresses.
    if (!isolate->runtime_state()->redirected_intrinsic_functions()) {
      size_t function_count = arraysize(kIntrinsicFunctions);
      Function* redirected_functions = new Function[function_count];
      memcpy(redirected_functions, kIntrinsicFunctions,
             sizeof(kIntrinsicFunctions));
      for (size_t i = 0; i < function_count; i++) {
        ExternalReference redirected_entry(static_cast<Runtime::FunctionId>(i),
                                           isolate);
        redirected_functions[i].entry = redirected_entry.address();
      }
      isolate->runtime_state()->set_redirected_intrinsic_functions(
          redirected_functions);
    }

    return isolate->runtime_state()->redirected_intrinsic_functions();
  } else {
    return kIntrinsicFunctions;
  }
}


std::ostream& operator<<(std::ostream& os, Runtime::FunctionId id) {
  return os << Runtime::FunctionForId(id)->name;
}


class RuntimeCallStatEntries {
 public:
  void Print(std::ostream& os) {
    if (total_call_count > 0) {
      std::sort(entries.rbegin(), entries.rend());
      os << "Runtime function                                  Time      "
            "Count"
         << std::endl
         << std::string(70, '=') << std::endl;
      for (Entry& entry : entries) {
        entry.Print(os);
      }
      os << std::string(60, '-') << std::endl;
      Entry("Total", total_time, total_call_count).Print(os);
    }
  }

  void Add(const char* name, base::TimeDelta time, uint32_t count) {
    entries.push_back(Entry(name, time, count));
    total_time += time;
    total_call_count += count;
  }

 private:
  class Entry {
   public:
    Entry(const char* name, base::TimeDelta time, uint64_t count)
        : name_(name), time_(time.InMilliseconds()), count_(count) {}

    bool operator<(const Entry& other) const {
      if (time_ < other.time_) return true;
      if (time_ > other.time_) return false;
      return count_ < other.count_;
    }

    void Print(std::ostream& os) {
      os << std::setw(50) << name_;
      os << std::setw(8) << time_ << "ms";
      os << std::setw(10) << count_ << std::endl;
    }

   private:
    const char* name_;
    int64_t time_;
    uint64_t count_;
  };

  uint64_t total_call_count = 0;
  base::TimeDelta total_time;
  std::vector<Entry> entries;
};


void RuntimeCallStats::Print(std::ostream& os) {
  RuntimeCallStatEntries entries;

#define PRINT_COUNTER(name, nargs, ressize)                                    \
  if (this->Count_Runtime_##name > 0) {                                        \
    entries.Add(#name, this->Time_Runtime_##name, this->Count_Runtime_##name); \
  }
  FOR_EACH_INTRINSIC(PRINT_COUNTER)
#undef PRINT_COUNTER
  entries.Print(os);
}


void RuntimeCallStats::Reset() {
#define RESET_COUNTER(name, nargs, ressize) \
  Count_Runtime_##name = 0;                 \
  Time_Runtime_##name = base::TimeDelta();
  FOR_EACH_INTRINSIC(RESET_COUNTER)
#undef RESET_COUNTER
}

}  // namespace internal
}  // namespace v8
