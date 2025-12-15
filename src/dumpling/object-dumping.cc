// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/dumpling/object-dumping.h"

#include <iostream>

#include "src/objects/instance-type.h"
#include "src/objects/objects-inl.h"
#include "src/objects/objects.h"
#include "src/objects/tagged.h"
#include "src/strings/string-stream.h"

namespace v8::internal {

void DifferentialFuzzingPrint(Tagged<Object> obj, std::ostream& os) {
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
    HeapObjectFuzzingPrint(heap_object, os);
  } else if (obj.GetHeapObjectIfStrong(&heap_object)) {
    HeapObjectFuzzingPrint(heap_object, os);
  } else {
    UNREACHABLE();
  }
}

void HeapObjectFuzzingPrint(Tagged<HeapObject> obj, std::ostream& os) {
  PtrComprCageBase cage_base = GetPtrComprCageBase();

  if (IsString(obj, cage_base)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    Cast<String>(obj)->StringShortPrint(&accumulator);
    os << accumulator.ToCString().get();
    return;
  }
  if (IsJSObject(obj, cage_base)) {
    HeapStringAllocator allocator;
    StringStream accumulator(&allocator);
    // TODO(mdanylo): implement JSObjectFuzzingPrint
    Cast<JSObject>(obj)->JSObjectShortPrint(&accumulator);
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
        // This is one of the meta maps, print only relevant fields.
        os << "<MetaMap (" << Brief(map->native_context_or_null()) << ")>";
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
      Tagged<AccessorInfo> info = Cast<AccessorInfo>(obj);
      os << "<AccessorInfo ";
      os << "name= " << Brief(info->name());
      os << ">";
      break;
    }
    default:
      // TODO(mdanylo): add more cases after a test run with Fuzzilli
      FATAL("Unexpected value in switch: %s\n",
            ToString(instance_type).c_str());
  }
}

}  // namespace v8::internal
