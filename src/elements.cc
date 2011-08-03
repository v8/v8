// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "objects.h"
#include "elements.h"

namespace v8 {
namespace internal {


ElementsAccessor** ElementsAccessor::elements_accessors_;


// Base class for element handler implementations. Contains the
// the common logic for objects with different ElementsKinds.
// Subclasses must specialize method for which the element
// implementation differs from the base class implementation.
//
// This class is intended to be used in the following way:
//
//   class SomeElementsAccessor :
//       public ElementsAccessorBase<SomeElementsAccessor,
//                                   BackingStoreClass> {
//     ...
//   }
//
// This is an example of the Curiously Recurring Template Pattern (see
// http://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).  We use
// CRTP to guarantee aggressive compile time optimizations (i.e.  inlining and
// specialization of SomeElementsAccessor methods).
template <typename ElementsAccessorSubclass, typename BackingStoreClass>
class ElementsAccessorBase : public ElementsAccessor {
 public:
  ElementsAccessorBase() { }
  virtual MaybeObject* GetWithReceiver(JSObject* obj,
                                       Object* receiver,
                                       uint32_t index) {
    if (index < ElementsAccessorSubclass::GetLength(obj)) {
      BackingStoreClass* backing_store =
          ElementsAccessorSubclass::GetBackingStore(obj);
      return backing_store->get(index);
    }
    return obj->GetHeap()->the_hole_value();
  }

 protected:
  static BackingStoreClass* GetBackingStore(JSObject* obj) {
    return BackingStoreClass::cast(obj->elements());
  }

  static uint32_t GetLength(JSObject* obj) {
    return ElementsAccessorSubclass::GetBackingStore(obj)->length();
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(ElementsAccessorBase);
};


class FastElementsAccessor
    : public ElementsAccessorBase<FastElementsAccessor, FixedArray> {
};


class FastDoubleElementsAccessor
    : public ElementsAccessorBase<FastDoubleElementsAccessor,
                                  FixedDoubleArray> {
};


// Super class for all external element arrays.
template<typename ExternalElementsAccessorSubclass,
         typename ExternalArray>
class ExternalElementsAccessor
    : public ElementsAccessorBase<ExternalElementsAccessorSubclass,
                                  ExternalArray> {
 public:
  virtual MaybeObject* GetWithReceiver(JSObject* obj,
                                       Object* receiver,
                                       uint32_t index) {
    if (index < ExternalElementsAccessorSubclass::GetLength(obj)) {
      ExternalArray* backing_store =
          ExternalElementsAccessorSubclass::GetBackingStore(obj);
      return backing_store->get(index);
    } else {
      return obj->GetHeap()->undefined_value();
    }
  }
};


class ExternalByteElementsAccessor
    : public ExternalElementsAccessor<ExternalByteElementsAccessor,
                                      ExternalByteArray> {
};


class ExternalUnsignedByteElementsAccessor
    : public ExternalElementsAccessor<ExternalUnsignedByteElementsAccessor,
                                      ExternalUnsignedByteArray> {
};


class ExternalShortElementsAccessor
    : public ExternalElementsAccessor<ExternalShortElementsAccessor,
                                      ExternalShortArray> {
};


class ExternalUnsignedShortElementsAccessor
    : public ExternalElementsAccessor<ExternalUnsignedShortElementsAccessor,
                                      ExternalUnsignedShortArray> {
};


class ExternalIntElementsAccessor
    : public ExternalElementsAccessor<ExternalIntElementsAccessor,
                                      ExternalIntArray> {
};


class ExternalUnsignedIntElementsAccessor
    : public ExternalElementsAccessor<ExternalUnsignedIntElementsAccessor,
                                      ExternalUnsignedIntArray> {
};


class ExternalFloatElementsAccessor
    : public ExternalElementsAccessor<ExternalFloatElementsAccessor,
                                      ExternalFloatArray> {
};


class ExternalDoubleElementsAccessor
    : public ExternalElementsAccessor<ExternalDoubleElementsAccessor,
                                      ExternalDoubleArray> {
};


class PixelElementsAccessor
    : public ExternalElementsAccessor<PixelElementsAccessor,
                                      ExternalPixelArray> {
};


class DictionaryElementsAccessor
    : public ElementsAccessorBase<DictionaryElementsAccessor,
                                  NumberDictionary> {
 public:
  static MaybeObject* GetNumberDictionaryElement(
      JSObject* obj,
      Object* receiver,
      NumberDictionary* backing_store,
      uint32_t index) {
    int entry = backing_store->FindEntry(index);
    if (entry != NumberDictionary::kNotFound) {
      Object* element = backing_store->ValueAt(entry);
      PropertyDetails details = backing_store->DetailsAt(entry);
      if (details.type() == CALLBACKS) {
        return obj->GetElementWithCallback(receiver,
                                           element,
                                           index,
                                           obj);
      } else {
        return element;
      }
    }
    return obj->GetHeap()->the_hole_value();
  }

  virtual MaybeObject* GetWithReceiver(JSObject* obj,
                                       Object* receiver,
                                       uint32_t index) {
    return GetNumberDictionaryElement(obj,
                                      receiver,
                                      obj->element_dictionary(),
                                      index);
  }
};


class NonStrictArgumentsElementsAccessor
    : public ElementsAccessorBase<NonStrictArgumentsElementsAccessor,
                                  FixedArray> {
 public:
  virtual MaybeObject* GetWithReceiver(JSObject* obj,
                                       Object* receiver,
                                       uint32_t index) {
    FixedArray* parameter_map = GetBackingStore(obj);
    uint32_t length = parameter_map->length();
    Object* probe =
        (index < length - 2) ? parameter_map->get(index + 2) : NULL;
    if (probe != NULL && !probe->IsTheHole()) {
      Context* context = Context::cast(parameter_map->get(0));
      int context_index = Smi::cast(probe)->value();
      ASSERT(!context->get(context_index)->IsTheHole());
      return context->get(context_index);
    } else {
      // Object is not mapped, defer to the arguments.
      FixedArray* arguments = FixedArray::cast(parameter_map->get(1));
      if (arguments->IsDictionary()) {
        return DictionaryElementsAccessor::GetNumberDictionaryElement(
            obj,
            receiver,
            NumberDictionary::cast(arguments),
            index);
      } else if (index < static_cast<uint32_t>(arguments->length())) {
        return arguments->get(index);
      }
    }
    return obj->GetHeap()->the_hole_value();
  }
};


void ElementsAccessor::InitializeOncePerProcess() {
  static struct ConcreteElementsAccessors {
    FastElementsAccessor fast_elements_handler;
    FastDoubleElementsAccessor fast_double_elements_handler;
    DictionaryElementsAccessor dictionary_elements_handler;
    NonStrictArgumentsElementsAccessor non_strict_arguments_elements_handler;
    ExternalByteElementsAccessor byte_elements_handler;
    ExternalUnsignedByteElementsAccessor unsigned_byte_elements_handler;
    ExternalShortElementsAccessor short_elements_handler;
    ExternalUnsignedShortElementsAccessor unsigned_short_elements_handler;
    ExternalIntElementsAccessor int_elements_handler;
    ExternalUnsignedIntElementsAccessor unsigned_int_elements_handler;
    ExternalFloatElementsAccessor float_elements_handler;
    ExternalDoubleElementsAccessor double_elements_handler;
    PixelElementsAccessor pixel_elements_handler;
  } element_accessors;

  static ElementsAccessor* accessor_array[] = {
    &element_accessors.fast_elements_handler,
    &element_accessors.fast_double_elements_handler,
    &element_accessors.dictionary_elements_handler,
    &element_accessors.non_strict_arguments_elements_handler,
    &element_accessors.byte_elements_handler,
    &element_accessors.unsigned_byte_elements_handler,
    &element_accessors.short_elements_handler,
    &element_accessors.unsigned_short_elements_handler,
    &element_accessors.int_elements_handler,
    &element_accessors.unsigned_int_elements_handler,
    &element_accessors.float_elements_handler,
    &element_accessors.double_elements_handler,
    &element_accessors.pixel_elements_handler
  };

  elements_accessors_ = accessor_array;
}


} }  // namespace v8::internal
