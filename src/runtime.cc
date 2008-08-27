// Copyright 2006-2008 Google Inc. All Rights Reserved.
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

#include <stdlib.h>

#include "v8.h"

#include "accessors.h"
#include "api.h"
#include "arguments.h"
#include "compiler.h"
#include "cpu.h"
#include "dateparser.h"
#include "debug.h"
#include "execution.h"
#include "jsregexp.h"
#include "platform.h"
#include "runtime.h"
#include "scopeinfo.h"
#include "v8threads.h"

namespace v8 { namespace internal {


#define RUNTIME_ASSERT(value) do {                                   \
  if (!(value)) return IllegalOperation();                           \
} while (false)

// Cast the given object to a value of the specified type and store
// it in a variable with the given name.  If the object is not of the
// expected type call IllegalOperation and return.
#define CONVERT_CHECKED(Type, name, obj)                             \
  RUNTIME_ASSERT(obj->Is##Type());                                   \
  Type* name = Type::cast(obj);

#define CONVERT_ARG_CHECKED(Type, name, index)                       \
  RUNTIME_ASSERT(args[index]->Is##Type());                           \
  Handle<Type> name = args.at<Type>(index);

// Cast the given object to a boolean and store it in a variable with
// the given name.  If the object is not a boolean call IllegalOperation
// and return.
#define CONVERT_BOOLEAN_CHECKED(name, obj)                            \
  RUNTIME_ASSERT(obj->IsBoolean());                                   \
  bool name = (obj)->IsTrue();

// Cast the given object to a double and store it in a variable with
// the given name.  If the object is not a number (as opposed to
// the number not-a-number) call IllegalOperation and return.
#define CONVERT_DOUBLE_CHECKED(name, obj)                            \
  RUNTIME_ASSERT(obj->IsNumber());                                   \
  double name = (obj)->Number();

// Call the specified converter on the object *comand store the result in
// a variable of the specified type with the given name.  If the
// object is not a Number call IllegalOperation and return.
#define CONVERT_NUMBER_CHECKED(type, name, Type, obj)                \
  RUNTIME_ASSERT(obj->IsNumber());                                   \
  type name = NumberTo##Type(obj);

// Non-reentrant string buffer for efficient general use in this file.
static StaticResource<StringInputBuffer> string_input_buffer;


static Object* IllegalOperation() {
  return Top::Throw(Heap::illegal_access_symbol());
}


static Object* Runtime_CloneObjectLiteralBoilerplate(Arguments args) {
  CONVERT_CHECKED(JSObject, boilerplate, args[0]);
  return boilerplate->Copy();
}


static Object* Runtime_CreateObjectLiteralBoilerplate(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);
  // Copy the arguments.
  Handle<FixedArray> literals = args.at<FixedArray>(0);
  int literals_index = Smi::cast(args[1])->value();
  Handle<FixedArray> constant_properties = args.at<FixedArray>(2);

  // Get the object function from the literals array.  This is the
  // object function from the context in which the function was
  // created.  We do not use the object function from the current
  // global context because this might be the object function from
  // another context which we should not have access to.
  const int kObjectFunIndex = JSFunction::kLiteralObjectFunctionIndex;
  Handle<JSFunction> constructor =
      Handle<JSFunction>(JSFunction::cast(literals->get(kObjectFunIndex)));

  Handle<JSObject> boilerplate = Factory::NewJSObject(constructor, TENURED);

  {  // Add the constant propeties to the boilerplate.
    int length = constant_properties->length();
    OptimizedObjectForAddingMultipleProperties opt(boilerplate, true);
    for (int index = 0; index < length; index +=2) {
      Handle<Object> key(constant_properties->get(index+0));
      Handle<Object> value(constant_properties->get(index+1));
      uint32_t element_index = 0;
      if (key->IsSymbol()) {
        // If key is a symbol it is not an array element.
        Handle<String> name(String::cast(*key));
        ASSERT(!name->AsArrayIndex(&element_index));
        SetProperty(boilerplate, name, value, NONE);
      } else if (Array::IndexFromObject(*key, &element_index)) {
        // Array index (uint32).
        SetElement(boilerplate, element_index, value);
      } else {
        // Non-uint32 number.
        ASSERT(key->IsNumber());
        double num = key->Number();
        char arr[100];
        Vector<char> buffer(arr, ARRAY_SIZE(arr));
        const char* str = DoubleToCString(num, buffer);
        Handle<String> name = Factory::NewStringFromAscii(CStrVector(str));
        SetProperty(boilerplate, name, value, NONE);
      }
    }
  }

  // Update the functions literal and return the boilerplate.
  literals->set(literals_index, *boilerplate);

  return *boilerplate;
}


static Object* Runtime_CreateArrayLiteral(Arguments args) {
  // Takes a FixedArray of elements containing the literal elements of
  // the array literal and produces JSArray with those elements.
  // Additionally takes the literals array of the surrounding function
  // which contains the Array function to use for creating the array
  // literal.
  ASSERT(args.length() == 2);
  CONVERT_CHECKED(FixedArray, elements, args[0]);

#ifdef USE_OLD_CALLING_CONVENTIONS
  ASSERT(args[1]->IsTheHole());
  // TODO(1332579): Pass in the literals array from the function once
  // the new calling convention is in place on ARM.  Currently, we
  // retrieve the array constructor from the global context.  This is
  // a security problem since the global object might have been
  // reinitialized and the array constructor from the global context
  // might be from a context that we are not allowed to access.
  JSFunction* constructor =
      JSFunction::cast(Top::context()->global_context()->array_function());
#else
  CONVERT_CHECKED(FixedArray, literals, args[1]);
  const int kArrayFunIndex = JSFunction::kLiteralArrayFunctionIndex;
  JSFunction* constructor = JSFunction::cast(literals->get(kArrayFunIndex));
#endif

  // Create the JSArray.
  Object* object = Heap::AllocateJSObject(constructor);
  if (object->IsFailure()) return object;

  // Copy the elements.
  Object* content = elements->Copy();
  if (content->IsFailure()) return content;

  // Set the elements.
  JSArray::cast(object)->SetContent(FixedArray::cast(content));
  return object;
}


static Object* Runtime_ClassOf(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);
  Object* obj = args[0];
  if (!obj->IsJSObject()) return Heap::null_value();
  return JSObject::cast(obj)->class_name();
}


static Object* Runtime_IsInPrototypeChain(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);
  // See ECMA-262, section 15.3.5.3, page 88 (steps 5 - 8).
  Object* O = args[0];
  Object* V = args[1];
  while (true) {
    Object* prototype = V->GetPrototype();
    if (prototype->IsNull()) return Heap::false_value();
    if (O == prototype) return Heap::true_value();
    V = prototype;
  }
}


static Object* Runtime_IsConstructCall(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);
  JavaScriptFrameIterator it;
  return Heap::ToBoolean(it.frame()->IsConstructor());
}


static Object* Runtime_RegExpCompile(Arguments args) {
  HandleScope scope;  // create a new handle scope
  ASSERT(args.length() == 3);
  CONVERT_CHECKED(JSValue, raw_re, args[0]);
  Handle<JSValue> re(raw_re);
  CONVERT_CHECKED(String, raw_pattern, args[1]);
  Handle<String> pattern(raw_pattern);
  CONVERT_CHECKED(String, raw_flags, args[2]);
  Handle<String> flags(raw_flags);
  return *RegExpImpl::JsreCompile(re, pattern, flags);
}


static Object* Runtime_CreateApiFunction(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(FunctionTemplateInfo, raw_data, args[0]);
  Handle<FunctionTemplateInfo> data(raw_data);
  return *Factory::CreateApiFunction(data);
}


static Object* Runtime_IsTemplate(Arguments args) {
  ASSERT(args.length() == 1);
  Object* arg = args[0];
  bool result = arg->IsObjectTemplateInfo() || arg->IsFunctionTemplateInfo();
  return Heap::ToBoolean(result);
}


static Object* Runtime_GetTemplateField(Arguments args) {
  ASSERT(args.length() == 2);
  CONVERT_CHECKED(HeapObject, templ, args[0]);
  RUNTIME_ASSERT(templ->IsStruct());
  CONVERT_CHECKED(Smi, field, args[1]);
  return HeapObject::GetHeapObjectField(templ, field->value());
}


static Object* ThrowRedeclarationError(const char* type, Handle<String> name) {
  HandleScope scope;
  Handle<Object> type_handle = Factory::NewStringFromAscii(CStrVector(type));
  Handle<Object> args[2] = { type_handle, name };
  Handle<Object> error =
      Factory::NewTypeError("redeclaration", HandleVector(args, 2));
  return Top::Throw(*error);
}


static Object* Runtime_DeclareGlobals(Arguments args) {
  HandleScope scope;
  Handle<GlobalObject> global = Handle<GlobalObject>(Top::context()->global());

  CONVERT_ARG_CHECKED(FixedArray, pairs, 0);
  Handle<Context> context = args.at<Context>(1);
  bool is_eval = Smi::cast(args[2])->value() == 1;

  // Compute the property attributes. According to ECMA-262, section
  // 13, page 71, the property must be read-only and
  // non-deletable. However, neither SpiderMonkey nor KJS creates the
  // property as read-only, so we don't either.
  PropertyAttributes base = is_eval ? NONE : DONT_DELETE;

  // Only optimize the object if we intend to add more than 5 properties.
  OptimizedObjectForAddingMultipleProperties ba(global, pairs->length()/2 > 5);

  // Traverse the name/value pairs and set the properties.
  int length = pairs->length();
  for (int i = 0; i < length; i += 2) {
    HandleScope scope;
    Handle<String> name(String::cast(pairs->get(i)));
    Handle<Object> value(pairs->get(i + 1));

    // We have to declare a global const property. To capture we only
    // assign to it when evaluating the assignment for "const x =
    // <expr>" the initial value is the hole.
    bool is_const_property = value->IsTheHole();

    if (value->IsUndefined() || is_const_property) {
      // Lookup the property in the global object, and don't set the
      // value of the variable if the property is already there.
      LookupResult lookup;
      global->Lookup(*name, &lookup);
      if (lookup.IsProperty()) {
        // Determine if the property is local by comparing the holder
        // against the global object. The information will be used to
        // avoid throwing re-declaration errors when declaring
        // variables or constants that exist in the prototype chain.
        bool is_local = (*global == lookup.holder());
        // Get the property attributes and determine if the property is
        // read-only.
        PropertyAttributes attributes = global->GetPropertyAttribute(*name);
        bool is_read_only = (attributes & READ_ONLY) != 0;
        if (lookup.type() == INTERCEPTOR) {
          // If the interceptor says the property is there, we
          // just return undefined without overwriting the property.
          // Otherwise, we continue to setting the property.
          if (attributes != ABSENT) {
            // Check if the existing property conflicts with regards to const.
            if (is_local && (is_read_only || is_const_property)) {
              const char* type = (is_read_only) ? "const" : "var";
              return ThrowRedeclarationError(type, name);
            };
            // The property already exists without conflicting: Go to
            // the next declaration.
            continue;
          }
          // Fall-through and introduce the absent property by using
          // SetProperty.
        } else {
          if (is_local && (is_read_only || is_const_property)) {
            const char* type = (is_read_only) ? "const" : "var";
            return ThrowRedeclarationError(type, name);
          }
          // The property already exists without conflicting: Go to
          // the next declaration.
          continue;
        }
      }
    } else {
      // Copy the function and update its context. Use it as value.
      Handle<JSFunction> boilerplate = Handle<JSFunction>::cast(value);
      Handle<JSFunction> function =
          Factory::NewFunctionFromBoilerplate(boilerplate, context);
      value = function;
    }

    LookupResult lookup;
    global->LocalLookup(*name, &lookup);

    PropertyAttributes attributes = is_const_property
        ? static_cast<PropertyAttributes>(base | READ_ONLY)
        : base;

    if (lookup.IsProperty()) {
      // There's a local property that we need to overwrite because
      // we're either declaring a function or there's an interceptor
      // that claims the property is absent.

      // Check for conflicting re-declarations. We cannot have
      // conflicting types in case of intercepted properties because
      // they are absent.
      if (lookup.type() != INTERCEPTOR &&
          (lookup.IsReadOnly() || is_const_property)) {
        const char* type = (lookup.IsReadOnly()) ? "const" : "var";
        return ThrowRedeclarationError(type, name);
      }
      SetProperty(global, name, value, attributes);
    } else {
      // If a property with this name does not already exist on the
      // global object add the property locally.  We take special
      // precautions to always add it as a local property even in case
      // of callbacks in the prototype chain (this rules out using
      // SetProperty).  Also, we must use the handle-based version to
      // avoid GC issues.
      AddProperty(global, name, value, attributes);
    }
  }
  // Done.
  return Heap::undefined_value();
}


static Object* Runtime_DeclareContextSlot(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 5);

  // args[0] is result (TOS)
  CONVERT_ARG_CHECKED(Context, context, 1);
  Handle<String> name(String::cast(args[2]));
  PropertyAttributes mode =
      static_cast<PropertyAttributes>(Smi::cast(args[3])->value());
  ASSERT(mode == READ_ONLY || mode == NONE);
  Handle<Object> initial_value(args[4]);

  // Declarations are always done in the function context.
  context = Handle<Context>(context->fcontext());

  int index;
  PropertyAttributes attributes;
  ContextLookupFlags flags = DONT_FOLLOW_CHAINS;
  Handle<Object> context_obj =
      context->Lookup(name, flags, &index, &attributes);

  if (attributes != ABSENT) {
    // The name was declared before; check for conflicting
    // re-declarations: This is similar to the code in parser.cc in
    // the AstBuildingParser::Declare function.
    if (((attributes & READ_ONLY) != 0) || (mode == READ_ONLY)) {
      // Functions are not read-only.
      ASSERT(mode != READ_ONLY || initial_value->IsTheHole());
      const char* type = ((attributes & READ_ONLY) != 0) ? "const" : "var";
      return ThrowRedeclarationError(type, name);
    }

    // Initialize it if necessary.
    if (*initial_value != NULL) {
      if (index >= 0) {
        // The variable or constant context slot should always be in
        // the function context; not in any outer context nor in the
        // arguments object.
        ASSERT(context_obj.is_identical_to(context));
        if (((attributes & READ_ONLY) == 0) ||
            context->get(index)->IsTheHole()) {
          context->set(index, *initial_value);
        }
      } else {
        // Slow case: The property is not in the FixedArray part of the context.
        Handle<JSObject> context_ext = Handle<JSObject>::cast(context_obj);
        SetProperty(context_ext, name, initial_value, mode);
      }
    }
    return args[0];  // return TOS
  }

  // The property is not in the function context. It needs to be "declared"
  // in the function context's extension context, or in the global context.
  Handle<JSObject> context_ext;
  if (context->extension() != NULL) {
    // The function context's extension context exists - use it.
    context_ext = Handle<JSObject>(context->extension());
  } else {
    // The function context's extension context does not exists - allocate it.
    context_ext = Factory::NewJSObject(Top::context_extension_function());
    // And store it in the extension slot.
    context->set_extension(*context_ext);
  }
  ASSERT(*context_ext != NULL);

  // Declare the property by setting it to the initial value if provided,
  // or undefined, and use the correct mode (e.g. READ_ONLY attribute for
  // constant declarations).
  ASSERT(!context_ext->HasLocalProperty(*name));
  Handle<Object> value(Heap::undefined_value());
  if (*initial_value != NULL) value = initial_value;
  SetProperty(context_ext, name, value, mode);
  ASSERT(context_ext->GetLocalPropertyAttribute(*name) == mode);
  return args[0];  // return TOS
}


static Object* Runtime_InitializeVarGlobal(Arguments args) {
  NoHandleAllocation nha;

  // Determine if we need to assign to the variable if it already
  // exists (based on the number of arguments).
  RUNTIME_ASSERT(args.length() == 1 || args.length() == 2);
  bool assign = args.length() == 2;

  CONVERT_ARG_CHECKED(String, name, 0);
  GlobalObject* global = Top::context()->global();

  // According to ECMA-262, section 12.2, page 62, the property must
  // not be deletable.
  PropertyAttributes attributes = DONT_DELETE;

  // Lookup the property locally in the global object. If it isn't
  // there, we add the property and take special precautions to always
  // add it as a local property even in case of callbacks in the
  // prototype chain (this rules out using SetProperty).
  LookupResult lookup;
  global->LocalLookup(*name, &lookup);
  if (!lookup.IsProperty()) {
    Object* value = (assign) ? args[1] : Heap::undefined_value();
    return global->AddProperty(*name, value, attributes);
  }

  // Determine if this is a redeclaration of something read-only.
  if (lookup.IsReadOnly()) {
    return ThrowRedeclarationError("const", name);
  }

  // Determine if this is a redeclaration of an intercepted read-only
  // property and figure out if the property exists at all.
  bool found = true;
  PropertyType type = lookup.type();
  if (type == INTERCEPTOR) {
    PropertyAttributes intercepted = global->GetPropertyAttribute(*name);
    if (intercepted == ABSENT) {
      // The interceptor claims the property isn't there. We need to
      // make sure to introduce it.
      found = false;
    } else if ((intercepted & READ_ONLY) != 0) {
      // The property is present, but read-only. Since we're trying to
      // overwrite it with a variable declaration we must throw a
      // re-declaration error.
      return ThrowRedeclarationError("const", name);
    }
    // Restore global object from context (in case of GC).
    global = Top::context()->global();
  }

  if (found && !assign) {
    // The global property is there and we're not assigning any value
    // to it. Just return.
    return Heap::undefined_value();
  }

  // Assign the value (or undefined) to the property.
  Object* value = (assign) ? args[1] : Heap::undefined_value();
  return global->SetProperty(&lookup, *name, value, attributes);
}


static Object* Runtime_InitializeConstGlobal(Arguments args) {
  // All constants are declared with an initial value. The name
  // of the constant is the first argument and the initial value
  // is the second.
  RUNTIME_ASSERT(args.length() == 2);
  CONVERT_ARG_CHECKED(String, name, 0);
  Handle<Object> value = args.at<Object>(1);

  // Get the current global object from top.
  GlobalObject* global = Top::context()->global();

  // According to ECMA-262, section 12.2, page 62, the property must
  // not be deletable. Since it's a const, it must be READ_ONLY too.
  PropertyAttributes attributes =
      static_cast<PropertyAttributes>(DONT_DELETE | READ_ONLY);

  // Lookup the property locally in the global object. If it isn't
  // there, we add the property and take special precautions to always
  // add it as a local property even in case of callbacks in the
  // prototype chain (this rules out using SetProperty).
  LookupResult lookup;
  global->LocalLookup(*name, &lookup);
  if (!lookup.IsProperty()) {
    return global->AddProperty(*name, *value, attributes);
  }

  // Determine if this is a redeclaration of something not
  // read-only. In case the result is hidden behind an interceptor we
  // need to ask it for the property attributes.
  if (!lookup.IsReadOnly()) {
    if (lookup.type() != INTERCEPTOR) {
      return ThrowRedeclarationError("var", name);
    }

    PropertyAttributes intercepted = global->GetPropertyAttribute(*name);

    // Throw re-declaration error if the intercepted property is present
    // but not read-only.
    if (intercepted != ABSENT && (intercepted & READ_ONLY) == 0) {
      return ThrowRedeclarationError("var", name);
    }

    // Restore global object from context (in case of GC) and continue
    // with setting the value because the property is either absent or
    // read-only. We also have to do redo the lookup.
    global = Top::context()->global();

    // BUG 1213579: Handle the case where we have to set a read-only
    // property through an interceptor and only do it if it's
    // uninitialized, e.g. the hole. Nirk...
    global->SetProperty(*name, *value, attributes);
    return *value;
  }

  // Set the value, but only we're assigning the initial value to a
  // constant. For now, we determine this by checking if the
  // current value is the hole.
  PropertyType type = lookup.type();
  if (type == FIELD) {
    FixedArray* properties = global->properties();
    int index = lookup.GetFieldIndex();
    if (properties->get(index)->IsTheHole()) {
      properties->set(index, *value);
    }
  } else if (type == NORMAL) {
    Dictionary* dictionary = global->property_dictionary();
    int entry = lookup.GetDictionaryEntry();
    if (dictionary->ValueAt(entry)->IsTheHole()) {
      dictionary->ValueAtPut(entry, *value);
    }
  } else {
    // Ignore re-initialization of constants that have already been
    // assigned a function value.
    ASSERT(lookup.IsReadOnly() && type == CONSTANT_FUNCTION);
  }

  // Use the set value as the result of the operation.
  return *value;
}


static Object* Runtime_InitializeConstContextSlot(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);

  Handle<Object> value(args[0]);
  ASSERT(!value->IsTheHole());
  CONVERT_ARG_CHECKED(Context, context, 1);
  Handle<String> name(String::cast(args[2]));

  // Initializations are always done in the function context.
  context = Handle<Context>(context->fcontext());

  int index;
  PropertyAttributes attributes;
  ContextLookupFlags flags = DONT_FOLLOW_CHAINS;
  Handle<Object> context_obj =
      context->Lookup(name, flags, &index, &attributes);

  // The property should always be present. It is always declared
  // before being initialized through DeclareContextSlot.
  ASSERT(attributes != ABSENT && (attributes & READ_ONLY) != 0);

  // If the slot is in the context, we set it but only if it hasn't
  // been set before.
  if (index >= 0) {
    // The constant context slot should always be in the function
    // context; not in any outer context nor in the arguments object.
    ASSERT(context_obj.is_identical_to(context));
    if (context->get(index)->IsTheHole()) {
      context->set(index, *value);
    }
    return *value;
  }

  // Otherwise, the slot must be in a JS object extension.
  Handle<JSObject> context_ext(JSObject::cast(*context_obj));

  // We must initialize the value only if it wasn't initialized
  // before, e.g. for const declarations in a loop. The property has
  // the hole value if it wasn't initialized yet. NOTE: We cannot use
  // GetProperty() to get the current value as it 'unholes' the value.
  LookupResult lookup;
  context_ext->LocalLookupRealNamedProperty(*name, &lookup);
  ASSERT(lookup.IsProperty());  // the property was declared
  ASSERT(lookup.IsReadOnly());  // and it was declared as read-only

  PropertyType type = lookup.type();
  if (type == FIELD) {
    FixedArray* properties = context_ext->properties();
    int index = lookup.GetFieldIndex();
    if (properties->get(index)->IsTheHole()) {
      properties->set(index, *value);
    }
  } else if (type == NORMAL) {
    Dictionary* dictionary = context_ext->property_dictionary();
    int entry = lookup.GetDictionaryEntry();
    if (dictionary->ValueAt(entry)->IsTheHole()) {
      dictionary->ValueAtPut(entry, *value);
    }
  } else {
    // We should not reach here. Any real, named property should be
    // either a field or a dictionary slot.
    UNREACHABLE();
  }
  return *value;
}


static Object* Runtime_RegExpExec(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);
  CONVERT_CHECKED(JSValue, raw_regexp, args[0]);
  Handle<JSValue> regexp(raw_regexp);
  CONVERT_CHECKED(String, raw_subject, args[1]);
  Handle<String> subject(raw_subject);
  Handle<Object> index(args[2]);
  ASSERT(index->IsNumber());
  return *RegExpImpl::JsreExec(regexp, subject, index);
}


static Object* Runtime_RegExpExecGlobal(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);
  CONVERT_CHECKED(JSValue, raw_regexp, args[0]);
  Handle<JSValue> regexp(raw_regexp);
  CONVERT_CHECKED(String, raw_subject, args[1]);
  Handle<String> subject(raw_subject);
  return *RegExpImpl::JsreExecGlobal(regexp, subject);
}


static Object* Runtime_MaterializeRegExpLiteral(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 4);
  CONVERT_ARG_CHECKED(FixedArray, literals, 0);
  int index = Smi::cast(args[1])->value();
  Handle<String> pattern = args.at<String>(2);
  Handle<String> flags = args.at<String>(3);

  // Get the RegExp function from the literals array.  This is the
  // RegExp function from the context in which the function was
  // created.  We do not use the RegExp function from the current
  // global context because this might be the RegExp function from
  // another context which we should not have access to.
  const int kRegexpFunIndex = JSFunction::kLiteralRegExpFunctionIndex;
  Handle<JSFunction> constructor =
      Handle<JSFunction>(JSFunction::cast(literals->get(kRegexpFunIndex)));

  // Compute the regular expression literal.
  bool has_pending_exception;
  Handle<Object> regexp =
      RegExpImpl::CreateRegExpLiteral(constructor, pattern, flags,
                                      &has_pending_exception);
  if (has_pending_exception) {
    ASSERT(Top::has_pending_exception());
    return Failure::Exception();
  }
  literals->set(index, *regexp);
  return *regexp;
}


static Object* Runtime_FunctionGetName(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSFunction, f, args[0]);
  return f->shared()->name();
}


static Object* Runtime_FunctionGetScript(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSFunction, fun, args[0]);
  Handle<Object> script = Handle<Object>(fun->shared()->script());
  if (!script->IsScript()) return Heap::undefined_value();

  return *GetScriptWrapper(Handle<Script>::cast(script));
}


static Object* Runtime_FunctionGetSourceCode(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSFunction, f, args[0]);
  return f->shared()->GetSourceCode();
}


static Object* Runtime_FunctionGetScriptSourcePosition(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSFunction, fun, args[0]);
  int pos = fun->shared()->start_position();
  return Smi::FromInt(pos);
}


static Object* Runtime_FunctionSetInstanceClassName(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(JSFunction, fun, args[0]);
  CONVERT_CHECKED(String, name, args[1]);
  fun->SetInstanceClassName(name);
  return Heap::undefined_value();
}


static Object* Runtime_FunctionSetLength(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(JSFunction, fun, args[0]);
  CONVERT_CHECKED(Smi, length, args[1]);
  fun->shared()->set_length(length->value());
  return length;
}


static Object* Runtime_FunctionSetPrototype(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(JSFunction, fun, args[0]);
  Object* obj = Accessors::FunctionSetPrototype(fun, args[1], NULL);
  if (obj->IsFailure()) return obj;
  return args[0];  // return TOS
}


static Object* Runtime_SetCode(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(JSFunction, raw_target, args[0]);
  Handle<JSFunction> target(raw_target);
  Handle<Object> code = args.at<Object>(1);

  Handle<Context> context(target->context());

  if (!code->IsNull()) {
    RUNTIME_ASSERT(code->IsJSFunction());
    Handle<JSFunction> fun = Handle<JSFunction>::cast(code);
    SetExpectedNofProperties(target, fun->shared()->expected_nof_properties());
    if (!fun->is_compiled() && !CompileLazy(fun, KEEP_EXCEPTION)) {
      return Failure::Exception();
    }
    // Set the code, formal parameter count, and the length of the target
    // function.
    target->set_code(fun->code());
    target->shared()->set_length(fun->shared()->length());
    target->shared()->set_formal_parameter_count(
                          fun->shared()->formal_parameter_count());
    // Set the source code of the target function.
    target->shared()->set_script(fun->shared()->script());
    target->shared()->set_start_position(fun->shared()->start_position());
    target->shared()->set_end_position(fun->shared()->end_position());
    context = Handle<Context>(fun->context());

    // Make sure we get a fresh copy of the literal vector to avoid
    // cross context contamination.
    int number_of_literals = fun->NumberOfLiterals();
    Handle<FixedArray> literals =
        Factory::NewFixedArray(number_of_literals, TENURED);
    if (number_of_literals > 0) {
      // Insert the object, regexp and array functions in the literals
      // array prefix.  These are the functions that will be used when
      // creating object, regexp and array literals.
      literals->set(JSFunction::kLiteralObjectFunctionIndex,
                    context->global_context()->object_function());
      literals->set(JSFunction::kLiteralRegExpFunctionIndex,
                    context->global_context()->regexp_function());
      literals->set(JSFunction::kLiteralArrayFunctionIndex,
                    context->global_context()->array_function());
    }
    target->set_literals(*literals);
  }

  target->set_context(*context);
  return *target;
}


static Object* CharCodeAt(String* subject, Object* index) {
  uint32_t i = 0;
  if (!Array::IndexFromObject(index, &i)) return Heap::nan_value();
  // Flatten the string.  If someone wants to get a char at an index
  // in a cons string, it is likely that more indices will be
  // accessed.
  subject->TryFlatten();
  if (i >= static_cast<uint32_t>(subject->length())) return Heap::nan_value();
  return Smi::FromInt(subject->Get(i));
}


static Object* Runtime_StringCharCodeAt(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(String, subject, args[0]);
  Object* index = args[1];
  return CharCodeAt(subject, index);
}


static Object* Runtime_CharFromCode(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);
  uint32_t code;
  if (Array::IndexFromObject(args[0], &code)) {
    if (code <= 0xffff) {
      return Heap::LookupSingleCharacterStringFromCode(code);
    }
  }
  return Heap::empty_string();
}


static inline void ComputeKMPNextTable(String* pattern, int next_table[]) {
  int i = 0;
  int j = -1;
  next_table[0] = -1;

  Access<StringInputBuffer> buffer(&string_input_buffer);
  buffer->Reset(pattern);
  int length = pattern->length();
  uint16_t p = buffer->GetNext();
  while (i < length - 1) {
    while (j > -1 && p != pattern->Get(j)) {
      j = next_table[j];
    }
    i++;
    j++;
    p = buffer->GetNext();
    if (p == pattern->Get(j)) {
      next_table[i] = next_table[j];
    } else {
      next_table[i] = j;
    }
  }
}


static Object* Runtime_StringIndexOf(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 3);

  CONVERT_CHECKED(String, sub, args[0]);
  CONVERT_CHECKED(String, pat, args[1]);
  Object* index = args[2];

  int subject_length = sub->length();
  int pattern_length = pat->length();

  sub->TryFlatten();
  pat->TryFlatten();

  uint32_t start_index;
  if (!Array::IndexFromObject(index, &start_index)) return Smi::FromInt(-1);
  if (pattern_length == 0) return Smi::FromInt(start_index);

  // Searching for one specific character is common.  For one
  // character patterns the KMP algorithm is guaranteed to slow down
  // the search, so we just run through the subject string.
  if (pattern_length == 1) {
    uint16_t pattern_char = pat->Get(0);
    for (int i = start_index; i < subject_length; i++) {
      if (sub->Get(i) == pattern_char) {
        return Smi::FromInt(i);
      }
    }
    return Smi::FromInt(-1);
  }

  // For patterns with a length larger than one character we use the KMP
  // algorithm.
  //
  // Compute the 'next' table.
  int* next_table = NewArray<int>(pattern_length);
  ComputeKMPNextTable(pat, next_table);
  // Search using the 'next' table.
  int pattern_index = 0;
  // We would like to use StringInputBuffer here, but it does not have
  // the ability to start anywhere but the first character of a
  // string.  It would be nice to have efficient forward-seeking
  // support on StringInputBuffers.
  int subject_index = start_index;
  while (subject_index < subject_length) {
    uint16_t subject_char = sub->Get(subject_index);
    while (pattern_index > -1 && pat->Get(pattern_index) != subject_char) {
      pattern_index = next_table[pattern_index];
    }
    pattern_index++;
    subject_index++;
    if (pattern_index >= pattern_length) {
      DeleteArray(next_table);
      return Smi::FromInt(subject_index - pattern_index);
    }
  }
  DeleteArray(next_table);
  return Smi::FromInt(-1);
}


static Object* Runtime_StringLastIndexOf(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 3);

  CONVERT_CHECKED(String, sub, args[0]);
  CONVERT_CHECKED(String, pat, args[1]);
  Object* index = args[2];

  sub->TryFlatten();
  pat->TryFlatten();

  uint32_t start_index;
  if (!Array::IndexFromObject(index, &start_index)) return Smi::FromInt(-1);

  uint32_t pattern_length = pat->length();
  uint32_t sub_length = sub->length();

  if (start_index + pattern_length > sub_length) {
    start_index = sub_length - pattern_length;
  }

  for (int i = start_index; i >= 0; i--) {
    bool found = true;
    for (uint32_t j = 0; j < pattern_length; j++) {
      if (sub->Get(i + j) != pat->Get(j)) {
        found = false;
        break;
      }
    }
    if (found) return Smi::FromInt(i);
  }

  return Smi::FromInt(-1);
}


static Object* Runtime_StringLocaleCompare(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(String, str1, args[0]);
  CONVERT_CHECKED(String, str2, args[1]);

  if (str1 == str2) return Smi::FromInt(0);  // Equal.
  int str1_length = str1->length();
  int str2_length = str2->length();

  // Decide trivial cases without flattening.
  if (str1_length == 0) {
    if (str2_length == 0) return Smi::FromInt(0);  // Equal.
    return Smi::FromInt(-str2_length);
  } else {
    if (str2_length == 0) return Smi::FromInt(str1_length);
  }

  int end = str1_length < str2_length ? str1_length : str2_length;

  // No need to flatten if we are going to find the answer on the first
  // character.  At this point we know there is at least one character
  // in each string, due to the trivial case handling above.
  int d = str1->Get(0) - str2->Get(0);
  if (d != 0) return Smi::FromInt(d);

  str1->TryFlatten();
  str2->TryFlatten();

  static StringInputBuffer buf1;
  static StringInputBuffer buf2;

  buf1.Reset(str1);
  buf2.Reset(str2);

  for (int i = 0; i < end; i++) {
    uint16_t char1 = buf1.GetNext();
    uint16_t char2 = buf2.GetNext();
    if (char1 != char2) return Smi::FromInt(char1 - char2);
  }

  return Smi::FromInt(str1_length - str2_length);
}


static Object* Runtime_StringSlice(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 3);

  CONVERT_CHECKED(String, value, args[0]);
  CONVERT_DOUBLE_CHECKED(from_number, args[1]);
  CONVERT_DOUBLE_CHECKED(to_number, args[2]);

  int start = FastD2I(from_number);
  int end = FastD2I(to_number);

  RUNTIME_ASSERT(end >= start);
  RUNTIME_ASSERT(start >= 0);
  RUNTIME_ASSERT(end <= value->length());
  return value->Slice(start, end);
}


static Object* Runtime_NumberToRadixString(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(value, args[0]);
  if (isnan(value)) {
    return Heap::AllocateStringFromAscii(CStrVector("NaN"));
  }
  if (isinf(value)) {
    if (value < 0) {
      return Heap::AllocateStringFromAscii(CStrVector("-Infinity"));
    }
    return Heap::AllocateStringFromAscii(CStrVector("Infinity"));
  }
  CONVERT_DOUBLE_CHECKED(radix_number, args[1]);
  int radix = FastD2I(radix_number);
  RUNTIME_ASSERT(2 <= radix && radix <= 36);
  char* str = DoubleToRadixCString(value, radix);
  Object* result = Heap::AllocateStringFromAscii(CStrVector(str));
  DeleteArray(str);
  return result;
}


static Object* Runtime_NumberToFixed(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(value, args[0]);
  if (isnan(value)) {
    return Heap::AllocateStringFromAscii(CStrVector("NaN"));
  }
  if (isinf(value)) {
    if (value < 0) {
      return Heap::AllocateStringFromAscii(CStrVector("-Infinity"));
    }
    return Heap::AllocateStringFromAscii(CStrVector("Infinity"));
  }
  CONVERT_DOUBLE_CHECKED(f_number, args[1]);
  int f = FastD2I(f_number);
  RUNTIME_ASSERT(f >= 0);
  char* str = DoubleToFixedCString(value, f);
  Object* res = Heap::AllocateStringFromAscii(CStrVector(str));
  DeleteArray(str);
  return res;
}


static Object* Runtime_NumberToExponential(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(value, args[0]);
  if (isnan(value)) {
    return Heap::AllocateStringFromAscii(CStrVector("NaN"));
  }
  if (isinf(value)) {
    if (value < 0) {
      return Heap::AllocateStringFromAscii(CStrVector("-Infinity"));
    }
    return Heap::AllocateStringFromAscii(CStrVector("Infinity"));
  }
  CONVERT_DOUBLE_CHECKED(f_number, args[1]);
  int f = FastD2I(f_number);
  RUNTIME_ASSERT(f >= -1 && f <= 20);
  char* str = DoubleToExponentialCString(value, f);
  Object* res = Heap::AllocateStringFromAscii(CStrVector(str));
  DeleteArray(str);
  return res;
}


static Object* Runtime_NumberToPrecision(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(value, args[0]);
  if (isnan(value)) {
    return Heap::AllocateStringFromAscii(CStrVector("NaN"));
  }
  if (isinf(value)) {
    if (value < 0) {
      return Heap::AllocateStringFromAscii(CStrVector("-Infinity"));
    }
    return Heap::AllocateStringFromAscii(CStrVector("Infinity"));
  }
  CONVERT_DOUBLE_CHECKED(f_number, args[1]);
  int f = FastD2I(f_number);
  RUNTIME_ASSERT(f >= 1 && f <= 21);
  char* str = DoubleToPrecisionCString(value, f);
  Object* res = Heap::AllocateStringFromAscii(CStrVector(str));
  DeleteArray(str);
  return res;
}


// Returns a single character string where first character equals
// string->Get(index).
static Object* GetCharAt(String* string, uint32_t index) {
  if (index < static_cast<uint32_t>(string->length())) {
    string->TryFlatten();
    return Heap::LookupSingleCharacterStringFromCode(string->Get(index));
  }
  return *Execution::CharAt(Handle<String>(string), index);
}


Object* Runtime::GetElementOrCharAt(Handle<Object> object, uint32_t index) {
  // Handle [] indexing on Strings
  if (object->IsString()) {
    Object* result = GetCharAt(String::cast(*object), index);
    if (!result->IsUndefined()) return result;
  }

  // Handle [] indexing on String objects
  if (object->IsStringObjectWithCharacterAt(index)) {
    JSValue* js_value = JSValue::cast(*object);
    Object* result = GetCharAt(String::cast(js_value->value()), index);
    if (!result->IsUndefined()) return result;
  }

  if (object->IsString() || object->IsNumber() || object->IsBoolean()) {
    Object* prototype = object->GetPrototype();
    return prototype->GetElement(index);
  }

  return object->GetElement(index);
}


Object* Runtime::GetObjectProperty(Handle<Object> object, Object* key) {
  if (object->IsUndefined() || object->IsNull()) {
    HandleScope scope;
    Handle<Object> key_handle(key);
    Handle<Object> args[2] = { key_handle, object };
    Handle<Object> error =
        Factory::NewTypeError("non_object_property_load",
                              HandleVector(args, 2));
    return Top::Throw(*error);
  }

  // Check if the given key is an array index.
  uint32_t index;
  if (Array::IndexFromObject(key, &index)) {
    HandleScope scope;
    return GetElementOrCharAt(object, index);
  }

  // Convert the key to a string - possibly by calling back into JavaScript.
  String* name;
  if (key->IsString()) {
    name = String::cast(key);
  } else {
    HandleScope scope;
    bool has_pending_exception = false;
    Handle<Object> converted =
        Execution::ToString(Handle<Object>(key), &has_pending_exception);
    if (has_pending_exception) return Failure::Exception();
    name = String::cast(*converted);
  }

  // Check if the name is trivially convertable to an index and get
  // the element if so.
  if (name->AsArrayIndex(&index)) {
    HandleScope scope;
    return GetElementOrCharAt(object, index);
  } else {
    PropertyAttributes attr;
    return object->GetProperty(name, &attr);
  }
}


static Object* Runtime_GetProperty(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  Handle<Object> object = args.at<Object>(0);
  Object* key = args[1];

  return Runtime::GetObjectProperty(object, key);
}


Object* Runtime::SetObjectProperty(Handle<Object> object,
                                   Handle<Object> key,
                                   Handle<Object> value,
                                   PropertyAttributes attr) {
  if (object->IsUndefined() || object->IsNull()) {
    HandleScope scope;
    Handle<Object> obj(object);
    Handle<Object> args[2] = { key, obj };
    Handle<Object> error =
        Factory::NewTypeError("non_object_property_store",
                              HandleVector(args, 2));
    return Top::Throw(*error);
  }

  // If the object isn't a JavaScript object, we ignore the store.
  if (!object->IsJSObject()) return *value;

  // Check if the given key is an array index.
  uint32_t index;
  if (Array::IndexFromObject(*key, &index)) {
    ASSERT(attr == NONE);

    // In Firefox/SpiderMonkey, Safari and Opera you can access the characters
    // of a string using [] notation.  We need to support this too in
    // JavaScript.
    // In the case of a String object we just need to redirect the assignment to
    // the underlying string if the index is in range.  Since the underlying
    // string does nothing with the assignment then we can ignore such
    // assignments.
    if (object->IsStringObjectWithCharacterAt(index))
      return *value;

    Object* result = JSObject::cast(*object)->SetElement(index, *value);
    if (result->IsFailure()) return result;
    return *value;
  }

  if (key->IsString()) {
    Object* result;
    if (String::cast(*key)->AsArrayIndex(&index)) {
      ASSERT(attr == NONE);
      result = JSObject::cast(*object)->SetElement(index, *value);
    } else {
      String::cast(*key)->TryFlatten();
      result =
          JSObject::cast(*object)->SetProperty(String::cast(*key), *value,
                                               attr);
    }
    if (result->IsFailure()) return result;
    return *value;
  }

  HandleScope scope;

  // Handlify object and value before calling into JavaScript again.
  Handle<JSObject> object_handle = Handle<JSObject>::cast(object);
  Handle<Object> value_handle = value;

  // Call-back into JavaScript to convert the key to a string.
  bool has_pending_exception = false;
  Handle<Object> converted = Execution::ToString(key, &has_pending_exception);
  if (has_pending_exception) return Failure::Exception();
  Handle<String> name = Handle<String>::cast(converted);

  if (name->AsArrayIndex(&index)) {
    ASSERT(attr == NONE);
    return object_handle->SetElement(index, *value_handle);
  } else {
    return object_handle->SetProperty(*name, *value_handle, attr);
  }
}


static Object* Runtime_AddProperty(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 4);

  CONVERT_CHECKED(JSObject, object, args[0]);
  CONVERT_CHECKED(String, name, args[1]);
  RUNTIME_ASSERT(!object->HasLocalProperty(name));
  CONVERT_CHECKED(Smi, attr_obj, args[3]);

  int attr = attr_obj->value();
  RUNTIME_ASSERT((attr & ~(READ_ONLY | DONT_ENUM | DONT_DELETE)) == 0);
  PropertyAttributes attributes = static_cast<PropertyAttributes>(attr);

  return object->AddProperty(name, args[2], attributes);
}


static Object* Runtime_SetProperty(Arguments args) {
  NoHandleAllocation ha;
  RUNTIME_ASSERT(args.length() == 3 || args.length() == 4);

  Handle<Object> object = args.at<Object>(0);
  Handle<Object> key = args.at<Object>(1);
  Handle<Object> value = args.at<Object>(2);

  // Compute attributes.
  PropertyAttributes attributes = NONE;
  if (args.length() == 4) {
    CONVERT_CHECKED(Smi, value_obj, args[3]);
    int value = value_obj->value();
    // Only attribute bits should be set.
    ASSERT((value & ~(READ_ONLY | DONT_ENUM | DONT_DELETE)) == 0);
    attributes = static_cast<PropertyAttributes>(value);
  }
  return Runtime::SetObjectProperty(object, key, value, attributes);
}


// Set a local property, even if it is READ_ONLY.  If the property does not
// exist, it will be added with attributes NONE.
static Object* Runtime_IgnoreAttributesAndSetProperty(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 3);

  CONVERT_CHECKED(JSObject, object, args[0]);
  CONVERT_CHECKED(String, name, args[1]);

  return object->IgnoreAttributesAndSetLocalProperty(name, args[2]);
}


static Object* Runtime_DeleteProperty(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(JSObject, object, args[0]);
  CONVERT_CHECKED(String, key, args[1]);
  return object->DeleteProperty(key);
}


static Object* Runtime_HasLocalProperty(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);
  CONVERT_CHECKED(String, key, args[1]);

  // Only JS objects can have properties.
  if (args[0]->IsJSObject()) {
    JSObject* object = JSObject::cast(args[0]);
    if (object->HasLocalProperty(key)) return Heap::true_value();
  } else if (args[0]->IsString()) {
    // Well, there is one exception:  Handle [] on strings.
    uint32_t index;
    if (key->AsArrayIndex(&index)) {
      String* string = String::cast(args[0]);
      if (index < static_cast<uint32_t>(string->length()))
        return Heap::true_value();
    }
  }
  return Heap::false_value();
}


static Object* Runtime_HasProperty(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 2);

  // Only JS objects can have properties.
  if (args[0]->IsJSObject()) {
    JSObject* object = JSObject::cast(args[0]);
    CONVERT_CHECKED(String, key, args[1]);
    if (object->HasProperty(key)) return Heap::true_value();
  }
  return Heap::false_value();
}


static Object* Runtime_HasElement(Arguments args) {
  NoHandleAllocation na;
  ASSERT(args.length() == 2);

  // Only JS objects can have elements.
  if (args[0]->IsJSObject()) {
    JSObject* object = JSObject::cast(args[0]);
    CONVERT_CHECKED(Smi, index_obj, args[1]);
    uint32_t index = index_obj->value();
    if (object->HasElement(index)) return Heap::true_value();
  }
  return Heap::false_value();
}


static Object* Runtime_IsPropertyEnumerable(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(JSObject, object, args[0]);
  CONVERT_CHECKED(String, key, args[1]);

  uint32_t index;
  if (key->AsArrayIndex(&index)) {
    return Heap::ToBoolean(object->HasElement(index));
  }

  LookupResult result;
  object->LocalLookup(key, &result);
  if (!result.IsProperty()) return Heap::false_value();
  return Heap::ToBoolean(!result.IsDontEnum());
}


static Object* Runtime_GetPropertyNames(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSObject, raw_object, args[0]);
  Handle<JSObject> object(raw_object);
  return *GetKeysFor(object);
}


// Returns either a FixedArray as Runtime_GetPropertyNames,
// or, if the given object has an enum cache that contains
// all enumerable properties of the object and its prototypes
// have none, the map of the object. This is used to speed up
// the check for deletions during a for-in.
static Object* Runtime_GetPropertyNamesFast(Arguments args) {
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSObject, raw_object, args[0]);

  if (raw_object->IsSimpleEnum()) return raw_object->map();

  HandleScope scope;
  Handle<JSObject> object(raw_object);
  Handle<FixedArray> content = GetKeysInFixedArrayFor(object);

  // Test again, since cache may have been built by preceding call.
  if (object->IsSimpleEnum()) return object->map();

  return *content;
}


static Object* Runtime_GetArgumentsProperty(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  // Compute the frame holding the arguments.
  JavaScriptFrameIterator it;
  it.AdvanceToArgumentsFrame();
  JavaScriptFrame* frame = it.frame();

  // Get the actual number of provided arguments.
  const uint32_t n = frame->GetProvidedParametersCount();

  // Try to convert the key to an index. If successful and within
  // index return the the argument from the frame.
  uint32_t index;
  if (Array::IndexFromObject(args[0], &index) && index < n) {
    return frame->GetParameter(index);
  }

  // Convert the key to a string.
  HandleScope scope;
  bool exception = false;
  Handle<Object> converted =
      Execution::ToString(args.at<Object>(0), &exception);
  if (exception) return Failure::Exception();
  Handle<String> key = Handle<String>::cast(converted);

  // Try to convert the string key into an array index.
  if (key->AsArrayIndex(&index)) {
    if (index < n) {
      return frame->GetParameter(index);
    } else {
      return Top::initial_object_prototype()->GetElement(index);
    }
  }

  // Handle special arguments properties.
  if (key->Equals(Heap::length_symbol())) return Smi::FromInt(n);
  if (key->Equals(Heap::callee_symbol())) return frame->function();

  // Lookup in the initial Object.prototype object.
  return Top::initial_object_prototype()->GetProperty(*key);
}


static Object* Runtime_ToBool(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  return args[0]->ToBoolean();
}


// Returns the type string of a value; see ECMA-262, 11.4.3 (p 47).
// Possible optimizations: put the type string into the oddballs.
static Object* Runtime_Typeof(Arguments args) {
  NoHandleAllocation ha;

  Object* obj = args[0];
  if (obj->IsNumber()) return Heap::number_symbol();
  HeapObject* heap_obj = HeapObject::cast(obj);

  // typeof an undetectable object is 'undefined'
  if (heap_obj->map()->is_undetectable()) return Heap::undefined_symbol();

  InstanceType instance_type = heap_obj->map()->instance_type();
  if (instance_type < FIRST_NONSTRING_TYPE) {
    return Heap::string_symbol();
  }

  switch (instance_type) {
    case ODDBALL_TYPE:
      if (heap_obj->IsTrue() || heap_obj->IsFalse()) {
        return Heap::boolean_symbol();
      }
      if (heap_obj->IsNull()) {
        return Heap::object_symbol();
      }
      ASSERT(heap_obj->IsUndefined());
      return Heap::undefined_symbol();
    case JS_FUNCTION_TYPE:
      return Heap::function_symbol();
    default:
      // For any kind of object not handled above, the spec rule for
      // host objects gives that it is okay to return "object"
      return Heap::object_symbol();
  }
}


static Object* Runtime_StringToNumber(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(String, subject, args[0]);
  subject->TryFlatten();
  return Heap::NumberFromDouble(StringToDouble(subject, ALLOW_HEX));
}


static Object* Runtime_StringFromCharCodeArray(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSArray, codes, args[0]);
  int length = Smi::cast(codes->length())->value();

  // Check if the string can be ASCII.
  int i;
  for (i = 0; i < length; i++) {
    Object* element = codes->GetElement(i);
    CONVERT_NUMBER_CHECKED(int, chr, Int32, element);
    if ((chr & 0xffff) > String::kMaxAsciiCharCode)
      break;
  }

  Object* object = NULL;
  if (i == length) {  // The string is ASCII.
    object = Heap::AllocateRawAsciiString(length);
  } else {  // The string is not ASCII.
    object = Heap::AllocateRawTwoByteString(length);
  }

  if (object->IsFailure()) return object;
  String* result = String::cast(object);
  for (int i = 0; i < length; i++) {
    Object* element = codes->GetElement(i);
    CONVERT_NUMBER_CHECKED(int, chr, Int32, element);
    result->Set(i, chr & 0xffff);
  }
  return result;
}


// kNotEscaped is generated by the following:
//
// #!/bin/perl
// for (my $i = 0; $i < 256; $i++) {
//   print "\n" if $i % 16 == 0;
//   my $c = chr($i);
//   my $escaped = 1;
//   $escaped = 0 if $c =~ m#[A-Za-z0-9@*_+./-]#;
//   print $escaped ? "0, " : "1, ";
// }


static bool IsNotEscaped(uint16_t character) {
  // Only for 8 bit characters, the rest are always escaped (in a different way)
  ASSERT(character < 256);
  static const char kNotEscaped[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  };
  return kNotEscaped[character] != 0;
}


static Object* Runtime_URIEscape(Arguments args) {
  const char hex_chars[] = "0123456789ABCDEF";
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(String, source, args[0]);

  source->TryFlatten();

  int escaped_length = 0;
  int length = source->length();
  {
    Access<StringInputBuffer> buffer(&string_input_buffer);
    buffer->Reset(source);
    while (buffer->has_more()) {
      uint16_t character = buffer->GetNext();
      if (character >= 256) {
        escaped_length += 6;
      } else if (IsNotEscaped(character)) {
        escaped_length++;
      } else {
        escaped_length += 3;
      }
      // We don't allow strings that are longer than Smi range.
      if (!Smi::IsValid(escaped_length)) {
        Top::context()->mark_out_of_memory();
        return Failure::OutOfMemoryException();
      }
    }
  }
  // No length change implies no change.  Return original string if no change.
  if (escaped_length == length) {
    return source;
  }
  Object* o = Heap::AllocateRawAsciiString(escaped_length);
  if (o->IsFailure()) return o;
  String* destination = String::cast(o);
  int dest_position = 0;

  Access<StringInputBuffer> buffer(&string_input_buffer);
  buffer->Rewind();
  while (buffer->has_more()) {
    uint16_t character = buffer->GetNext();
    if (character >= 256) {
      destination->Set(dest_position, '%');
      destination->Set(dest_position+1, 'u');
      destination->Set(dest_position+2, hex_chars[character >> 12]);
      destination->Set(dest_position+3, hex_chars[(character >> 8) & 0xf]);
      destination->Set(dest_position+4, hex_chars[(character >> 4) & 0xf]);
      destination->Set(dest_position+5, hex_chars[character & 0xf]);
      dest_position += 6;
    } else if (IsNotEscaped(character)) {
      destination->Set(dest_position, character);
      dest_position++;
    } else {
      destination->Set(dest_position, '%');
      destination->Set(dest_position+1, hex_chars[character >> 4]);
      destination->Set(dest_position+2, hex_chars[character & 0xf]);
      dest_position += 3;
    }
  }
  return destination;
}


static inline int TwoDigitHex(uint16_t character1, uint16_t character2) {
  static const signed char kHexValue['g'] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    0,  1,  2,   3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, 10, 11, 12, 13, 14, 15 };

  if (character1 > 'f') return -1;
  int hi = kHexValue[character1];
  if (hi == -1) return -1;
  if (character2 > 'f') return -1;
  int lo = kHexValue[character2];
  if (lo == -1) return -1;
  return (hi << 4) + lo;
}


static inline int Unescape(String* source, int i, int length, int* step) {
  uint16_t character = source->Get(i);
  int32_t hi, lo;
  if (character == '%' &&
      i <= length - 6 &&
      source->Get(i + 1) == 'u' &&
      (hi = TwoDigitHex(source->Get(i + 2), source->Get(i + 3))) != -1 &&
      (lo = TwoDigitHex(source->Get(i + 4), source->Get(i + 5))) != -1) {
    *step = 6;
    return (hi << 8) + lo;
  } else if (character == '%' &&
      i <= length - 3 &&
      (lo = TwoDigitHex(source->Get(i + 1), source->Get(i + 2))) != -1) {
    *step = 3;
    return lo;
  } else {
    *step = 1;
    return character;
  }
}


static Object* Runtime_URIUnescape(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(String, source, args[0]);

  source->TryFlatten();

  bool ascii = true;
  int length = source->length();

  int unescaped_length = 0;
  for (int i = 0; i < length; unescaped_length++) {
    int step;
    if (Unescape(source, i, length, &step) > String::kMaxAsciiCharCode)
      ascii = false;
    i += step;
  }

  // No length change implies no change.  Return original string if no change.
  if (unescaped_length == length)
    return source;

  Object* o = ascii ?
              Heap::AllocateRawAsciiString(unescaped_length) :
              Heap::AllocateRawTwoByteString(unescaped_length);
  if (o->IsFailure()) return o;
  String* destination = String::cast(o);

  int dest_position = 0;
  for (int i = 0; i < length; dest_position++) {
    int step;
    destination->Set(dest_position, Unescape(source, i, length, &step));
    i += step;
  }
  return destination;
}


static Object* Runtime_StringParseInt(Arguments args) {
  NoHandleAllocation ha;

  CONVERT_CHECKED(String, s, args[0]);
  CONVERT_DOUBLE_CHECKED(n, args[1]);
  int radix = FastD2I(n);

  s->TryFlatten();

  int len = s->length();
  int i;

  // Skip leading white space.
  for (i = 0; i < len && Scanner::kIsWhiteSpace.get(s->Get(i)); i++) ;
  if (i == len) return Heap::nan_value();

  // Compute the sign (default to +).
  int sign = 1;
  if (s->Get(i) == '-') {
    sign = -1;
    i++;
  } else if (s->Get(i) == '+') {
    i++;
  }

  // Compute the radix if 0.
  if (radix == 0) {
    radix = 10;
    if (i < len && s->Get(i) == '0') {
      radix = 8;
      if (i + 1 < len) {
        int c = s->Get(i + 1);
        if (c == 'x' || c == 'X') {
          radix = 16;
          i += 2;
        }
      }
    }
  } else if (radix == 16) {
    // Allow 0x or 0X prefix if radix is 16.
    if (i + 1 < len && s->Get(i) == '0') {
      int c = s->Get(i + 1);
      if (c == 'x' || c == 'X') i += 2;
    }
  }

  RUNTIME_ASSERT(2 <= radix && radix <= 36);
  double value;
  int end_index = StringToInt(s, i, radix, &value);
  if (end_index != i) {
    return Heap::NumberFromDouble(sign * value);
  }
  return Heap::nan_value();
}


static Object* Runtime_StringParseFloat(Arguments args) {
  NoHandleAllocation ha;
  CONVERT_CHECKED(String, str, args[0]);

  // ECMA-262 section 15.1.2.3, empty string is NaN
  double value = StringToDouble(str, ALLOW_TRAILING_JUNK, OS::nan_value());

  // Create a number object from the value.
  return Heap::NumberFromDouble(value);
}


static unibrow::Mapping<unibrow::ToUppercase, 128> to_upper_mapping;
static unibrow::Mapping<unibrow::ToLowercase, 128> to_lower_mapping;


template <class Converter>
static Object* ConvertCase(Arguments args,
                           unibrow::Mapping<Converter, 128>* mapping) {
  NoHandleAllocation ha;

  CONVERT_CHECKED(String, s, args[0]);
  int raw_string_length = s->length();
  // Assume that the string is not empty; we need this assumption later
  if (raw_string_length == 0) return s;
  int length = raw_string_length;

  s->TryFlatten();

  // We try this twice, once with the assumption that the result is
  // no longer than the input and, if that assumption breaks, again
  // with the exact length.  This is implemented using a goto back
  // to this label if we discover that the assumption doesn't hold.
  // I apologize sincerely for this and will give a vaffel-is to
  // the first person who can implement it in a nicer way.
 try_convert:

  // Allocate the resulting string.
  //
  // NOTE: This assumes that the upper/lower case of an ascii
  // character is also ascii.  This is currently the case, but it
  // might break in the future if we implement more context and locale
  // dependent upper/lower conversions.
  Object* o = s->IsAscii()
      ? Heap::AllocateRawAsciiString(length)
      : Heap::AllocateRawTwoByteString(length);
  if (o->IsFailure()) return o;
  String* result = String::cast(o);
  bool has_changed_character = false;

  // Convert all characters to upper case, assuming that they will fit
  // in the buffer
  Access<StringInputBuffer> buffer(&string_input_buffer);
  buffer->Reset(s);
  unibrow::uchar chars[unibrow::kMaxCaseConvertedSize];
  int i = 0;
  // We can assume that the string is not empty
  uc32 current = buffer->GetNext();
  while (i < length) {
    uc32 next = buffer->has_more() ? buffer->GetNext() : 0;
    int char_length = mapping->get(current, next, chars);
    if (char_length == 0) {
      // The case conversion of this character is the character itself.
      result->Set(i, current);
      i++;
    } else if (char_length == 1) {
      // Common case: converting the letter resulted in one character.
      ASSERT(static_cast<uc32>(chars[0]) != current);
      result->Set(i, chars[0]);
      has_changed_character = true;
      i++;
    } else if (length == raw_string_length) {
      // We've assumed that the result would be as long as the
      // input but here is a character that converts to several
      // characters.  No matter, we calculate the exact length
      // of the result and try the whole thing again.
      //
      // Note that this leaves room for optimization.  We could just
      // memcpy what we already have to the result string.  Also,
      // the result string is the last object allocated we could
      // "realloc" it and probably, in the vast majority of cases,
      // extend the existing string to be able to hold the full
      // result.
      int current_length = i + char_length + mapping->get(next, 0, chars);
      while (buffer->has_more()) {
        current = buffer->GetNext();
        int char_length = mapping->get(current, 0, chars);
        if (char_length == 0) char_length = 1;
        current += char_length;
      }
      length = current_length;
      goto try_convert;
    } else {
      for (int j = 0; j < char_length; j++) {
        result->Set(i, chars[j]);
        i++;
      }
      has_changed_character = true;
    }
    current = next;
  }
  if (has_changed_character) {
    return result;
  } else {
    // If we didn't actually change anything in doing the conversion
    // we simple return the result and let the converted string
    // become garbage; there is no reason to keep two identical strings
    // alive.
    return s;
  }
}


static Object* Runtime_StringToLowerCase(Arguments args) {
  return ConvertCase<unibrow::ToLowercase>(args, &to_lower_mapping);
}


static Object* Runtime_StringToUpperCase(Arguments args) {
  return ConvertCase<unibrow::ToUppercase>(args, &to_upper_mapping);
}


static Object* Runtime_ConsStringFst(Arguments args) {
  NoHandleAllocation ha;

  CONVERT_CHECKED(ConsString, str, args[0]);
  return str->first();
}


static Object* Runtime_ConsStringSnd(Arguments args) {
  NoHandleAllocation ha;

  CONVERT_CHECKED(ConsString, str, args[0]);
  return str->second();
}


static Object* Runtime_NumberToString(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  Object* number = args[0];
  RUNTIME_ASSERT(number->IsNumber());

  Object* cached = Heap::GetNumberStringCache(number);
  if (cached != Heap::undefined_value()) {
    return cached;
  }

  char arr[100];
  Vector<char> buffer(arr, ARRAY_SIZE(arr));
  const char* str;
  if (number->IsSmi()) {
    int num = Smi::cast(number)->value();
    str = IntToCString(num, buffer);
  } else {
    double num = HeapNumber::cast(number)->value();
    str = DoubleToCString(num, buffer);
  }
  Object* result = Heap::AllocateStringFromAscii(CStrVector(str));

  if (!result->IsFailure()) {
    Heap::SetNumberStringCache(number, String::cast(result));
  }
  return result;
}


static Object* Runtime_NumberToInteger(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  Object* obj = args[0];
  if (obj->IsSmi()) return obj;
  CONVERT_DOUBLE_CHECKED(number, obj);
  return Heap::NumberFromDouble(DoubleToInteger(number));
}


static Object* Runtime_NumberToJSUint32(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  Object* obj = args[0];
  if (obj->IsSmi() && Smi::cast(obj)->value() >= 0) return obj;
  CONVERT_NUMBER_CHECKED(int32_t, number, Uint32, obj);
  return Heap::NumberFromUint32(number);
}


static Object* Runtime_NumberToJSInt32(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  Object* obj = args[0];
  if (obj->IsSmi()) return obj;
  CONVERT_DOUBLE_CHECKED(number, obj);
  return Heap::NumberFromInt32(DoubleToInt32(number));
}


static Object* Runtime_NumberAdd(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  return Heap::AllocateHeapNumber(x + y);
}


static Object* Runtime_NumberSub(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  return Heap::AllocateHeapNumber(x - y);
}


static Object* Runtime_NumberMul(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  return Heap::AllocateHeapNumber(x * y);
}


static Object* Runtime_NumberUnaryMinus(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(-x);
}


static Object* Runtime_NumberDiv(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  return Heap::NewNumberFromDouble(x / y);
}


static Object* Runtime_NumberMod(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);

#ifdef WIN32
  // Workaround MS fmod bugs. ECMA-262 says:
  // dividend is finite and divisor is an infinity => result equals dividend
  // dividend is a zero and divisor is nonzero finite => result equals dividend
  if (!(isfinite(x) && (!isfinite(y) && !isnan(y))) &&
      !(x == 0 && (y != 0 && isfinite(y))))
#endif
  x = fmod(x, y);
  // NewNumberFromDouble may return a Smi instead of a Number object
  return Heap::NewNumberFromDouble(x);
}


static Object* Runtime_StringAdd(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(String, str1, args[0]);
  CONVERT_CHECKED(String, str2, args[1]);
  int len1 = str1->length();
  int len2 = str2->length();
  if (len1 == 0) return str2;
  if (len2 == 0) return str1;
  int length_sum = len1 + len2;
  // Make sure that an out of memory exception is thrown if the length
  // of the new cons string is too large to fit in a Smi.
  if (length_sum > Smi::kMaxValue || length_sum < 0) {
    Top::context()->mark_out_of_memory();
    return Failure::OutOfMemoryException();
  }
  return Heap::AllocateConsString(str1, str2);
}


static Object* Runtime_StringBuilderConcat(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);
  CONVERT_CHECKED(JSArray, array, args[0]);
  CONVERT_CHECKED(String, special, args[1]);
  int special_length = special->length();
  Object* smi_array_length = array->length();
  if (!smi_array_length->IsSmi()) {
    Top::context()->mark_out_of_memory();
    return Failure::OutOfMemoryException();
  }
  int array_length = Smi::cast(smi_array_length)->value();
  if (!array->HasFastElements()) {
    return Top::Throw(Heap::illegal_argument_symbol());
  }
  FixedArray* fixed_array = FixedArray::cast(array->elements());
  if (fixed_array->length() < array_length) {
    array_length = fixed_array->length();
  }

  if (array_length == 0) {
    return Heap::empty_string();
  } else if (array_length == 1) {
    Object* first = fixed_array->get(0);
    if (first->IsString()) return first;
  }

  bool ascii = special->IsAscii();
  int position = 0;
  for (int i = 0; i < array_length; i++) {
    Object* elt = fixed_array->get(i);
    if (elt->IsSmi()) {
      int len = Smi::cast(elt)->value();
      int pos = len >> 11;
      len &= 0x7ff;
      if (pos + len > special_length) {
        return Top::Throw(Heap::illegal_argument_symbol());
      }
      position += len;
    } else if (elt->IsString()) {
      String* element = String::cast(elt);
      int element_length = element->length();
      if (!Smi::IsValid(element_length + position)) {
        Top::context()->mark_out_of_memory();
        return Failure::OutOfMemoryException();
      }
      position += element_length;
      if (ascii && !element->IsAscii()) {
        ascii = false;
      }
    } else {
      return Top::Throw(Heap::illegal_argument_symbol());
    }
  }

  int length = position;
  position = 0;
  Object* object;
  if (ascii) {
    object = Heap::AllocateRawAsciiString(length);
  } else {
    object = Heap::AllocateRawTwoByteString(length);
  }
  if (object->IsFailure()) return object;

  String* answer = String::cast(object);
  for (int i = 0; i < array_length; i++) {
    Object* element = fixed_array->get(i);
    if (element->IsSmi()) {
      int len = Smi::cast(element)->value();
      int pos = len >> 11;
      len &= 0x7ff;
      String::Flatten(special, answer, pos, pos + len, position);
      position += len;
    } else {
      String* string = String::cast(element);
      int element_length = string->length();
      String::Flatten(string, answer, 0, element_length, position);
      position += element_length;
    }
  }
  return answer;
}


static Object* Runtime_NumberOr(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_NUMBER_CHECKED(int32_t, x, Int32, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, y, Int32, args[1]);
  return Heap::NumberFromInt32(x | y);
}


static Object* Runtime_NumberAnd(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_NUMBER_CHECKED(int32_t, x, Int32, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, y, Int32, args[1]);
  return Heap::NumberFromInt32(x & y);
}


static Object* Runtime_NumberXor(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_NUMBER_CHECKED(int32_t, x, Int32, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, y, Int32, args[1]);
  return Heap::NumberFromInt32(x ^ y);
}


static Object* Runtime_NumberNot(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_NUMBER_CHECKED(int32_t, x, Int32, args[0]);
  return Heap::NumberFromInt32(~x);
}


static Object* Runtime_NumberShl(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_NUMBER_CHECKED(int32_t, x, Int32, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, y, Int32, args[1]);
  return Heap::NumberFromInt32(x << (y & 0x1f));
}


static Object* Runtime_NumberShr(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_NUMBER_CHECKED(uint32_t, x, Uint32, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, y, Int32, args[1]);
  return Heap::NumberFromUint32(x >> (y & 0x1f));
}


static Object* Runtime_NumberSar(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_NUMBER_CHECKED(int32_t, x, Int32, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, y, Int32, args[1]);
  return Heap::NumberFromInt32(ArithmeticShiftRight(x, y & 0x1f));
}


static Object* Runtime_ObjectEquals(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  return Smi::FromInt(args[0] == args[1] ? EQUAL : NOT_EQUAL);
}


static Object* Runtime_NumberEquals(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  if (isnan(x)) return Smi::FromInt(NOT_EQUAL);
  if (isnan(y)) return Smi::FromInt(NOT_EQUAL);
  if (x == y) return Smi::FromInt(EQUAL);
  Object* result;
  if ((fpclassify(x) == FP_ZERO) && (fpclassify(y) == FP_ZERO)) {
    result = Smi::FromInt(EQUAL);
  } else {
    result = Smi::FromInt(NOT_EQUAL);
  }
  return result;
}


static Object* Runtime_StringEquals(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(String, x, args[0]);
  CONVERT_CHECKED(String, y, args[1]);

  // This is very similar to String::Equals(String*) but that version
  // requires flattened strings as input, whereas we flatten the
  // strings only if the fast cases fail.  Note that this may fail,
  // requiring a GC.  String::Equals(String*) returns a bool and has
  // no way to signal a failure.
  if (y == x) return Smi::FromInt(EQUAL);
  if (x->IsSymbol() && y->IsSymbol()) return Smi::FromInt(NOT_EQUAL);
  // Compare contents
  int len = x->length();
  if (len != y->length()) return Smi::FromInt(NOT_EQUAL);
  if (len == 0) return Smi::FromInt(EQUAL);
  // Fast case:  First, middle and last characters.
  if (x->Get(0) != y->Get(0)) return Smi::FromInt(NOT_EQUAL);
  if (x->Get(len>>1) != y->Get(len>>1)) return Smi::FromInt(NOT_EQUAL);
  if (x->Get(len - 1) != y->Get(len - 1)) return Smi::FromInt(NOT_EQUAL);

  x->TryFlatten();
  y->TryFlatten();

  static StringInputBuffer buf1;
  static StringInputBuffer buf2;
  buf1.Reset(x);
  buf2.Reset(y);
  while (buf1.has_more()) {
    if (buf1.GetNext() != buf2.GetNext())
      return Smi::FromInt(NOT_EQUAL);
  }
  return Smi::FromInt(EQUAL);
}


static Object* Runtime_NumberCompare(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 3);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  if (isnan(x) || isnan(y)) return args[2];
  if (x == y) return Smi::FromInt(EQUAL);
  if (isless(x, y)) return Smi::FromInt(LESS);
  return Smi::FromInt(GREATER);
}


static Object* Runtime_StringCompare(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_CHECKED(String, x, args[0]);
  CONVERT_CHECKED(String, y, args[1]);

  // A few fast case tests before we flatten.
  if (x == y) return Smi::FromInt(EQUAL);
  if (y->length() == 0) {
    if (x->length() == 0) return Smi::FromInt(EQUAL);
    return Smi::FromInt(GREATER);
  } else if (x->length() == 0) {
    return Smi::FromInt(LESS);
  }

  int d = x->Get(0) - y->Get(0);
  if (d < 0) return Smi::FromInt(LESS);
  else if (d > 0) return Smi::FromInt(GREATER);

  x->TryFlatten();
  y->TryFlatten();

  static StringInputBuffer bufx;
  static StringInputBuffer bufy;
  bufx.Reset(x);
  bufy.Reset(y);
  while (bufx.has_more() && bufy.has_more()) {
    int d = bufx.GetNext() - bufy.GetNext();
    if (d < 0) return Smi::FromInt(LESS);
    else if (d > 0) return Smi::FromInt(GREATER);
  }

  // x is (non-trivial) prefix of y:
  if (bufy.has_more()) return Smi::FromInt(LESS);
  // y is prefix of x:
  return Smi::FromInt(bufx.has_more() ? GREATER : EQUAL);
}


static Object* Runtime_Math_abs(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(fabs(x));
}


static Object* Runtime_Math_acos(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(acos(x));
}


static Object* Runtime_Math_asin(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(asin(x));
}


static Object* Runtime_Math_atan(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(atan(x));
}


static Object* Runtime_Math_atan2(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  double result;
  if (isinf(x) && isinf(y)) {
    // Make sure that the result in case of two infinite arguments
    // is a multiple of Pi / 4. The sign of the result is determined
    // by the first argument (x) and the sign of the second argument
    // determines the multiplier: one or three.
    static double kPiDividedBy4 = 0.78539816339744830962;
    int multiplier = (x < 0) ? -1 : 1;
    if (y < 0) multiplier *= 3;
    result = multiplier * kPiDividedBy4;
  } else {
    result = atan2(x, y);
  }
  return Heap::AllocateHeapNumber(result);
}


static Object* Runtime_Math_ceil(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::NumberFromDouble(ceiling(x));
}


static Object* Runtime_Math_cos(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(cos(x));
}


static Object* Runtime_Math_exp(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(exp(x));
}


static Object* Runtime_Math_floor(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::NumberFromDouble(floor(x));
}


static Object* Runtime_Math_log(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(log(x));
}


static Object* Runtime_Math_pow(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 2);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  CONVERT_DOUBLE_CHECKED(y, args[1]);
  if (isnan(y) || ((x == 1 || x == -1) && isinf(y))) {
    return Heap::nan_value();
  } else if (y == 0) {
    return Smi::FromInt(1);
  } else {
    return Heap::AllocateHeapNumber(pow(x, y));
  }
}

// Returns a number value with positive sign, greater than or equal to
// 0 but less than 1, chosen randomly.
static Object* Runtime_Math_random(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);

  // To get much better precision, we combine the results of two
  // invocations of random(). The result is computed by normalizing a
  // double in the range [0, RAND_MAX + 1) obtained by adding the
  // high-order bits in the range [0, RAND_MAX] with the low-order
  // bits in the range [0, 1).
  double lo = static_cast<double>(random()) / (RAND_MAX + 1.0);
  double hi = static_cast<double>(random());
  double result = (hi + lo) / (RAND_MAX + 1.0);
  ASSERT(result >= 0 && result < 1);
  return Heap::AllocateHeapNumber(result);
}


static Object* Runtime_Math_round(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  if (signbit(x) && x >= -0.5) return Heap::minus_zero_value();
  return Heap::NumberFromDouble(floor(x + 0.5));
}


static Object* Runtime_Math_sin(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(sin(x));
}


static Object* Runtime_Math_sqrt(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(sqrt(x));
}


static Object* Runtime_Math_tan(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::AllocateHeapNumber(tan(x));
}


static Object* Runtime_NewArguments(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  // ECMA-262, 3rd., 10.1.8, p.39
  CONVERT_CHECKED(JSFunction, callee, args[0]);

  // Compute the frame holding the arguments.
  JavaScriptFrameIterator it;
  it.AdvanceToArgumentsFrame();
  JavaScriptFrame* frame = it.frame();

  const int length = frame->GetProvidedParametersCount();
  Object* result = Heap::AllocateArgumentsObject(callee, length);
  if (result->IsFailure()) return result;
  FixedArray* array = FixedArray::cast(JSObject::cast(result)->elements());
  ASSERT(array->length() == length);
  for (int i = 0; i < length; i++) {
    array->set(i, frame->GetParameter(i));
  }
  return result;
}


static Object* Runtime_NewClosure(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);
  CONVERT_ARG_CHECKED(JSFunction, boilerplate, 0);
  CONVERT_ARG_CHECKED(Context, context, 1);

  Handle<JSFunction> result =
      Factory::NewFunctionFromBoilerplate(boilerplate, context);
  return *result;
}


static Object* Runtime_NewObject(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  Object* constructor = args[0];
  if (constructor->IsJSFunction()) {
    JSFunction* function = JSFunction::cast(constructor);

    // Handle steping into constructors.
    if (Debug::StepInActive()) {
      StackFrameIterator it;
      it.Advance();
      ASSERT(InternalFrame::cast(it.frame())->is_construct_trampoline());
      it.Advance();
      if (it.frame()->fp() == Debug::step_in_fp()) {
        HandleScope scope;
        Debug::FloodWithOneShot(Handle<SharedFunctionInfo>(function->shared()));
      }
    }

    if (function->has_initial_map() &&
        function->initial_map()->instance_type() == JS_FUNCTION_TYPE) {
      // The 'Function' function ignores the receiver object when
      // called using 'new' and creates a new JSFunction object that
      // is returned.  The receiver object is only used for error
      // reporting if an error occurs when constructing the new
      // JSFunction.  AllocateJSObject should not be used to allocate
      // JSFunctions since it does not properly initialize the shared
      // part of the function.  Since the receiver is ignored anyway,
      // we use the global object as the receiver instead of a new
      // JSFunction object.  This way, errors are reported the same
      // way whether or not 'Function' is called using 'new'.
      return Top::context()->global();
    }
    return Heap::AllocateJSObject(function);
  }

  HandleScope scope;
  Handle<Object> cons(constructor);
  // The constructor is not a function; throw a type error.
  Handle<Object> type_error =
    Factory::NewTypeError("not_constructor", HandleVector(&cons, 1));
  return Top::Throw(*type_error);
}


#ifdef DEBUG
DEFINE_bool(trace_lazy, false, "trace lazy compilation");
#endif


static Object* Runtime_LazyCompile(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  Handle<JSFunction> function = args.at<JSFunction>(0);
#ifdef DEBUG
  if (FLAG_trace_lazy) {
    PrintF("[lazy: ");
    function->shared()->name()->Print();
    PrintF("]\n");
  }
#endif

  // Compile the target function.
  ASSERT(!function->is_compiled());
  if (!CompileLazy(function, KEEP_EXCEPTION)) {
    return Failure::Exception();
  }

  return function->code();
}


static Object* Runtime_GetCalledFunction(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 0);
  StackFrameIterator it;
  // Get past the JS-to-C exit frame.
  ASSERT(it.frame()->is_exit());
  it.Advance();
  // Get past the CALL_NON_FUNCTION activation frame.
  ASSERT(it.frame()->is_java_script());
  it.Advance();
  // Argument adaptor frames do not copy the function; we have to skip
  // past them to get to the real calling frame.
  if (it.frame()->is_arguments_adaptor()) it.Advance();
  // Get the function from the top of the expression stack of the
  // calling frame.
  StandardFrame* frame = StandardFrame::cast(it.frame());
  int index = frame->ComputeExpressionsCount() - 1;
  Object* result = frame->GetExpression(index);
  return result;
}


static Object* Runtime_GetFunctionDelegate(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  RUNTIME_ASSERT(!args[0]->IsJSFunction());
  return *Execution::GetFunctionDelegate(args.at<Object>(0));
}


static Object* Runtime_NewContext(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSFunction, function, args[0]);
  int length = ScopeInfo<>::NumberOfContextSlots(function->code());
  Object* result = Heap::AllocateFunctionContext(length, function);
  if (result->IsFailure()) return result;

  Top::set_context(Context::cast(result));

  return result;  // non-failure
}


static Object* Runtime_PushContext(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  // Convert the object to a proper JavaScript object.
  Object* object = args[0];
  if (!object->IsJSObject()) {
    object = object->ToObject();
    if (object->IsFailure()) {
      if (!Failure::cast(object)->IsInternalError()) return object;
      HandleScope scope;
      Handle<Object> handle(args[0]);
      Handle<Object> result =
          Factory::NewTypeError("with_expression", HandleVector(&handle, 1));
      return Top::Throw(*result);
    }
  }

  Object* result =
      Heap::AllocateWithContext(Top::context(), JSObject::cast(object));
  if (result->IsFailure()) return result;

  Top::set_context(Context::cast(result));

  return result;
}


static Object* Runtime_LookupContext(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);

  CONVERT_ARG_CHECKED(Context, context, 0);
  CONVERT_ARG_CHECKED(String, name, 1);

  int index;
  PropertyAttributes attributes;
  ContextLookupFlags flags = FOLLOW_CHAINS;
  Handle<Object> context_obj =
      context->Lookup(name, flags, &index, &attributes);

  if (index < 0 && *context_obj != NULL) {
    ASSERT(context_obj->IsJSObject());
    return *context_obj;
  }

  // No intermediate context found. Use global object by default.
  return Top::context()->global();
}


// A mechanism to return pairs of Object*'s. This is somewhat
// compiler-dependent as it assumes that a 64-bit value (a long long)
// is returned via two registers (edx:eax on ia32). Both the ia32 and
// arm platform support this; it is mostly an issue of "coaxing" the
// compiler to do the right thing.
//
// TODO(1236026): This is a non-portable hack that should be removed.
typedef uint64_t ObjPair;
ObjPair MakePair(Object* x, Object* y) {
  return reinterpret_cast<uint32_t>(x) |
         (reinterpret_cast<ObjPair>(y) << 32);
}


static Object* Unhole(Object* x, PropertyAttributes attributes) {
  ASSERT(!x->IsTheHole() || (attributes & READ_ONLY) != 0);
  USE(attributes);
  return x->IsTheHole() ? Heap::undefined_value() : x;
}


static ObjPair LoadContextSlotHelper(Arguments args, bool throw_error) {
  HandleScope scope;
  ASSERT(args.length() == 2);

  if (!args[0]->IsContext()) return MakePair(IllegalOperation(), NULL);
  Handle<Context> context = args.at<Context>(0);
  Handle<String> name(String::cast(args[1]));

  int index;
  PropertyAttributes attributes;
  ContextLookupFlags flags = FOLLOW_CHAINS;
  Handle<Object> context_obj =
      context->Lookup(name, flags, &index, &attributes);

  if (index >= 0) {
    if (context_obj->IsContext()) {
      // The context is an Execution context, and the "property" we were looking
      // for is a local variable in that context. According to ECMA-262, 3rd.,
      // 10.1.6 and 10.2.3, the receiver is the global object.
      return MakePair(
          Unhole(Handle<Context>::cast(context_obj)->get(index), attributes),
          Top::context()->global());
    } else {
      return MakePair(
          Unhole(Handle<JSObject>::cast(context_obj)->GetElement(index),
                 attributes),
          *context_obj);
    }
  }

  if (*context_obj != NULL) {
    ASSERT(Handle<JSObject>::cast(context_obj)->HasProperty(*name));
    // Note: As of 5/29/2008, GetProperty does the "unholing" and so this call
    // here is redundant. We left it anyway, to be explicit; also it's not clear
    // why GetProperty should do the unholing in the first place.
    return MakePair(
        Unhole(Handle<JSObject>::cast(context_obj)->GetProperty(*name),
               attributes),
        *context_obj);
  }

  if (throw_error) {
    // The property doesn't exist - throw exception.
    Handle<Object> reference_error =
        Factory::NewReferenceError("not_defined", HandleVector(&name, 1));
    return MakePair(Top::Throw(*reference_error), NULL);
  } else {
    // The property doesn't exist - return undefined
    return MakePair(Heap::undefined_value(), Heap::undefined_value());
  }
}


static ObjPair Runtime_LoadContextSlot(Arguments args) {
  return LoadContextSlotHelper(args, true);
}


static ObjPair Runtime_LoadContextSlotNoReferenceError(Arguments args) {
  return LoadContextSlotHelper(args, false);
}


static Object* Runtime_StoreContextSlot(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);

  Handle<Object> value(args[0]);
  CONVERT_ARG_CHECKED(Context, context, 1);
  CONVERT_ARG_CHECKED(String, name, 2);

  int index;
  PropertyAttributes attributes;
  ContextLookupFlags flags = FOLLOW_CHAINS;
  Handle<Object> context_obj =
      context->Lookup(name, flags, &index, &attributes);

  if (index >= 0) {
    if (context_obj->IsContext()) {
      // Ignore if read_only variable.
      if ((attributes & READ_ONLY) == 0) {
        Handle<Context>::cast(context_obj)->set(index, *value);
      }
    } else {
      ASSERT((attributes & READ_ONLY) == 0);
      Object* result =
          Handle<JSObject>::cast(context_obj)->SetElement(index, *value);
      USE(result);
      ASSERT(!result->IsFailure());
    }
    return *value;
  }

  // Slow case: The property is not in a FixedArray context.
  // It is either in an JSObject extension context or it was not found.
  Handle<JSObject> context_ext;

  if (*context_obj != NULL) {
    // The property exists in the extension context.
    context_ext = Handle<JSObject>::cast(context_obj);
  } else {
    // The property was not found. It needs to be stored in the global context.
    ASSERT(attributes == ABSENT);
    attributes = NONE;
    context_ext = Handle<JSObject>(Top::context()->global());
  }

  // Set the property, but ignore if read_only variable.
  if ((attributes & READ_ONLY) == 0) {
    Handle<Object> set = SetProperty(context_ext, name, value, attributes);
    if (set.is_null()) {
      // Failure::Exception is converted to a null handle in the
      // handle-based methods such as SetProperty.  We therefore need
      // to convert null handles back to exceptions.
      ASSERT(Top::has_pending_exception());
      return Failure::Exception();
    }
  }
  return *value;
}


static Object* Runtime_Throw(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  return Top::Throw(args[0]);
}


static Object* Runtime_ReThrow(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  return Top::ReThrow(args[0]);
}


static Object* Runtime_ThrowReferenceError(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  Handle<Object> name(args[0]);
  Handle<Object> reference_error =
    Factory::NewReferenceError("not_defined", HandleVector(&name, 1));
  return Top::Throw(*reference_error);
}


static Object* Runtime_StackOverflow(Arguments args) {
  NoHandleAllocation na;
  return Top::StackOverflow();
}


static Object* RuntimePreempt(Arguments args) {
  // Clear the preempt request flag.
  StackGuard::Continue(PREEMPT);

  ContextSwitcher::PreemptionReceived();

  {
    v8::Unlocker unlocker;
    Thread::YieldCPU();
  }

  return Heap::undefined_value();
}


static Object* Runtime_DebugBreak(Arguments args) {
  // Just continue if breaks are disabled or if we fail to load the debugger.
  if (Debug::disable_break() || !Debug::Load()) {
    return args[0];
  }

  // Don't break in system functions. If the current function is
  // either in the builtins object of some context or is in the debug
  // context just return with the debug break stack guard active.
  JavaScriptFrameIterator it;
  JavaScriptFrame* frame = it.frame();
  Object* fun = frame->function();
  if (fun->IsJSFunction()) {
    GlobalObject* global = JSFunction::cast(fun)->context()->global();
    if (global->IsJSBuiltinsObject() || Debug::IsDebugGlobal(global)) {
      return args[0];
    }
  }

  // Clear the debug request flag.
  StackGuard::Continue(DEBUGBREAK);

  HandleScope scope;
  SaveBreakFrame save;
  EnterDebuggerContext enter;

  // Notify the debug event listeners.
  Debugger::OnDebugBreak(Factory::undefined_value());

  // Return to continue execution.
  return args[0];
}


static Object* Runtime_StackGuard(Arguments args) {
  ASSERT(args.length() == 1);

  // First check if this is a real stack overflow.
  if (StackGuard::IsStackOverflow()) return Runtime_StackOverflow(args);

  // If not real stack overflow the stack guard was used to interrupt
  // execution for another purpose.
  if (StackGuard::IsDebugBreak()) Runtime_DebugBreak(args);
  if (StackGuard::IsPreempted()) RuntimePreempt(args);
  if (StackGuard::IsInterrupted()) {
    // interrupt
    StackGuard::Continue(INTERRUPT);
    return Top::StackOverflow();
  }
  return Heap::undefined_value();
}


// NOTE: These PrintXXX functions are defined for all builds (not just
// DEBUG builds) because we may want to be able to trace function
// calls in all modes.
static void PrintString(String* str) {
  // not uncommon to have empty strings
  if (str->length() > 0) {
    SmartPointer<char> s =
        str->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
    PrintF("%s", *s);
  }
}


static void PrintObject(Object* obj) {
  if (obj->IsSmi()) {
    PrintF("%d", Smi::cast(obj)->value());
  } else if (obj->IsString() || obj->IsSymbol()) {
    PrintString(String::cast(obj));
  } else if (obj->IsNumber()) {
    PrintF("%g", obj->Number());
  } else if (obj->IsFailure()) {
    PrintF("<failure>");
  } else if (obj->IsUndefined()) {
    PrintF("<undefined>");
  } else if (obj->IsNull()) {
    PrintF("<null>");
  } else if (obj->IsTrue()) {
    PrintF("<true>");
  } else if (obj->IsFalse()) {
    PrintF("<false>");
  } else {
    PrintF("%p", obj);
  }
}


static int StackSize() {
  int n = 0;
  for (JavaScriptFrameIterator it; !it.done(); it.Advance()) n++;
  return n;
}


static void PrintTransition(Object* result) {
  // indentation
  { const int nmax = 80;
    int n = StackSize();
    if (n <= nmax)
      PrintF("%4d:%*s", n, n, "");
    else
      PrintF("%4d:%*s", n, nmax, "...");
  }

  if (result == NULL) {
    // constructor calls
    JavaScriptFrameIterator it;
    JavaScriptFrame* frame = it.frame();
    if (frame->IsConstructor()) PrintF("new ");
    // function name
    Object* fun = frame->function();
    if (fun->IsJSFunction()) {
      PrintObject(JSFunction::cast(fun)->shared()->name());
    } else {
      PrintObject(fun);
    }
    // function arguments
    // (we are intentionally only printing the actually
    // supplied parameters, not all parameters required)
    PrintF("(this=");
    PrintObject(frame->receiver());
    const int length = frame->GetProvidedParametersCount();
    for (int i = 0; i < length; i++) {
      PrintF(", ");
      PrintObject(frame->GetParameter(i));
    }
    PrintF(") {\n");

  } else {
    // function result
    PrintF("} -> ");
    PrintObject(result);
    PrintF("\n");
  }
}


static Object* Runtime_TraceEnter(Arguments args) {
  NoHandleAllocation ha;
  PrintTransition(NULL);
  return args[0];  // return TOS
}


static Object* Runtime_TraceExit(Arguments args) {
  NoHandleAllocation ha;
  PrintTransition(args[0]);
  return args[0];  // return TOS
}


static Object* Runtime_DebugPrint(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

#ifdef DEBUG
  if (args[0]->IsString()) {
    // If we have a string, assume it's a code "marker"
    // and print some interesting cpu debugging info.
    JavaScriptFrameIterator it;
    JavaScriptFrame* frame = it.frame();
    PrintF("fp = %p, sp = %p, pp = %p: ",
           frame->fp(), frame->sp(), frame->pp());
  } else {
    PrintF("DebugPrint: ");
  }
  args[0]->Print();
#else
  PrintF("DebugPrint: %p", args[0]);
#endif
  PrintF("\n");

  return args[0];  // return TOS
}


static Object* Runtime_DebugTrace(Arguments args) {
  ASSERT(args.length() == 1);
  NoHandleAllocation ha;
  Top::PrintStack();
  return args[0];  // return TOS
}


static Object* Runtime_DateCurrentTime(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);

  // According to ECMA-262, section 15.9.1, page 117, the precision of
  // the number in a Date object representing a particular instant in
  // time is milliseconds. Therefore, we floor the result of getting
  // the OS time.
  double millis = floor(OS::TimeCurrentMillis());
  return Heap::NumberFromDouble(millis);
}


static Object* Runtime_DateParseString(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(String, string_object, args[0]);

  Handle<String> str(string_object);
  Handle<FixedArray> output = Factory::NewFixedArray(DateParser::OUTPUT_SIZE);
  if (DateParser::Parse(*str, *output)) {
    return *Factory::NewJSArrayWithElements(output);
  } else {
    return *Factory::null_value();
  }
}


static Object* Runtime_DateLocalTimezone(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  char* zone = OS::LocalTimezone(x);
  return Heap::AllocateStringFromUtf8(CStrVector(zone));
}


static Object* Runtime_DateLocalTimeOffset(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);

  return Heap::NumberFromDouble(OS::LocalTimeOffset());
}


static Object* Runtime_DateDaylightSavingsOffset(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(x, args[0]);
  return Heap::NumberFromDouble(OS::DaylightSavingsOffset(x));
}


static Object* Runtime_NumberIsFinite(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_DOUBLE_CHECKED(value, args[0]);
  Object* result;
  if (isnan(value) || (fpclassify(value) == FP_INFINITE)) {
    result = Heap::false_value();
  } else {
    result = Heap::true_value();
  }
  return result;
}


static Object* Runtime_NumberMaxValue(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);

  return Heap::number_max_value();
}


static Object* Runtime_NumberMinValue(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);

  return Heap::number_min_value();
}


static Object* Runtime_NumberNaN(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 0);

  return Heap::nan_value();
}


static Object* EvalContext() {
  // The topmost JS frame belongs to the eval function which called
  // the CompileString runtime function. We need to unwind one level
  // to get to the caller of eval.
  StackFrameLocator locator;
  JavaScriptFrame* frame = locator.FindJavaScriptFrame(1);

  // TODO(900055): Right now we check if the caller of eval() supports
  // eval to determine if it's an aliased eval or not. This may not be
  // entirely correct in the unlikely case where a function uses both
  // aliased and direct eval calls.
  HandleScope scope;
  if (!ScopeInfo<>::SupportsEval(frame->FindCode())) {
    // Aliased eval: Evaluate in the global context of the eval
    // function to support aliased, cross environment evals.
    return *Top::global_context();
  }

  // Fetch the caller context from the frame.
  Handle<Context> caller(Context::cast(frame->context()));

  // Check for eval() invocations that cross environments. Use the
  // context from the stack if evaluating in current environment.
  Handle<Context> target = Top::global_context();
  if (caller->global_context() == *target) return *caller;

  // Compute a function closure that captures the calling context. We
  // need a function that has trivial scope info, since it is only
  // used to hold the context chain together.
  Handle<JSFunction> closure = Factory::NewFunction(Factory::empty_symbol(),
                                                    Factory::undefined_value());
  closure->set_context(*caller);

  // Create a new adaptor context that has the target environment as
  // the extension object. This enables the evaluated code to see both
  // the current context with locals and everything and to see global
  // variables declared in the target global object. Furthermore, any
  // properties introduced with 'var' will be added to the target
  // global object because it is the extension object.
  Handle<Context> adaptor =
    Factory::NewFunctionContext(Context::MIN_CONTEXT_SLOTS, closure);
  adaptor->set_extension(target->global());
  return *adaptor;
}


static Object* Runtime_EvalReceiver(Arguments args) {
  StackFrameLocator locator;
  return locator.FindJavaScriptFrame(1)->receiver();
}


static Object* Runtime_CompileString(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);
  bool contextual = args[1]->IsTrue();
  RUNTIME_ASSERT(contextual || args[1]->IsFalse());

  // Compute the eval context.
  Handle<Context> context;
  if (contextual) {
    // Get eval context. May not be available if we are calling eval
    // through an alias, and the corresponding frame doesn't have a
    // proper eval context set up.
    Object* eval_context = EvalContext();
    if (eval_context->IsFailure()) return eval_context;
    context = Handle<Context>(Context::cast(eval_context));
  } else {
    context = Handle<Context>(Top::context()->global_context());
  }

  // Compile eval() source.
  Handle<String> source(String::cast(args[0]));
  Handle<JSFunction> boilerplate =
      Compiler::CompileEval(context->IsGlobalContext(), source);
  if (boilerplate.is_null()) return Failure::Exception();
  Handle<JSFunction> fun =
      Factory::NewFunctionFromBoilerplate(boilerplate, context);
  return *fun;
}


static Object* Runtime_CompileScript(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 4);

  CONVERT_ARG_CHECKED(String, source, 0);
  CONVERT_ARG_CHECKED(String, script, 1);
  CONVERT_CHECKED(Smi, line_attrs, args[2]);
  int line = line_attrs->value();
  CONVERT_CHECKED(Smi, col_attrs, args[3]);
  int col = col_attrs->value();
  Handle<JSFunction> boilerplate =
      Compiler::Compile(source, script, line, col, NULL, NULL);
  if (boilerplate.is_null()) return Failure::Exception();
  Handle<JSFunction> fun =
      Factory::NewFunctionFromBoilerplate(boilerplate,
                                          Handle<Context>(Top::context()));
  return *fun;
}


static Object* Runtime_SetNewFunctionAttributes(Arguments args) {
  // This utility adjusts the property attributes for newly created Function
  // object ("new Function(...)") by changing the map.
  // All it does is changing the prototype property to enumerable
  // as specified in ECMA262, 15.3.5.2.
  HandleScope scope;
  ASSERT(args.length() == 1);
  CONVERT_ARG_CHECKED(JSFunction, func, 0);
  ASSERT(func->map()->instance_type() ==
         Top::function_instance_map()->instance_type());
  ASSERT(func->map()->instance_size() ==
         Top::function_instance_map()->instance_size());
  func->set_map(*Top::function_instance_map());
  return *func;
}


// This will not allocate (flatten the string), but it may run
// very slowly for very deeply nested ConsStrings.  For debugging use only.
static Object* Runtime_GlobalPrint(Arguments args) {
  NoHandleAllocation ha;
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(String, string, args[0]);
  StringInputBuffer buffer(string);
  while (buffer.has_more()) {
    uint16_t character = buffer.GetNext();
    PrintF("%c", character);
  }
  return string;
}


static Object* Runtime_RemoveArrayHoles(Arguments args) {
  ASSERT(args.length() == 1);
  // Ignore the case if this is not a JSArray.
  if (!args[0]->IsJSArray()) return args[0];
  return JSArray::cast(args[0])->RemoveHoles();
}


// Move contents of argument 0 (an array) to argument 1 (an array)
static Object* Runtime_MoveArrayContents(Arguments args) {
  ASSERT(args.length() == 2);
  CONVERT_CHECKED(JSArray, from, args[0]);
  CONVERT_CHECKED(JSArray, to, args[1]);
  to->SetContent(FixedArray::cast(from->elements()));
  to->set_length(from->length());
  from->SetContent(Heap::empty_fixed_array());
  from->set_length(0);
  return to;
}


// How many elements does this array have?
static Object* Runtime_EstimateNumberOfElements(Arguments args) {
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(JSArray, array, args[0]);
  HeapObject* elements = array->elements();
  if (elements->IsDictionary()) {
    return Smi::FromInt(Dictionary::cast(elements)->NumberOfElements());
  } else {
    return array->length();
  }
}


// Returns an array that tells you where in the [0, length) interval an array
// might have elements.  Can either return keys or intervals.  Keys can have
// gaps in (undefined).  Intervals can also span over some undefined keys.
static Object* Runtime_GetArrayKeys(Arguments args) {
  ASSERT(args.length() == 2);
  HandleScope scope;
  CONVERT_CHECKED(JSArray, raw_array, args[0]);
  Handle<JSArray> array(raw_array);
  CONVERT_NUMBER_CHECKED(uint32_t, length, Uint32, args[1]);
  if (array->elements()->IsDictionary()) {
    // Create an array and get all the keys into it, then remove all the
    // keys that are not integers in the range 0 to length-1.
    Handle<FixedArray> keys = GetKeysInFixedArrayFor(array);
    int keys_length = keys->length();
    for (int i = 0; i < keys_length; i++) {
      Object* key = keys->get(i);
      uint32_t index;
      if (!Array::IndexFromObject(key, &index) || index >= length) {
        // Zap invalid keys.
        keys->set_undefined(i);
      }
    }
    return *Factory::NewJSArrayWithElements(keys);
  } else {
    Handle<FixedArray> single_interval = Factory::NewFixedArray(2);
    // -1 means start of array.
    single_interval->set(0, Smi::FromInt(-1));
    Handle<Object> length_object =
        Factory::NewNumber(static_cast<double>(length));
    single_interval->set(1, *length_object);
    return *Factory::NewJSArrayWithElements(single_interval);
  }
}


// DefineAccessor takes an optional final argument which is the
// property attributes (eg, DONT_ENUM, DONT_DELETE).  IMPORTANT: due
// to the way accessors are implemented, it is set for both the getter
// and setter on the first call to DefineAccessor and ignored on
// subsequent calls.
static Object* Runtime_DefineAccessor(Arguments args) {
  RUNTIME_ASSERT(args.length() == 4 || args.length() == 5);
  // Compute attributes.
  PropertyAttributes attributes = NONE;
  if (args.length() == 5) {
    CONVERT_CHECKED(Smi, attrs, args[4]);
    int value = attrs->value();
    // Only attribute bits should be set.
    ASSERT((value & ~(READ_ONLY | DONT_ENUM | DONT_DELETE)) == 0);
    attributes = static_cast<PropertyAttributes>(value);
  }

  CONVERT_CHECKED(JSObject, obj, args[0]);
  CONVERT_CHECKED(String, name, args[1]);
  CONVERT_CHECKED(Smi, flag, args[2]);
  CONVERT_CHECKED(JSFunction, fun, args[3]);
  return obj->DefineAccessor(name, flag->value() == 0, fun, attributes);
}


static Object* Runtime_LookupAccessor(Arguments args) {
  ASSERT(args.length() == 3);
  CONVERT_CHECKED(JSObject, obj, args[0]);
  CONVERT_CHECKED(String, name, args[1]);
  CONVERT_CHECKED(Smi, flag, args[2]);
  return obj->LookupAccessor(name, flag->value() == 0);
}


// Helper functions for wrapping and unwrapping stack frame ids.
static Smi* WrapFrameId(StackFrame::Id id) {
  ASSERT(IsAligned(OffsetFrom(id), 4));
  return Smi::FromInt(id >> 2);
}


static StackFrame::Id UnwrapFrameId(Smi* wrapped) {
  return static_cast<StackFrame::Id>(wrapped->value() << 2);
}


// Adds a JavaScript function as a debug event listener.
// args[0]: debug event listener function
// args[1]: object supplied during callback
static Object* Runtime_AddDebugEventListener(Arguments args) {
  ASSERT(args.length() == 2);
  // Convert the parameters to API objects to call the API function for adding
  // a JavaScript function as debug event listener.
  CONVERT_ARG_CHECKED(JSFunction, raw_fun, 0);
  v8::Handle<v8::Function> fun(ToApi<v8::Function>(raw_fun));
  v8::Handle<v8::Value> data(ToApi<v8::Value>(args.at<Object>(0)));
  v8::Debug::AddDebugEventListener(fun, data);

  return Heap::undefined_value();
}


// Removes a JavaScript function debug event listener.
// args[0]: debug event listener function
static Object* Runtime_RemoveDebugEventListener(Arguments args) {
  ASSERT(args.length() == 1);
  // Convert the parameter to an API object to call the API function for
  // removing a JavaScript function debug event listener.
  CONVERT_ARG_CHECKED(JSFunction, raw_fun, 0);
  v8::Handle<v8::Function> fun(ToApi<v8::Function>(raw_fun));
  v8::Debug::RemoveDebugEventListener(fun);

  return Heap::undefined_value();
}


static Object* Runtime_Break(Arguments args) {
  ASSERT(args.length() == 0);
  StackGuard::DebugBreak();
  return Heap::undefined_value();
}


static Object* DebugLookupResultValue(LookupResult* result) {
  Object* value;
  switch (result->type()) {
    case NORMAL: {
      Dictionary* dict =
          JSObject::cast(result->holder())->property_dictionary();
      value = dict->ValueAt(result->GetDictionaryEntry());
      if (value->IsTheHole()) {
        return Heap::undefined_value();
      }
      return value;
    }
    case FIELD:
      value =
          JSObject::cast(
              result->holder())->properties()->get(result->GetFieldIndex());
      if (value->IsTheHole()) {
        return Heap::undefined_value();
      }
      return value;
    case CONSTANT_FUNCTION:
      return result->GetConstantFunction();
    case CALLBACKS:
    case INTERCEPTOR:
    case MAP_TRANSITION:
    case CONSTANT_TRANSITION:
    case NULL_DESCRIPTOR:
      return Heap::undefined_value();
    default:
      UNREACHABLE();
  }
  UNREACHABLE();
  return Heap::undefined_value();
}


static Object* Runtime_DebugGetLocalPropertyDetails(Arguments args) {
  HandleScope scope;

  ASSERT(args.length() == 2);

  CONVERT_ARG_CHECKED(JSObject, obj, 0);
  CONVERT_ARG_CHECKED(String, name, 1);

  // Check if the name is trivially convertible to an index and get the element
  // if so.
  uint32_t index;
  if (name->AsArrayIndex(&index)) {
    Handle<FixedArray> details = Factory::NewFixedArray(2);
    details->set(0, Runtime::GetElementOrCharAt(obj, index));
    details->set(1, PropertyDetails(NONE, NORMAL).AsSmi());
    return *Factory::NewJSArrayWithElements(details);
  }

  // Perform standard local lookup on the object.
  LookupResult result;
  obj->LocalLookup(*name, &result);
  if (result.IsProperty()) {
    Handle<Object> value(DebugLookupResultValue(&result));
    Handle<FixedArray> details = Factory::NewFixedArray(2);
    details->set(0, *value);
    details->set(1, result.GetPropertyDetails().AsSmi());
    return *Factory::NewJSArrayWithElements(details);
  }
  return Heap::undefined_value();
}


static Object* Runtime_DebugGetProperty(Arguments args) {
  HandleScope scope;

  ASSERT(args.length() == 2);

  CONVERT_ARG_CHECKED(JSObject, obj, 0);
  CONVERT_ARG_CHECKED(String, name, 1);

  LookupResult result;
  obj->Lookup(*name, &result);
  if (result.IsProperty()) {
    return DebugLookupResultValue(&result);
  }
  return Heap::undefined_value();
}


// Return the names of the local named properties.
// args[0]: object
static Object* Runtime_DebugLocalPropertyNames(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  if (!args[0]->IsJSObject()) {
    return Heap::undefined_value();
  }
  CONVERT_ARG_CHECKED(JSObject, obj, 0);

  int n = obj->NumberOfLocalProperties(static_cast<PropertyAttributes>(NONE));
  Handle<FixedArray> names = Factory::NewFixedArray(n);
  obj->GetLocalPropertyNames(*names);
  return *Factory::NewJSArrayWithElements(names);
}


// Return the names of the local indexed properties.
// args[0]: object
static Object* Runtime_DebugLocalElementNames(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  if (!args[0]->IsJSObject()) {
    return Heap::undefined_value();
  }
  CONVERT_ARG_CHECKED(JSObject, obj, 0);

  int n = obj->NumberOfLocalElements(static_cast<PropertyAttributes>(NONE));
  Handle<FixedArray> names = Factory::NewFixedArray(n);
  obj->GetLocalElementKeys(*names, static_cast<PropertyAttributes>(NONE));
  return *Factory::NewJSArrayWithElements(names);
}


// Return the property type calculated from the property details.
// args[0]: smi with property details.
static Object* Runtime_DebugPropertyTypeFromDetails(Arguments args) {
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(Smi, details, args[0]);
  PropertyType type = PropertyDetails(details).type();
  return Smi::FromInt(static_cast<int>(type));
}


// Return the property attribute calculated from the property details.
// args[0]: smi with property details.
static Object* Runtime_DebugPropertyAttributesFromDetails(Arguments args) {
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(Smi, details, args[0]);
  PropertyAttributes attributes = PropertyDetails(details).attributes();
  return Smi::FromInt(static_cast<int>(attributes));
}


// Return the property insertion index calculated from the property details.
// args[0]: smi with property details.
static Object* Runtime_DebugPropertyIndexFromDetails(Arguments args) {
  ASSERT(args.length() == 1);
  CONVERT_CHECKED(Smi, details, args[0]);
  int index = PropertyDetails(details).index();
  return Smi::FromInt(index);
}


// Return information on whether an object has a named or indexed interceptor.
// args[0]: object
static Object* Runtime_DebugInterceptorInfo(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  if (!args[0]->IsJSObject()) {
    return Smi::FromInt(0);
  }
  CONVERT_ARG_CHECKED(JSObject, obj, 0);

  int result = 0;
  if (obj->HasNamedInterceptor()) result |= 2;
  if (obj->HasIndexedInterceptor()) result |= 1;

  return Smi::FromInt(result);
}


// Return property names from named interceptor.
// args[0]: object
static Object* Runtime_DebugNamedInterceptorPropertyNames(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  CONVERT_ARG_CHECKED(JSObject, obj, 0);
  RUNTIME_ASSERT(obj->HasNamedInterceptor());

  v8::Handle<v8::Array> result = GetKeysForNamedInterceptor(obj, obj);
  if (!result.IsEmpty()) return *v8::Utils::OpenHandle(*result);
  return Heap::undefined_value();
}


// Return element names from indexed interceptor.
// args[0]: object
static Object* Runtime_DebugIndexedInterceptorElementNames(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  CONVERT_ARG_CHECKED(JSObject, obj, 0);
  RUNTIME_ASSERT(obj->HasIndexedInterceptor());

  v8::Handle<v8::Array> result = GetKeysForIndexedInterceptor(obj, obj);
  if (!result.IsEmpty()) return *v8::Utils::OpenHandle(*result);
  return Heap::undefined_value();
}


// Return property value from named interceptor.
// args[0]: object
// args[1]: property name
static Object* Runtime_DebugNamedInterceptorPropertyValue(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);
  CONVERT_ARG_CHECKED(JSObject, obj, 0);
  RUNTIME_ASSERT(obj->HasNamedInterceptor());
  CONVERT_ARG_CHECKED(String, name, 1);

  PropertyAttributes attributes;
  return obj->GetPropertyWithInterceptor(*obj, *name, &attributes);
}


// Return element value from indexed interceptor.
// args[0]: object
// args[1]: index
static Object* Runtime_DebugIndexedInterceptorElementValue(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);
  CONVERT_ARG_CHECKED(JSObject, obj, 0);
  RUNTIME_ASSERT(obj->HasIndexedInterceptor());
  CONVERT_NUMBER_CHECKED(uint32_t, index, Uint32, args[1]);

  return obj->GetElementWithInterceptor(*obj, index);
}


static Object* Runtime_CheckExecutionState(Arguments args) {
  ASSERT(args.length() >= 1);
  CONVERT_NUMBER_CHECKED(int, break_id, Int32, args[0]);
  // Check that the break id is valid and that there is a valid frame
  // where execution is broken.
  if (break_id != Top::break_id() ||
      Top::break_frame_id() == StackFrame::NO_ID) {
    return Top::Throw(Heap::illegal_execution_state_symbol());
  }

  return Heap::true_value();
}


static Object* Runtime_GetFrameCount(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  // Check arguments.
  Object* result = Runtime_CheckExecutionState(args);
  if (result->IsFailure()) return result;

  // Count all frames which are relevant to debugging stack trace.
  int n = 0;
  StackFrame::Id id = Top::break_frame_id();
  for (JavaScriptFrameIterator it(id); !it.done(); it.Advance()) n++;
  return Smi::FromInt(n);
}


static const int kFrameDetailsFrameIdIndex = 0;
static const int kFrameDetailsReceiverIndex = 1;
static const int kFrameDetailsFunctionIndex = 2;
static const int kFrameDetailsArgumentCountIndex = 3;
static const int kFrameDetailsLocalCountIndex = 4;
static const int kFrameDetailsSourcePositionIndex = 5;
static const int kFrameDetailsConstructCallIndex = 6;
static const int kFrameDetailsDebuggerFrameIndex = 7;
static const int kFrameDetailsFirstDynamicIndex = 8;

// Return an array with frame details
// args[0]: number: break id
// args[1]: number: frame index
//
// The array returned contains the following information:
// 0: Frame id
// 1: Receiver
// 2: Function
// 3: Argument count
// 4: Local count
// 5: Source position
// 6: Constructor call
// 7: Debugger frame
// Arguments name, value
// Locals name, value
static Object* Runtime_GetFrameDetails(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);

  // Check arguments.
  Object* check = Runtime_CheckExecutionState(args);
  if (check->IsFailure()) return check;
  CONVERT_NUMBER_CHECKED(int, index, Int32, args[1]);

  // Find the relevant frame with the requested index.
  StackFrame::Id id = Top::break_frame_id();
  int count = 0;
  JavaScriptFrameIterator it(id);
  for (; !it.done(); it.Advance()) {
    if (count == index) break;
    count++;
  }
  if (it.done()) return Heap::undefined_value();

  // Traverse the saved contexts chain to find the active context for the
  // selected frame.
  SaveContext* save = Top::save_context();
  while (save != NULL && reinterpret_cast<Address>(save) < it.frame()->sp()) {
    save = save->prev();
  }

  // Get the frame id.
  Handle<Object> frame_id(WrapFrameId(it.frame()->id()));

  // Find source position.
  int position = it.frame()->FindCode()->SourcePosition(it.frame()->pc());

  // Check for constructor frame.
  bool constructor = it.frame()->IsConstructor();

  // Get code and read scope info from it for local variable information.
  Handle<Code> code(it.frame()->FindCode());
  ScopeInfo<> info(*code);

  // Get the context.
  Handle<Context> context(Context::cast(it.frame()->context()));

  // Get the locals names and values into a temporary array.
  //
  // TODO(1240907): Hide compiler-introduced stack variables
  // (e.g. .result)?  For users of the debugger, they will probably be
  // confusing.
  Handle<FixedArray> locals = Factory::NewFixedArray(info.NumberOfLocals() * 2);
  for (int i = 0; i < info.NumberOfLocals(); i++) {
    // Name of the local.
    locals->set(i * 2, *info.LocalName(i));

    // Fetch the value of the local - either from the stack or from a
    // heap-allocated context.
    if (i < info.number_of_stack_slots()) {
      locals->set(i * 2 + 1, it.frame()->GetExpression(i));
    } else {
      Handle<String> name = info.LocalName(i);
      // Traverse the context chain to the function context as all local
      // variables stored in the context will be on the function context.
      while (context->previous() != NULL) {
        context = Handle<Context>(context->previous());
      }
      ASSERT(context->is_function_context());
      locals->set(i * 2 + 1,
                  context->get(ScopeInfo<>::ContextSlotIndex(*code, *name,
                                                             NULL)));
    }
  }

  // Now advance to the arguments adapter frame (if any). If contains all
  // the provided parameters and

  // Now advance to the arguments adapter frame (if any). It contains all
  // the provided parameters whereas the function frame always have the number
  // of arguments matching the functions parameters. The rest of the
  // information (except for what is collected above) is the same.
  it.AdvanceToArgumentsFrame();

  // Find the number of arguments to fill. At least fill the number of
  // parameters for the function and fill more if more parameters are provided.
  int argument_count = info.number_of_parameters();
  if (argument_count < it.frame()->GetProvidedParametersCount()) {
    argument_count = it.frame()->GetProvidedParametersCount();
  }

  // Calculate the size of the result.
  int details_size = kFrameDetailsFirstDynamicIndex +
                     2 * (argument_count + info.NumberOfLocals());
  Handle<FixedArray> details = Factory::NewFixedArray(details_size);

  // Add the frame id.
  details->set(kFrameDetailsFrameIdIndex, *frame_id);

  // Add the function (same as in function frame).
  details->set(kFrameDetailsFunctionIndex, it.frame()->function());

  // Add the arguments count.
  details->set(kFrameDetailsArgumentCountIndex, Smi::FromInt(argument_count));

  // Add the locals count
  details->set(kFrameDetailsLocalCountIndex,
               Smi::FromInt(info.NumberOfLocals()));

  // Add the source position.
  if (position != kNoPosition) {
    details->set(kFrameDetailsSourcePositionIndex, Smi::FromInt(position));
  } else {
    details->set(kFrameDetailsSourcePositionIndex, Heap::undefined_value());
  }

  // Add the constructor information.
  details->set(kFrameDetailsConstructCallIndex, Heap::ToBoolean(constructor));

  // Add information on whether this frame is invoked in the debugger context.
  details->set(kFrameDetailsDebuggerFrameIndex,
               Heap::ToBoolean(*save->context() == *Debug::debug_context()));

  // Fill the dynamic part.
  int details_index = kFrameDetailsFirstDynamicIndex;

  // Add arguments name and value.
  for (int i = 0; i < argument_count; i++) {
    // Name of the argument.
    if (i < info.number_of_parameters()) {
      details->set(details_index++, *info.parameter_name(i));
    } else {
      details->set(details_index++, Heap::undefined_value());
    }

    // Parameter value.
    if (i < it.frame()->GetProvidedParametersCount()) {
      details->set(details_index++, it.frame()->GetParameter(i));
    } else {
      details->set(details_index++, Heap::undefined_value());
    }
  }

  // Add locals name and value from the temporary copy from the function frame.
  for (int i = 0; i < info.NumberOfLocals() * 2; i++) {
    details->set(details_index++, locals->get(i));
  }

  // Add the receiver (same as in function frame).
  // THIS MUST BE DONE LAST SINCE WE MIGHT ADVANCE
  // THE FRAME ITERATOR TO WRAP THE RECEIVER.
  Handle<Object> receiver(it.frame()->receiver());
  if (!receiver->IsJSObject()) {
    // If the receiver is NOT a JSObject we have hit an optimization
    // where a value object is not converted into a wrapped JS objects.
    // To hide this optimization from the debugger, we wrap the receiver
    // by creating correct wrapper object based on the calling frame's
    // global context.
    it.Advance();
    Handle<Context> calling_frames_global_context(
        Context::cast(Context::cast(it.frame()->context())->global_context()));
    receiver = Factory::ToObject(receiver, calling_frames_global_context);
  }
  details->set(kFrameDetailsReceiverIndex, *receiver);

  ASSERT_EQ(details_size, details_index);
  return *Factory::NewJSArrayWithElements(details);
}


static Object* Runtime_GetCFrames(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  Object* result = Runtime_CheckExecutionState(args);
  if (result->IsFailure()) return result;

  static const int kMaxCFramesSize = 200;
  OS::StackFrame frames[kMaxCFramesSize];
  int frames_count = OS::StackWalk(frames, kMaxCFramesSize);
  if (frames_count == OS::kStackWalkError) {
    return Heap::undefined_value();
  }

  Handle<String> address_str = Factory::LookupAsciiSymbol("address");
  Handle<String> text_str = Factory::LookupAsciiSymbol("text");
  Handle<FixedArray> frames_array = Factory::NewFixedArray(frames_count);
  for (int i = 0; i < frames_count; i++) {
    Handle<JSObject> frame_value = Factory::NewJSObject(Top::object_function());
    frame_value->SetProperty(
        *address_str,
        *Factory::NewNumberFromInt(reinterpret_cast<int>(frames[i].address)),
        NONE);

    // Get the stack walk text for this frame.
    Handle<String> frame_text;
    if (strlen(frames[i].text) > 0) {
      Vector<const char> str(frames[i].text, strlen(frames[i].text));
      frame_text = Factory::NewStringFromAscii(str);
    }

    if (!frame_text.is_null()) {
      frame_value->SetProperty(*text_str, *frame_text, NONE);
    }

    frames_array->set(i, *frame_value);
  }
  return *Factory::NewJSArrayWithElements(frames_array);
}


static Object* Runtime_GetBreakLocations(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);

  CONVERT_ARG_CHECKED(JSFunction, raw_fun, 0);
  Handle<SharedFunctionInfo> shared(raw_fun->shared());
  // Find the number of break points
  Handle<Object> break_locations = Debug::GetSourceBreakLocations(shared);
  if (break_locations->IsUndefined()) return Heap::undefined_value();
  // Return array as JS array
  return *Factory::NewJSArrayWithElements(
      Handle<FixedArray>::cast(break_locations));
}


// Set a break point in a function
// args[0]: function
// args[1]: number: break source position (within the function source)
// args[2]: number: break point object
static Object* Runtime_SetFunctionBreakPoint(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);
  CONVERT_ARG_CHECKED(JSFunction, raw_fun, 0);
  Handle<SharedFunctionInfo> shared(raw_fun->shared());
  CONVERT_NUMBER_CHECKED(int32_t, source_position, Int32, args[1]);
  RUNTIME_ASSERT(source_position >= 0);
  Handle<Object> break_point_object_arg = args.at<Object>(2);

  // Set break point.
  Debug::SetBreakPoint(shared, source_position, break_point_object_arg);

  return Heap::undefined_value();
}


static Object* FindSharedFunctionInfoInScript(Handle<Script> script,
                                              int position) {
  // Iterate the heap looking for SharedFunctionInfo generated from the
  // script. The inner most SharedFunctionInfo containing the source position
  // for the requested break point is found.
  // NOTE: This might reqire several heap iterations. If the SharedFunctionInfo
  // which is found is not compiled it is compiled and the heap is iterated
  // again as the compilation might create inner functions from the newly
  // compiled function and the actual requested break point might be in one of
  // these functions.
  bool done = false;
  // The current candidate for the source position:
  int target_start_position = kNoPosition;
  Handle<SharedFunctionInfo> target;
  // The current candidate for the last function in script:
  Handle<SharedFunctionInfo> last;
  while (!done) {
    HeapIterator iterator;
    while (iterator.has_next()) {
      HeapObject* obj = iterator.next();
      ASSERT(obj != NULL);
      if (obj->IsSharedFunctionInfo()) {
        Handle<SharedFunctionInfo> shared(SharedFunctionInfo::cast(obj));
        if (shared->script() == *script) {
          // If the SharedFunctionInfo found has the requested script data and
          // contains the source position it is a candidate.
          int start_position = shared->function_token_position();
          if (start_position == kNoPosition) {
            start_position = shared->start_position();
          }
          if (start_position <= position &&
              position <= shared->end_position()) {
            // If there is no candidate or this function is within the currrent
            // candidate this is the new candidate.
            if (target.is_null()) {
              target_start_position = start_position;
              target = shared;
            } else {
              if (target_start_position < start_position &&
                  shared->end_position() < target->end_position()) {
                target_start_position = start_position;
                target = shared;
              }
            }
          }

          // Keep track of the last function in the script.
          if (last.is_null() ||
              shared->end_position() > last->start_position()) {
            last = shared;
          }
        }
      }
    }

    // Make sure some candidate is selected.
    if (target.is_null()) {
      if (!last.is_null()) {
        // Position after the last function - use last.
        target = last;
      } else {
        // Unable to find function - possibly script without any function.
        return Heap::undefined_value();
      }
    }

    // If the candidate found is compiled we are done. NOTE: when lazy
    // compilation of inner functions is introduced some additional checking
    // needs to be done here to compile inner functions.
    done = target->is_compiled();
    if (!done) {
      // If the candidate is not compiled compile it to reveal any inner
      // functions which might contain the requested source position.
      CompileLazyShared(target, KEEP_EXCEPTION);
    }
  }

  return *target;
}


// Change the state of a break point in a script. NOTE: Regarding performance
// see the NOTE for GetScriptFromScriptData.
// args[0]: script to set break point in
// args[1]: number: break source position (within the script source)
// args[2]: number: break point object
static Object* Runtime_SetScriptBreakPoint(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);
  CONVERT_ARG_CHECKED(JSValue, wrapper, 0);
  CONVERT_NUMBER_CHECKED(int32_t, source_position, Int32, args[1]);
  RUNTIME_ASSERT(source_position >= 0);
  Handle<Object> break_point_object_arg = args.at<Object>(2);

  // Get the script from the script wrapper.
  RUNTIME_ASSERT(wrapper->value()->IsScript());
  Handle<Script> script(Script::cast(wrapper->value()));

  Object* result = FindSharedFunctionInfoInScript(script, source_position);
  if (!result->IsUndefined()) {
    Handle<SharedFunctionInfo> shared(SharedFunctionInfo::cast(result));
    // Find position within function. The script position might be before the
    // source position of the first function.
    int position;
    if (shared->start_position() > source_position) {
      position = 0;
    } else {
      position = source_position - shared->start_position();
    }
    Debug::SetBreakPoint(shared, position, break_point_object_arg);
  }
  return  Heap::undefined_value();
}


// Clear a break point
// args[0]: number: break point object
static Object* Runtime_ClearBreakPoint(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 1);
  Handle<Object> break_point_object_arg = args.at<Object>(0);

  // Clear break point.
  Debug::ClearBreakPoint(break_point_object_arg);

  return Heap::undefined_value();
}


// Change the state of break on exceptions
// args[0]: boolean indicating uncaught exceptions
// args[1]: boolean indicating on/off
static Object* Runtime_ChangeBreakOnException(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 2);
  ASSERT(args[0]->IsNumber());
  ASSERT(args[1]->IsBoolean());

  // Update break point state
  ExceptionBreakType type =
      static_cast<ExceptionBreakType>(NumberToUint32(args[0]));
  bool enable = args[1]->ToBoolean()->IsTrue();
  Debug::ChangeBreakOnException(type, enable);
  return Heap::undefined_value();
}


// Prepare for stepping
// args[0]: break id for checking execution state
// args[1]: step action from the enumeration StepAction
// args[2]: number of times to perform the step
static Object* Runtime_PrepareStep(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 3);
  // Check arguments.
  Object* check = Runtime_CheckExecutionState(args);
  if (check->IsFailure()) return check;
  if (!args[1]->IsNumber() || !args[2]->IsNumber()) {
    return Top::Throw(Heap::illegal_argument_symbol());
  }

  // Get the step action and check validity.
  StepAction step_action = static_cast<StepAction>(NumberToInt32(args[1]));
  if (step_action != StepIn &&
      step_action != StepNext &&
      step_action != StepOut &&
      step_action != StepInMin &&
      step_action != StepMin) {
    return Top::Throw(Heap::illegal_argument_symbol());
  }

  // Get the number of steps.
  int step_count = NumberToInt32(args[2]);
  if (step_count < 1) {
    return Top::Throw(Heap::illegal_argument_symbol());
  }

  // Prepare step.
  Debug::PrepareStep(static_cast<StepAction>(step_action), step_count);
  return Heap::undefined_value();
}


// Clear all stepping set by PrepareStep.
static Object* Runtime_ClearStepping(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 0);
  Debug::ClearStepping();
  return Heap::undefined_value();
}


// Creates a copy of the with context chain. The copy of the context chain is
// is linked to the function context supplied.
static Handle<Context> CopyWithContextChain(Handle<Context> context_chain,
                                            Handle<Context> function_context) {
  // At the bottom of the chain. Return the function context to link to.
  if (context_chain->is_function_context()) {
    return function_context;
  }

  // Recursively copy the with contexts.
  Handle<Context> previous(context_chain->previous());
  Handle<JSObject> extension(JSObject::cast(context_chain->extension()));
  return Factory::NewWithContext(
      CopyWithContextChain(function_context, previous), extension);
}


// Helper function to find or create the arguments object for
// Runtime_DebugEvaluate.
static Handle<Object> GetArgumentsObject(JavaScriptFrame* frame,
                                         Handle<JSFunction> function,
                                         Handle<Code> code,
                                         const ScopeInfo<>* sinfo,
                                         Handle<Context> function_context) {
  // Try to find the value of 'arguments' to pass as parameter. If it is not
  // found (that is the debugged function does not reference 'arguments' and
  // does not support eval) then create an 'arguments' object.
  int index;
  if (sinfo->number_of_stack_slots() > 0) {
    index = ScopeInfo<>::StackSlotIndex(*code, Heap::arguments_symbol());
    if (index != -1) {
      return Handle<Object>(frame->GetExpression(index));
    }
  }

  if (sinfo->number_of_context_slots() > Context::MIN_CONTEXT_SLOTS) {
    index = ScopeInfo<>::ContextSlotIndex(*code, Heap::arguments_symbol(),
                                          NULL);
    if (index != -1) {
      return Handle<Object>(function_context->get(index));
    }
  }

  const int length = frame->GetProvidedParametersCount();
  Handle<Object> arguments = Factory::NewArgumentsObject(function, length);
  FixedArray* array = FixedArray::cast(JSObject::cast(*arguments)->elements());
  ASSERT(array->length() == length);
  for (int i = 0; i < length; i++) {
    array->set(i, frame->GetParameter(i));
  }
  return arguments;
}


// Evaluate a piece of JavaScript in the context of a stack frame for
// debugging. This is acomplished by creating a new context which in its
// extension part has all the parameters and locals of the function on the
// stack frame. A function which calls eval with the code to evaluate is then
// compiled in this context and called in this context. As this context
// replaces the context of the function on the stack frame a new (empty)
// function is created as well to be used as the closure for the context.
// This function and the context acts as replacements for the function on the
// stack frame presenting the same view of the values of parameters and
// local variables as if the piece of JavaScript was evaluated at the point
// where the function on the stack frame is currently stopped.
static Object* Runtime_DebugEvaluate(Arguments args) {
  HandleScope scope;

  // Check the execution state and decode arguments frame and source to be
  // evaluated.
  ASSERT(args.length() == 4);
  Object* check_result = Runtime_CheckExecutionState(args);
  if (check_result->IsFailure()) return check_result;
  CONVERT_CHECKED(Smi, wrapped_id, args[1]);
  CONVERT_ARG_CHECKED(String, source, 2);
  CONVERT_BOOLEAN_CHECKED(disable_break, args[3]);

  // Handle the processing of break.
  DisableBreak disable_break_save(disable_break);

  // Get the frame where the debugging is performed.
  StackFrame::Id id = UnwrapFrameId(wrapped_id);
  JavaScriptFrameIterator it(id);
  JavaScriptFrame* frame = it.frame();
  Handle<JSFunction> function(JSFunction::cast(frame->function()));
  Handle<Code> code(function->code());
  ScopeInfo<> sinfo(*code);

  // Traverse the saved contexts chain to find the active context for the
  // selected frame.
  SaveContext* save = Top::save_context();
  while (save != NULL && reinterpret_cast<Address>(save) < frame->sp()) {
    save = save->prev();
  }
  ASSERT(save != NULL);
  SaveContext savex;
  Top::set_context(*(save->context()));
  Top::set_security_context(*(save->security_context()));

  // Create the (empty) function replacing the function on the stack frame for
  // the purpose of evaluating in the context created below. It is important
  // that this function does not describe any parameters and local variables
  // in the context. If it does then this will cause problems with the lookup
  // in Context::Lookup, where context slots for parameters and local variables
  // are looked at before the extension object.
  Handle<JSFunction> go_between =
      Factory::NewFunction(Factory::empty_string(), Factory::undefined_value());
  go_between->set_context(function->context());
#ifdef DEBUG
  ScopeInfo<> go_between_sinfo(go_between->shared()->code());
  ASSERT(go_between_sinfo.number_of_parameters() == 0);
  ASSERT(go_between_sinfo.number_of_context_slots() == 0);
#endif

  // Allocate and initialize a context extension object with all the
  // arguments, stack locals heap locals and extension properties of the
  // debugged function.
  Handle<JSObject> context_ext = Factory::NewJSObject(Top::object_function());
  // First fill all parameters to the context extension.
  for (int i = 0; i < sinfo.number_of_parameters(); ++i) {
    SetProperty(context_ext,
                sinfo.parameter_name(i),
                Handle<Object>(frame->GetParameter(i)), NONE);
  }
  // Second fill all stack locals to the context extension.
  for (int i = 0; i < sinfo.number_of_stack_slots(); i++) {
    SetProperty(context_ext,
                sinfo.stack_slot_name(i),
                Handle<Object>(frame->GetExpression(i)), NONE);
  }
  // Third fill all context locals to the context extension.
  Handle<Context> frame_context(Context::cast(frame->context()));
  Handle<Context> function_context(frame_context->fcontext());
  for (int i = Context::MIN_CONTEXT_SLOTS;
       i < sinfo.number_of_context_slots();
       ++i) {
    int context_index =
        ScopeInfo<>::ContextSlotIndex(*code, *sinfo.context_slot_name(i), NULL);
    SetProperty(context_ext,
                sinfo.context_slot_name(i),
                Handle<Object>(function_context->get(context_index)), NONE);
  }
  // Finally copy any properties from the function context extension. This will
  // be variables introduced by eval.
  if (function_context->extension() != NULL &&
      !function_context->IsGlobalContext()) {
    Handle<JSObject> ext(JSObject::cast(function_context->extension()));
    Handle<FixedArray> keys = GetKeysInFixedArrayFor(ext);
    for (int i = 0; i < keys->length(); i++) {
      // Names of variables introduced by eval are strings.
      ASSERT(keys->get(i)->IsString());
      Handle<String> key(String::cast(keys->get(i)));
      SetProperty(context_ext, key, GetProperty(ext, key), NONE);
    }
  }

  // Allocate a new context for the debug evaluation and set the extension
  // object build.
  Handle<Context> context =
      Factory::NewFunctionContext(Context::MIN_CONTEXT_SLOTS, go_between);
  context->set_extension(*context_ext);
  // Copy any with contexts present and chain them in front of this context.
  context = CopyWithContextChain(frame_context, context);

  // Wrap the evaluation statement in a new function compiled in the newly
  // created context. The function has one parameter which has to be called
  // 'arguments'. This it to have access to what would have been 'arguments' in
  // the function beeing debugged.
  // function(arguments,__source__) {return eval(__source__);}
  static const char* source_str =
      "function(arguments,__source__){return eval(__source__);}";
  static const int source_str_length = strlen(source_str);
  Handle<String> function_source =
      Factory::NewStringFromAscii(Vector<const char>(source_str,
                                                     source_str_length));
  Handle<JSFunction> boilerplate =
      Compiler::CompileEval(context->IsGlobalContext(), function_source);
  if (boilerplate.is_null()) return Failure::Exception();
  Handle<JSFunction> compiled_function =
      Factory::NewFunctionFromBoilerplate(boilerplate, context);

  // Invoke the result of the compilation to get the evaluation function.
  bool has_pending_exception;
  Handle<Object> receiver(frame->receiver());
  Handle<Object> evaluation_function =
      Execution::Call(compiled_function, receiver, 0, NULL,
                      &has_pending_exception);

  Handle<Object> arguments = GetArgumentsObject(frame, function, code, &sinfo,
                                                function_context);

  // Invoke the evaluation function and return the result.
  const int argc = 2;
  Object** argv[argc] = { arguments.location(),
                          Handle<Object>::cast(source).location() };
  Handle<Object> result =
      Execution::Call(Handle<JSFunction>::cast(evaluation_function), receiver,
                      argc, argv, &has_pending_exception);
  return *result;
}


static Object* Runtime_DebugEvaluateGlobal(Arguments args) {
  HandleScope scope;

  // Check the execution state and decode arguments frame and source to be
  // evaluated.
  ASSERT(args.length() == 3);
  Object* check_result = Runtime_CheckExecutionState(args);
  if (check_result->IsFailure()) return check_result;
  CONVERT_ARG_CHECKED(String, source, 1);
  CONVERT_BOOLEAN_CHECKED(disable_break, args[2]);

  // Handle the processing of break.
  DisableBreak disable_break_save(disable_break);

  // Enter the top context from before the debugger was invoked.
  SaveContext save;
  SaveContext* top = &save;
  while (top != NULL && *top->context() == *Debug::debug_context()) {
    top = top->prev();
  }
  if (top != NULL) {
    Top::set_context(*top->context());
    Top::set_security_context(*top->security_context());
  }

  // Get the global context now set to the top context from before the
  // debugger was invoked.
  Handle<Context> context = Top::global_context();

  // Compile the source to be evaluated.
  Handle<JSFunction> boilerplate(Compiler::CompileEval(true, source));
  if (boilerplate.is_null()) return Failure::Exception();
  Handle<JSFunction> compiled_function =
      Handle<JSFunction>(Factory::NewFunctionFromBoilerplate(boilerplate,
                                                             context));

  // Invoke the result of the compilation to get the evaluation function.
  bool has_pending_exception;
  Handle<Object> receiver = Top::global();
  Handle<Object> result =
    Execution::Call(compiled_function, receiver, 0, NULL,
                    &has_pending_exception);
  return *result;
}


// Helper function used by Runtime_DebugGetLoadedScripts below.
static int DebugGetLoadedScripts(FixedArray* instances, int instances_size) {
  NoHandleAllocation ha;
  AssertNoAllocation no_alloc;

  // Get hold of the current empty script.
  Context* context = Top::context()->global_context();
  Script* empty = context->empty_script();

  // Scan heap for Script objects.
  int count = 0;
  HeapIterator iterator;
  while (iterator.has_next()) {
    HeapObject* obj = iterator.next();
    ASSERT(obj != NULL);
    if (obj->IsScript() && obj != empty) {
      if (instances != NULL && count < instances_size) {
        instances->set(count, obj);
      }
      count++;
    }
  }

  return count;
}


static Object* Runtime_DebugGetLoadedScripts(Arguments args) {
  HandleScope scope;
  ASSERT(args.length() == 0);

  // Perform two GCs to get rid of all unreferenced scripts. The first GC gets
  // rid of all the cached script wrappes and the second gets rid of the
  // scripts which is no longer referenced.
  Heap::CollectGarbage(0, OLD_SPACE);
  Heap::CollectGarbage(0, OLD_SPACE);

  // Get the number of scripts.
  int count;
  count = DebugGetLoadedScripts(NULL, 0);

  // Allocate an array to hold the result.
  Handle<FixedArray> instances = Factory::NewFixedArray(count);

  // Fill the script objects.
  count = DebugGetLoadedScripts(*instances, count);

  // Convert the script objects to proper JS objects.
  for (int i = 0; i < count; i++) {
    Handle<Script> script(Script::cast(instances->get(i)));
    instances->set(i, *GetScriptWrapper(script));
  }

  // Return result as a JS array.
  Handle<JSObject> result = Factory::NewJSObject(Top::array_function());
  Handle<JSArray>::cast(result)->SetContent(*instances);
  return *result;
}


// Helper function used by Runtime_DebugReferencedBy below.
static int DebugReferencedBy(JSObject* target,
                             Object* instance_filter, int max_references,
                             FixedArray* instances, int instances_size,
                             JSFunction* context_extension_function,
                             JSFunction* arguments_function) {
  NoHandleAllocation ha;
  AssertNoAllocation no_alloc;

  // Iterate the heap.
  int count = 0;
  JSObject* last = NULL;
  HeapIterator iterator;
  while (iterator.has_next() &&
         (max_references == 0 || count < max_references)) {
    // Only look at all JSObjects.
    HeapObject* heap_obj = iterator.next();
    if (heap_obj->IsJSObject()) {
      // Skip context extension objects and argument arrays as these are
      // checked in the context of functions using them.
      JSObject* obj = JSObject::cast(heap_obj);
      if (obj->map()->constructor() == context_extension_function ||
          obj->map()->constructor() == arguments_function) {
        continue;
      }

      // Check if the JS object has a reference to the object looked for.
      if (obj->ReferencesObject(target)) {
        // Check instance filter if supplied. This is normally used to avoid
        // references from mirror objects (see Runtime_IsInPrototypeChain).
        if (!instance_filter->IsUndefined()) {
          Object* V = obj;
          while (true) {
            Object* prototype = V->GetPrototype();
            if (prototype->IsNull()) {
              break;
            }
            if (instance_filter == prototype) {
              obj = NULL;  // Don't add this object.
              break;
            }
            V = prototype;
          }
        }

        if (obj != NULL) {
          // Valid reference found add to instance array if supplied an update
          // count.
          if (instances != NULL && count < instances_size) {
            instances->set(count, obj);
          }
          last = obj;
          count++;
        }
      }
    }
  }

  // Check for circular reference only. This can happen when the object is only
  // referenced from mirrors and has a circular reference in which case the
  // object is not really alive and would have been garbage collected if not
  // referenced from the mirror.
  if (count == 1 && last == target) {
    count = 0;
  }

  // Return the number of referencing objects found.
  return count;
}


// Scan the heap for objects with direct references to an object
// args[0]: the object to find references to
// args[1]: constructor function for instances to exclude (Mirror)
// args[2]: the the maximum number of objects to return
static Object* Runtime_DebugReferencedBy(Arguments args) {
  ASSERT(args.length() == 3);

  // First perform a full GC in order to avoid references from dead objects.
  Heap::CollectGarbage(0, OLD_SPACE);

  // Check parameters.
  CONVERT_CHECKED(JSObject, target, args[0]);
  Object* instance_filter = args[1];
  RUNTIME_ASSERT(instance_filter->IsUndefined() ||
                 instance_filter->IsJSObject());
  CONVERT_NUMBER_CHECKED(int32_t, max_references, Int32, args[2]);
  RUNTIME_ASSERT(max_references >= 0);

  // Get the constructor function for context extension and arguments array.
  JSFunction* context_extension_function =
      Top::context()->global_context()->context_extension_function();
  JSObject* arguments_boilerplate =
      Top::context()->global_context()->arguments_boilerplate();
  JSFunction* arguments_function =
      JSFunction::cast(arguments_boilerplate->map()->constructor());

  // Get the number of referencing objects.
  int count;
  count = DebugReferencedBy(target, instance_filter, max_references,
                            NULL, 0,
                            context_extension_function, arguments_function);

  // Allocate an array to hold the result.
  Object* object = Heap::AllocateFixedArray(count);
  if (object->IsFailure()) return object;
  FixedArray* instances = FixedArray::cast(object);

  // Fill the referencing objects.
  count = DebugReferencedBy(target, instance_filter, max_references,
                            instances, count,
                            context_extension_function, arguments_function);

  // Return result as JS array.
  Object* result =
      Heap::AllocateJSObject(
          Top::context()->global_context()->array_function());
  if (!result->IsFailure()) JSArray::cast(result)->SetContent(instances);
  return result;
}


// Helper function used by Runtime_DebugConstructedBy below.
static int DebugConstructedBy(JSFunction* constructor, int max_references,
                              FixedArray* instances, int instances_size) {
  AssertNoAllocation no_alloc;

  // Iterate the heap.
  int count = 0;
  HeapIterator iterator;
  while (iterator.has_next() &&
         (max_references == 0 || count < max_references)) {
    // Only look at all JSObjects.
    HeapObject* heap_obj = iterator.next();
    if (heap_obj->IsJSObject()) {
      JSObject* obj = JSObject::cast(heap_obj);
      if (obj->map()->constructor() == constructor) {
        // Valid reference found add to instance array if supplied an update
        // count.
        if (instances != NULL && count < instances_size) {
          instances->set(count, obj);
        }
        count++;
      }
    }
  }

  // Return the number of referencing objects found.
  return count;
}


// Scan the heap for objects constructed by a specific function.
// args[0]: the constructor to find instances of
// args[1]: the the maximum number of objects to return
static Object* Runtime_DebugConstructedBy(Arguments args) {
  ASSERT(args.length() == 2);

  // First perform a full GC in order to avoid dead objects.
  Heap::CollectGarbage(0, OLD_SPACE);

  // Check parameters.
  CONVERT_CHECKED(JSFunction, constructor, args[0]);
  CONVERT_NUMBER_CHECKED(int32_t, max_references, Int32, args[1]);
  RUNTIME_ASSERT(max_references >= 0);

  // Get the number of referencing objects.
  int count;
  count = DebugConstructedBy(constructor, max_references, NULL, 0);

  // Allocate an array to hold the result.
  Object* object = Heap::AllocateFixedArray(count);
  if (object->IsFailure()) return object;
  FixedArray* instances = FixedArray::cast(object);

  // Fill the referencing objects.
  count = DebugConstructedBy(constructor, max_references, instances, count);

  // Return result as JS array.
  Object* result =
      Heap::AllocateJSObject(
          Top::context()->global_context()->array_function());
  if (!result->IsFailure()) JSArray::cast(result)->SetContent(instances);
  return result;
}


static Object* Runtime_GetPrototype(Arguments args) {
  ASSERT(args.length() == 1);

  CONVERT_CHECKED(JSObject, obj, args[0]);

  return obj->GetPrototype();
}


static Object* Runtime_SystemBreak(Arguments args) {
  ASSERT(args.length() == 0);
  CPU::DebugBreak();
  return Heap::undefined_value();
}


// Finds the script object from the script data. NOTE: This operation uses
// heap traversal to find the function generated for the source position
// for the requested break point. For lazily compiled functions several heap
// traversals might be required rendering this operation as a rather slow
// operation. However for setting break points which is normally done through
// some kind of user interaction the performance is not crucial.
static Handle<Object> Runtime_GetScriptFromScriptName(
    Handle<String> script_name) {
  // Scan the heap for Script objects to find the script with the requested
  // script data.
  Handle<Script> script;
  HeapIterator iterator;
  while (script.is_null() && iterator.has_next()) {
    HeapObject* obj = iterator.next();
    // If a script is found check if it has the script data requested.
    if (obj->IsScript()) {
      if (Script::cast(obj)->name()->IsString()) {
        if (String::cast(Script::cast(obj)->name())->Equals(*script_name)) {
          script = Handle<Script>(Script::cast(obj));
        }
      }
    }
  }

  // If no script with the requested script data is found return undefined.
  if (script.is_null()) return Factory::undefined_value();

  // Return the script found.
  return GetScriptWrapper(script);
}


// Get the script object from script data. NOTE: Regarding performance
// see the NOTE for GetScriptFromScriptData.
// args[0]: script data for the script to find the source for
static Object* Runtime_GetScript(Arguments args) {
  HandleScope scope;

  ASSERT(args.length() == 1);

  CONVERT_CHECKED(String, script_name, args[0]);

  // Find the requested script.
  Handle<Object> result =
      Runtime_GetScriptFromScriptName(Handle<String>(script_name));
  return *result;
}


static Object* Runtime_FunctionGetAssemblerCode(Arguments args) {
#ifdef DEBUG
  HandleScope scope;
  ASSERT(args.length() == 1);
  // Get the function and make sure it is compiled.
  CONVERT_ARG_CHECKED(JSFunction, func, 0);
  if (!func->is_compiled() && !CompileLazy(func, KEEP_EXCEPTION)) {
    return Failure::Exception();
  }
  func->code()->PrintLn();
#endif  // DEBUG
  return Heap::undefined_value();
}


static Object* Runtime_Abort(Arguments args) {
  ASSERT(args.length() == 2);
  OS::PrintError("abort: %s\n", reinterpret_cast<char*>(args[0]) +
                                    Smi::cast(args[1])->value());
  Top::PrintStack();
  OS::Abort();
  UNREACHABLE();
  return NULL;
}


#ifdef DEBUG
// ListNatives is ONLY used by the fuzz-natives.js in debug mode
// Exclude the code in release mode.
static Object* Runtime_ListNatives(Arguments args) {
  ASSERT(args.length() == 0);
  HandleScope scope;
  Handle<JSArray> result = Factory::NewJSArray(0);
  int index = 0;
#define ADD_ENTRY(Name, argc)                                                \
  {                                                                          \
    HandleScope inner;                                                       \
    Handle<String> name =                                                    \
      Factory::NewStringFromAscii(Vector<const char>(#Name, strlen(#Name))); \
    Handle<JSArray> pair = Factory::NewJSArray(0);                           \
    SetElement(pair, 0, name);                                               \
    SetElement(pair, 1, Handle<Smi>(Smi::FromInt(argc)));                    \
    SetElement(result, index++, pair);                                       \
  }
  RUNTIME_FUNCTION_LIST(ADD_ENTRY)
#undef ADD_ENTRY
  return *result;
}
#endif


static Object* Runtime_IS_VAR(Arguments args) {
  UNREACHABLE();  // implemented as macro in the parser
  return NULL;
}


// ----------------------------------------------------------------------------
// Implementation of Runtime

#define F(name, nargs)                                                 \
  { #name, "RuntimeStub_" #name, FUNCTION_ADDR(Runtime_##name), nargs, \
    static_cast<int>(Runtime::k##name) },

static Runtime::Function Runtime_functions[] = {
  RUNTIME_FUNCTION_LIST(F)
  { NULL, NULL, NULL, 0, -1 }
};

#undef F


Runtime::Function* Runtime::FunctionForId(FunctionId fid) {
  ASSERT(0 <= fid && fid < kNofFunctions);
  return &Runtime_functions[fid];
}


Runtime::Function* Runtime::FunctionForName(const char* name) {
  for (Function* f = Runtime_functions; f->name != NULL; f++) {
    if (strcmp(f->name, name) == 0) {
      return f;
    }
  }
  return NULL;
}


void Runtime::PerformGC(Object* result) {
  Failure* failure = Failure::cast(result);
  // Try to do a garbage collection; ignore it if it fails. The C
  // entry stub will throw an out-of-memory exception in that case.
  Heap::CollectGarbage(failure->requested(), failure->allocation_space());
}


} }  // namespace v8::internal
