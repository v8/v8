// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/dumpling/object-dumping.h"

#include <algorithm>
#include <iostream>
#include <vector>

#include "src/objects/instance-type.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/tagged.h"
#include "src/strings/string-stream.h"

namespace v8::internal {

namespace {

std::string SanitizeString(const char* input) {
  std::string result;
  for (const char* c = input; *c != '\0'; ++c) {
    if (*c == '\n') {
      result += "\\n";
    } else if (*c == '\r') {
      result += "\\r";
    } else {
      result += *c;
    }
  }
  return result;
}

void JSObjectFuzzingPrintInternalIndexRange(Tagged<JSObject> obj,
                                            StringStream* accumulator,
                                            int depth, bool is_fast_object) {
  Isolate* isolate = Isolate::Current();

  HandleScope scope(isolate);
  Tagged<DescriptorArray> descriptors =
      obj->map()->instance_descriptors(isolate);

  std::optional<Tagged<NameDictionary>> dict;
  std::optional<ReadOnlyRoots> roots;

  std::vector<InternalIndex> indices;

  if (is_fast_object) {
    for (InternalIndex i : obj->map()->IterateOwnDescriptors()) {
      indices.push_back(i);
    }
  } else {
    CHECK(!IsJSGlobalObject(*obj));
    CHECK(!IsJSGlobalProxy(*obj));
    roots = GetReadOnlyRoots();
    dict = obj->property_dictionary();
    // We want to print out the properties in the same order they'd be in e.g.,
    // Object.getOwnPropertyDescriptors. We'd like to use IterationIndices here,
    // but cannot allocate. The code below does what IterationIndices would do,
    // just without allocating.
    for (InternalIndex i : dict.value()->IterateEntries()) {
      Tagged<Object> k;
      if (!dict.value()->ToKey(roots.value(), i, &k)) continue;
      indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(),
              [&](InternalIndex a, InternalIndex b) {
                return dict.value()->DetailsAt(a).dictionary_index() <
                       dict.value()->DetailsAt(b).dictionary_index();
              });
  }

  bool first_property = true;
  accumulator->Add("{");

  for (InternalIndex i : indices) {
    Handle<Name> key_name;

    PropertyDetails details = PropertyDetails::Empty();
    {
      DisallowGarbageCollection no_gc;
      Tagged<Name> key;

      if (is_fast_object) {
        key = descriptors->GetKey(i);
      } else {
        Tagged<Object> k;
        CHECK(dict.value()->ToKey(roots.value(), i, &k));
        key = Cast<Name>(k);
      }

      key_name = handle(key, isolate);

      if (is_fast_object) {
        details = descriptors->GetDetails(i);
      } else {
        details = dict.value()->DetailsAt(i);
      }
    }

    if (!first_property) {
      accumulator->Add(", ");
    }

    base::ScopedVector<char> name_buffer(100);
    key_name->NameShortPrint(name_buffer);
    accumulator->Add("%s", SanitizeString(name_buffer.begin()).c_str());

    std::stringstream attributes_stream;
    attributes_stream << details.attributes();
    accumulator->Add(attributes_stream.str().c_str());

    if (is_fast_object) {
      switch (details.location()) {
        case PropertyLocation::kField: {
          FieldIndex field_index = FieldIndex::ForDetails(obj->map(), details);
          accumulator->Add(DifferentialFuzzingPrint(
                               obj->RawFastPropertyAt(field_index), depth - 1)
                               .c_str());
          break;
        }
        case PropertyLocation::kDescriptor:
          accumulator->Add(DifferentialFuzzingPrint(
                               descriptors->GetStrongValue(i), depth - 1)
                               .c_str());
          break;
      }
    } else {
      accumulator->Add(
          DifferentialFuzzingPrint(dict.value()->ValueAt(i), depth - 1)
              .c_str());
    }

    first_property = false;
  }

  accumulator->Add("}");
}

void JSObjectFuzzingPrintFastProperties(Tagged<JSObject> obj,
                                        StringStream* accumulator, int depth) {
  JSObjectFuzzingPrintInternalIndexRange(obj, accumulator, depth, true);
}

void JSObjectFuzzingPrintDictProperties(Tagged<JSObject> obj,
                                        StringStream* accumulator, int depth) {
  CHECK(!IsJSGlobalProxy(obj));
  CHECK(!IsJSGlobalObject(obj));
  if (obj->property_dictionary()->Capacity() == 0) {
    accumulator->Add("{}");
  } else {
    JSObjectFuzzingPrintInternalIndexRange(obj, accumulator, depth, false);
  }
}

void JSObjectFuzzingPrintPrototype(Tagged<JSObject> obj,
                                   StringStream* accumulator, int depth) {
  Tagged<HeapObject> proto = Tagged<HeapObject>::cast(obj->map()->prototype());

  // This is to avoid printing Object.prototype
  if (proto->map()->instance_type() == JS_OBJECT_PROTOTYPE_TYPE) {
    return;
  }

  accumulator->Add("__proto__:");
  accumulator->Add(DifferentialFuzzingPrint(proto, depth - 1).c_str());
}

void JSObjectFuzzingPrintElements(Tagged<JSObject> obj,
                                  StringStream* accumulator, int depth) {
  Isolate* isolate = Isolate::Current();
  HandleScope scope(isolate);

  DCHECK(!AllowGarbageCollection::IsAllowed());

  ElementsKind elements_kind = obj->GetElementsKind();

  auto process_elements = [](auto& elements, Isolate* isolate,
                             StringStream* accumulator,
                             std::function<std::string(int)> format_element) {
    int dump_len = elements->length();

    // We have this var because we print elements for all JSObjects and most
    // often the array will be just empty or not empty but every element will be
    // a hole. This way empty/full of holes JSArray will be printed without [],
    // which might be confusing but still less confusing than JSObjects printed
    // with [].
    bool printed_open_bracket = false;
    int hole_range_start = -1;
    // We output consecutive holes as hole_range_start-hole_range_end:the_hole
    for (int i = 0; i < dump_len; i++) {
      if (elements->is_the_hole(isolate, i)) {
        if (hole_range_start == -1) {
          hole_range_start = i;
        }
      } else {
        if (!printed_open_bracket) {
          accumulator->Add("[");
          printed_open_bracket = true;
        }

        if (hole_range_start != -1) {
          accumulator->Add("%d-%d:the_hole,", hole_range_start, i - 1);
          hole_range_start = -1;
        }
        accumulator->Add("%s,", format_element(i).c_str());
      }
    }
    // We specifically not add trailing holes, because elements capacity can be
    // somewhat arbitrary.
    if (printed_open_bracket) {
      accumulator->Add("]");
    }
  };

  if (elements_kind == PACKED_SMI_ELEMENTS ||
      elements_kind == HOLEY_SMI_ELEMENTS || elements_kind == PACKED_ELEMENTS ||
      elements_kind == HOLEY_ELEMENTS) {
    Tagged<FixedArray> elements = Cast<FixedArray>(obj->elements());
    process_elements(elements, isolate, accumulator, [&](int i) {
      return DifferentialFuzzingPrint(elements->get(i), depth - 1);
    });
  } else if (elements_kind == PACKED_DOUBLE_ELEMENTS ||
             elements_kind == HOLEY_DOUBLE_ELEMENTS) {
    if (obj->elements() == ReadOnlyRoots(isolate).empty_fixed_array()) {
      return;
    }
    Tagged<FixedDoubleArray> elements = Cast<FixedDoubleArray>(obj->elements());
    process_elements(elements, isolate, accumulator, [&](int i) {
      return std::to_string(elements->get_scalar(i));
    });
  }
}

void JSObjectFuzzingPrint(Tagged<JSObject> obj, int depth,
                          StringStream* accumulator) {
  if (IsJSGlobalProxy(obj)) {
    accumulator->Add("<global object>");
    return;
  }
  CHECK(!IsJSGlobalObject(obj));

  if (IsJSFunction(obj)) {
    Tagged<JSFunction> function = Cast<JSFunction>(obj);
    std::unique_ptr<char[]> fun_name = function->shared()->DebugNameCStr();
    accumulator->Add("<JSFunction ");
    if (fun_name[0] != '\0') {
      accumulator->Add(fun_name.get());
    }
    accumulator->Put('>');
  } else if (IsJSArray(obj)) {
    accumulator->Add("<JSArray>");
  } else {
    Tagged<Map> map = obj->map();
    Tagged<Object> constructor = map->GetConstructor();
    bool printed = false;
    if (IsJSFunction(constructor)) {
      Tagged<SharedFunctionInfo> sfi = Cast<JSFunction>(constructor)->shared();
      Tagged<String> constructor_name = sfi->Name();
      if (constructor_name->length() > 0) {
        accumulator->Add("<");
        accumulator->Put(constructor_name);
        printed = true;
      }
    }
    if (!printed) {
      accumulator->Add("<JSObject");
    }
    accumulator->Put('>');
  }

  Isolate* isolate = Isolate::Current();

  if (depth > 0 && !IsUninitializedHole(obj, isolate)) {
    if (IsJSObject(obj)) {
      if (obj->HasFastProperties()) {
        JSObjectFuzzingPrintFastProperties(obj, accumulator, depth);
      } else {
        JSObjectFuzzingPrintDictProperties(obj, accumulator, depth);
      }
      JSObjectFuzzingPrintPrototype(obj, accumulator, depth);
      JSObjectFuzzingPrintElements(obj, accumulator, depth);
    }
  }
}

void HeapObjectFuzzingPrint(Tagged<HeapObject> obj, int depth,
                            std::ostream& os) {
  PtrComprCageBase cage_base = GetPtrComprCageBase();

  if (IsString(obj, cage_base)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    Cast<String>(obj)->StringShortPrint(&accumulator);
    os << SanitizeString(accumulator.ToCString().get());
    return;
  }
  if (IsJSObject(obj, cage_base)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    JSObjectFuzzingPrint(Cast<JSObject>(obj), depth, &accumulator);
    os << accumulator.ToCString().get();
    return;
  }

  InstanceType instance_type = obj->map(cage_base)->instance_type();

  // Skip invalid trusted objects. Technically it'd be fine to still handle
  // them below since we only print the objects, but such an object will
  // quickly lead to out-of-sandbox segfaults and so fuzzers will complain.
  if (InstanceTypeChecker::IsTrustedObject(instance_type) &&
      !OutsideSandboxOrInReadonlySpace(obj)) {
    os << "<Invalid TrustedObject (outside trusted space)>\n";
    return;
  }

  switch (instance_type) {
    case MAP_TYPE: {
      Tagged<Map> map = Cast<Map>(obj);
      if (map->instance_type() == MAP_TYPE) {
        os << "<MetaMap>";
      } else {
        os << "<Map";
        os << "(";
        if (IsJSObjectMap(map)) {
          os << ElementsKindToString(map->elements_kind());
        } else {
          os << map->instance_type();
        }
        os << ")>";
      }
    } break;
    case CATCH_CONTEXT_TYPE:
      os << "<CatchContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    case NATIVE_CONTEXT_TYPE:
      os << "<NativeContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    case WITH_CONTEXT_TYPE:
      os << "<WithContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    case FIXED_ARRAY_TYPE:
      os << "<FixedArray[" << Cast<FixedArray>(obj)->length() << "]>";
      break;
    case HOLE_TYPE: {
#define PRINT_HOLE(Type, Value, _) \
  if (Is##Type(obj)) {             \
    os << "<" #Value ">";          \
    break;                         \
  }
      HOLE_LIST(PRINT_HOLE)
#undef PRINT_HOLE
      UNREACHABLE();
    }
    case ODDBALL_TYPE: {
      if (IsUndefined(obj)) {
        os << "<undefined>";
      } else if (IsNull(obj)) {
        os << "<null>";
      } else if (IsTrue(obj)) {
        os << "<true>";
      } else if (IsFalse(obj)) {
        os << "<false>";
      } else {
        os << "<Odd Oddball: ";
        os << Cast<Oddball>(obj)->to_string()->ToCString().get();
        os << ">";
      }
      break;
    }
    case ACCESSOR_INFO_TYPE: {
      os << "<AccessorInfo>";
      break;
    }
    case ACCESSOR_PAIR_TYPE: {
      os << "<AccessorPair>";
      break;
    }
    case SCRIPT_CONTEXT_TYPE: {
      os << "<ScriptContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    }
    case JS_PROXY_TYPE: {
      os << "<JSProxy>";
      break;
    }
    case BIG_INT_BASE_TYPE: {
      os << "<BigIntBase ";
      Cast<BigIntBase>(obj)->BigIntBaseShortPrint(os);
      os << ">";
      break;
    }
    case FUNCTION_CONTEXT_TYPE: {
      os << "<FunctionContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    }
    case BLOCK_CONTEXT_TYPE: {
      os << "<BlockContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    }
    case EVAL_CONTEXT_TYPE: {
      os << "<EvalContext[" << Cast<Context>(obj)->length() << "]>";
      break;
    }
    case CLASS_POSITIONS_TYPE: {
      os << "<ClassPositions>";
      break;
    }
    case SYMBOL_TYPE: {
      Tagged<Symbol> symbol = Cast<Symbol>(obj);
      symbol->SymbolShortPrint(os);
      break;
    }
    case CLASS_BOILERPLATE_TYPE: {
      os << "<ClassBoilerplate>";
      break;
    }
    case SCRIPT_TYPE: {
      os << "<Script>";
      break;
    }
    case FEEDBACK_VECTOR_TYPE: {
      os << "<FeedbackVector[" << Cast<FeedbackVector>(obj)->length() << "]>";
      break;
    }
    default:
      // TODO(mdanylo): add more cases after a test run with Fuzzilli
      FATAL("Unexpected value in switch: %s\n",
            ToString(instance_type).c_str());
  }
}

}  // namespace

void DifferentialFuzzingPrint(Tagged<Object> obj, std::ostream& os) {
  os << DifferentialFuzzingPrint(obj, v8_flags.dumpling_depth);
}

std::string DifferentialFuzzingPrint(Tagged<Object> obj, int depth) {
  std::ostringstream os;

  Tagged<HeapObject> heap_object;

  DCHECK(!obj.IsCleared());

  if (!IsAnyHole(obj) && IsNumber(obj)) {
    static const int kBufferSize = 100;
    char chars[kBufferSize];
    base::Vector<char> buffer(chars, kBufferSize);
    if (IsSmi(obj)) {
      os << IntToStringView(obj.ToSmi().value(), buffer);
    } else {
      double number = Cast<HeapNumber>(obj)->value();
      os << DoubleToStringView(number, buffer);
    }
  } else if (obj.GetHeapObjectIfWeak(&heap_object)) {
    os << "[weak] ";
    HeapObjectFuzzingPrint(heap_object, depth, os);
  } else if (obj.GetHeapObjectIfStrong(&heap_object)) {
    HeapObjectFuzzingPrint(heap_object, depth, os);
  } else {
    UNREACHABLE();
  }

  return os.str();
}

}  // namespace v8::internal
