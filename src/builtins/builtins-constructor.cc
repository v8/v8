// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-constructor.h"
#include "src/builtins/builtins-utils.h"
#include "src/builtins/builtins.h"
#include "src/code-factory.h"
#include "src/code-stub-assembler.h"
#include "src/interface-descriptors.h"

namespace v8 {
namespace internal {

typedef compiler::Node Node;

Node* ConstructorBuiltinsAssembler::EmitFastNewClosure(Node* shared_info,
                                                       Node* context) {
  typedef compiler::CodeAssembler::Label Label;
  typedef compiler::CodeAssembler::Variable Variable;

  Isolate* isolate = this->isolate();
  Factory* factory = isolate->factory();
  IncrementCounter(isolate->counters()->fast_new_closure_total(), 1);

  // Create a new closure from the given function info in new space
  Node* result = Allocate(JSFunction::kSize);

  // Calculate the index of the map we should install on the function based on
  // the FunctionKind and LanguageMode of the function.
  // Note: Must be kept in sync with Context::FunctionMapIndex
  Node* compiler_hints =
      LoadObjectField(shared_info, SharedFunctionInfo::kCompilerHintsOffset,
                      MachineType::Uint32());
  Node* is_strict = Word32And(
      compiler_hints, Int32Constant(1 << SharedFunctionInfo::kStrictModeBit));

  Label if_normal(this), if_generator(this), if_async(this),
      if_class_constructor(this), if_function_without_prototype(this),
      load_map(this);
  Variable map_index(this, MachineType::PointerRepresentation());

  STATIC_ASSERT(FunctionKind::kNormalFunction == 0);
  Node* is_not_normal =
      Word32And(compiler_hints,
                Int32Constant(SharedFunctionInfo::kAllFunctionKindBitsMask));
  GotoUnless(is_not_normal, &if_normal);

  Node* is_generator = Word32And(
      compiler_hints, Int32Constant(FunctionKind::kGeneratorFunction
                                    << SharedFunctionInfo::kFunctionKindShift));
  GotoIf(is_generator, &if_generator);

  Node* is_async = Word32And(
      compiler_hints, Int32Constant(FunctionKind::kAsyncFunction
                                    << SharedFunctionInfo::kFunctionKindShift));
  GotoIf(is_async, &if_async);

  Node* is_class_constructor = Word32And(
      compiler_hints, Int32Constant(FunctionKind::kClassConstructor
                                    << SharedFunctionInfo::kFunctionKindShift));
  GotoIf(is_class_constructor, &if_class_constructor);

  if (FLAG_debug_code) {
    // Function must be a function without a prototype.
    CSA_ASSERT(
        this,
        Word32And(compiler_hints,
                  Int32Constant((FunctionKind::kAccessorFunction |
                                 FunctionKind::kArrowFunction |
                                 FunctionKind::kConciseMethod)
                                << SharedFunctionInfo::kFunctionKindShift)));
  }
  Goto(&if_function_without_prototype);

  Bind(&if_normal);
  {
    map_index.Bind(SelectIntPtrConstant(is_strict,
                                        Context::STRICT_FUNCTION_MAP_INDEX,
                                        Context::SLOPPY_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_generator);
  {
    map_index.Bind(SelectIntPtrConstant(
        is_strict, Context::STRICT_GENERATOR_FUNCTION_MAP_INDEX,
        Context::SLOPPY_GENERATOR_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_async);
  {
    map_index.Bind(SelectIntPtrConstant(
        is_strict, Context::STRICT_ASYNC_FUNCTION_MAP_INDEX,
        Context::SLOPPY_ASYNC_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_class_constructor);
  {
    map_index.Bind(IntPtrConstant(Context::CLASS_FUNCTION_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&if_function_without_prototype);
  {
    map_index.Bind(
        IntPtrConstant(Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX));
    Goto(&load_map);
  }

  Bind(&load_map);

  // Get the function map in the current native context and set that
  // as the map of the allocated object.
  Node* native_context = LoadNativeContext(context);
  Node* map_slot_value =
      LoadFixedArrayElement(native_context, map_index.value());
  StoreMapNoWriteBarrier(result, map_slot_value);

  // Initialize the rest of the function.
  Node* empty_fixed_array = HeapConstant(factory->empty_fixed_array());
  Node* empty_literals_array = HeapConstant(factory->empty_literals_array());
  StoreObjectFieldNoWriteBarrier(result, JSObject::kPropertiesOffset,
                                 empty_fixed_array);
  StoreObjectFieldNoWriteBarrier(result, JSObject::kElementsOffset,
                                 empty_fixed_array);
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kLiteralsOffset,
                                 empty_literals_array);
  StoreObjectFieldNoWriteBarrier(
      result, JSFunction::kPrototypeOrInitialMapOffset, TheHoleConstant());
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kSharedFunctionInfoOffset,
                                 shared_info);
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kContextOffset, context);
  Handle<Code> lazy_builtin_handle(
      isolate->builtins()->builtin(Builtins::kCompileLazy));
  Node* lazy_builtin = HeapConstant(lazy_builtin_handle);
  Node* lazy_builtin_entry =
      IntPtrAdd(BitcastTaggedToWord(lazy_builtin),
                IntPtrConstant(Code::kHeaderSize - kHeapObjectTag));
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kCodeEntryOffset,
                                 lazy_builtin_entry,
                                 MachineType::PointerRepresentation());
  StoreObjectFieldNoWriteBarrier(result, JSFunction::kNextFunctionLinkOffset,
                                 UndefinedConstant());

  return result;
}

TF_BUILTIN(FastNewClosure, ConstructorBuiltinsAssembler) {
  Node* shared = Parameter(FastNewClosureDescriptor::kSharedFunctionInfo);
  Node* context = Parameter(FastNewClosureDescriptor::kContext);
  Return(EmitFastNewClosure(shared, context));
}

}  // namespace internal
}  // namespace v8
