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

#include "ast.h"
#include "compiler.h"
#include "ic.h"
#include "macro-assembler.h"
#include "stub-cache.h"
#include "type-info.h"

#include "ic-inl.h"
#include "objects-inl.h"

namespace v8 {
namespace internal {


TypeInfo TypeInfo::TypeFromValue(Handle<Object> value) {
  TypeInfo info;
  if (value->IsSmi()) {
    info = TypeInfo::Smi();
  } else if (value->IsHeapNumber()) {
    info = TypeInfo::IsInt32Double(HeapNumber::cast(*value)->value())
        ? TypeInfo::Integer32()
        : TypeInfo::Double();
  } else if (value->IsString()) {
    info = TypeInfo::String();
  } else {
    info = TypeInfo::Unknown();
  }
  return info;
}


TypeFeedbackOracle::TypeFeedbackOracle(Handle<Code> code,
                                       Handle<Context> global_context) {
  global_context_ = global_context;
  PopulateMap(code);
  ASSERT(reinterpret_cast<Address>(*dictionary_.location()) != kHandleZapValue);
}


Handle<Object> TypeFeedbackOracle::GetInfo(unsigned ast_id) {
  int entry = dictionary_->FindEntry(ast_id);
  return entry != NumberDictionary::kNotFound
      ? Handle<Object>(dictionary_->ValueAt(entry))
      : Isolate::Current()->factory()->undefined_value();
}


bool TypeFeedbackOracle::LoadIsMonomorphic(Property* expr) {
  Handle<Object> map_or_code(GetInfo(expr->id()));
  if (map_or_code->IsMap()) return true;
  if (map_or_code->IsCode()) {
    Handle<Code> code = Handle<Code>::cast(map_or_code);
    return code->is_keyed_load_stub() &&
        code->ic_state() == MONOMORPHIC &&
        code->FindFirstMap() != NULL;
  }
  return false;
}


bool TypeFeedbackOracle::StoreIsMonomorphic(Expression* expr) {
  Handle<Object> map_or_code(GetInfo(expr->id()));
  if (map_or_code->IsMap()) return true;
  if (map_or_code->IsCode()) {
    Handle<Code> code = Handle<Code>::cast(map_or_code);
    return code->is_keyed_store_stub() &&
        code->ic_state() == MONOMORPHIC;
  }
  return false;
}


bool TypeFeedbackOracle::CallIsMonomorphic(Call* expr) {
  Handle<Object> value = GetInfo(expr->id());
  return value->IsMap() || value->IsSmi();
}


Handle<Map> TypeFeedbackOracle::LoadMonomorphicReceiverType(Property* expr) {
  ASSERT(LoadIsMonomorphic(expr));
  Handle<Object> map_or_code(GetInfo(expr->id()));
  if (map_or_code->IsCode()) {
    Handle<Code> code = Handle<Code>::cast(map_or_code);
    Map* first_map = code->FindFirstMap();
    ASSERT(first_map != NULL);
    return Handle<Map>(first_map);
  }
  return Handle<Map>::cast(map_or_code);
}


Handle<Map> TypeFeedbackOracle::StoreMonomorphicReceiverType(Expression* expr) {
  ASSERT(StoreIsMonomorphic(expr));
  Handle<Object> map_or_code(GetInfo(expr->id()));
  if (map_or_code->IsCode()) {
    Handle<Code> code = Handle<Code>::cast(map_or_code);
    return Handle<Map>(code->FindFirstMap());
  }
  return Handle<Map>::cast(map_or_code);
}


ZoneMapList* TypeFeedbackOracle::LoadReceiverTypes(Property* expr,
                                                   Handle<String> name) {
  Code::Flags flags = Code::ComputeMonomorphicFlags(Code::LOAD_IC, NORMAL);
  return CollectReceiverTypes(expr->id(), name, flags);
}


ZoneMapList* TypeFeedbackOracle::StoreReceiverTypes(Assignment* expr,
                                                    Handle<String> name) {
  Code::Flags flags = Code::ComputeMonomorphicFlags(Code::STORE_IC, NORMAL);
  return CollectReceiverTypes(expr->id(), name, flags);
}


ZoneMapList* TypeFeedbackOracle::CallReceiverTypes(Call* expr,
                                                   Handle<String> name,
                                                   CallKind call_kind) {
  int arity = expr->arguments()->length();

  // Note: Currently we do not take string extra ic data into account
  // here.
  Code::ExtraICState extra_ic_state =
      CallIC::Contextual::encode(call_kind == CALL_AS_FUNCTION);

  Code::Flags flags = Code::ComputeMonomorphicFlags(Code::CALL_IC,
                                                    NORMAL,
                                                    extra_ic_state,
                                                    OWN_MAP,
                                                    NOT_IN_LOOP,
                                                    arity);
  return CollectReceiverTypes(expr->id(), name, flags);
}


CheckType TypeFeedbackOracle::GetCallCheckType(Call* expr) {
  Handle<Object> value = GetInfo(expr->id());
  if (!value->IsSmi()) return RECEIVER_MAP_CHECK;
  CheckType check = static_cast<CheckType>(Smi::cast(*value)->value());
  ASSERT(check != RECEIVER_MAP_CHECK);
  return check;
}

ExternalArrayType TypeFeedbackOracle::GetKeyedLoadExternalArrayType(
    Property* expr) {
  Handle<Object> stub = GetInfo(expr->id());
  ASSERT(stub->IsCode());
  return Code::cast(*stub)->external_array_type();
}

ExternalArrayType TypeFeedbackOracle::GetKeyedStoreExternalArrayType(
    Expression* expr) {
  Handle<Object> stub = GetInfo(expr->id());
  ASSERT(stub->IsCode());
  return Code::cast(*stub)->external_array_type();
}

Handle<JSObject> TypeFeedbackOracle::GetPrototypeForPrimitiveCheck(
    CheckType check) {
  JSFunction* function = NULL;
  switch (check) {
    case RECEIVER_MAP_CHECK:
      UNREACHABLE();
      break;
    case STRING_CHECK:
      function = global_context_->string_function();
      break;
    case NUMBER_CHECK:
      function = global_context_->number_function();
      break;
    case BOOLEAN_CHECK:
      function = global_context_->boolean_function();
      break;
  }
  ASSERT(function != NULL);
  return Handle<JSObject>(JSObject::cast(function->instance_prototype()));
}


bool TypeFeedbackOracle::LoadIsBuiltin(Property* expr, Builtins::Name id) {
  return *GetInfo(expr->id()) ==
      Isolate::Current()->builtins()->builtin(id);
}


TypeInfo TypeFeedbackOracle::CompareType(CompareOperation* expr) {
  Handle<Object> object = GetInfo(expr->id());
  TypeInfo unknown = TypeInfo::Unknown();
  if (!object->IsCode()) return unknown;
  Handle<Code> code = Handle<Code>::cast(object);
  if (!code->is_compare_ic_stub()) return unknown;

  CompareIC::State state = static_cast<CompareIC::State>(code->compare_state());
  switch (state) {
    case CompareIC::UNINITIALIZED:
      // Uninitialized means never executed.
      // TODO(fschneider): Introduce a separate value for never-executed ICs.
      return unknown;
    case CompareIC::SMIS:
      return TypeInfo::Smi();
    case CompareIC::HEAP_NUMBERS:
      return TypeInfo::Number();
    case CompareIC::SYMBOLS:
    case CompareIC::STRINGS:
      return TypeInfo::String();
    case CompareIC::OBJECTS:
      // TODO(kasperl): We really need a type for JS objects here.
      return TypeInfo::NonPrimitive();
    case CompareIC::GENERIC:
    default:
      return unknown;
  }
}


bool TypeFeedbackOracle::IsSymbolCompare(CompareOperation* expr) {
  Handle<Object> object = GetInfo(expr->id());
  if (!object->IsCode()) return false;
  Handle<Code> code = Handle<Code>::cast(object);
  if (!code->is_compare_ic_stub()) return false;
  CompareIC::State state = static_cast<CompareIC::State>(code->compare_state());
  return state == CompareIC::SYMBOLS;
}


TypeInfo TypeFeedbackOracle::UnaryType(UnaryOperation* expr) {
  Handle<Object> object = GetInfo(expr->id());
  TypeInfo unknown = TypeInfo::Unknown();
  if (!object->IsCode()) return unknown;
  Handle<Code> code = Handle<Code>::cast(object);
  ASSERT(code->is_unary_op_stub());
  UnaryOpIC::TypeInfo type = static_cast<UnaryOpIC::TypeInfo>(
      code->unary_op_type());
  switch (type) {
    case UnaryOpIC::SMI:
      return TypeInfo::Smi();
    case UnaryOpIC::HEAP_NUMBER:
      return TypeInfo::Double();
    default:
      return unknown;
  }
}


TypeInfo TypeFeedbackOracle::BinaryType(BinaryOperation* expr) {
  Handle<Object> object = GetInfo(expr->id());
  TypeInfo unknown = TypeInfo::Unknown();
  if (!object->IsCode()) return unknown;
  Handle<Code> code = Handle<Code>::cast(object);
  if (code->is_binary_op_stub()) {
    BinaryOpIC::TypeInfo type = static_cast<BinaryOpIC::TypeInfo>(
        code->binary_op_type());
    BinaryOpIC::TypeInfo result_type = static_cast<BinaryOpIC::TypeInfo>(
        code->binary_op_result_type());

    switch (type) {
      case BinaryOpIC::UNINITIALIZED:
        // Uninitialized means never executed.
        // TODO(fschneider): Introduce a separate value for never-executed ICs
        return unknown;
      case BinaryOpIC::SMI:
        switch (result_type) {
          case BinaryOpIC::UNINITIALIZED:
          case BinaryOpIC::SMI:
            return TypeInfo::Smi();
          case BinaryOpIC::INT32:
            return TypeInfo::Integer32();
          case BinaryOpIC::HEAP_NUMBER:
            return TypeInfo::Double();
          default:
            return unknown;
        }
      case BinaryOpIC::INT32:
        if (expr->op() == Token::DIV ||
            result_type == BinaryOpIC::HEAP_NUMBER) {
          return TypeInfo::Double();
        }
        return TypeInfo::Integer32();
      case BinaryOpIC::HEAP_NUMBER:
        return TypeInfo::Double();
      case BinaryOpIC::BOTH_STRING:
        return TypeInfo::String();
      case BinaryOpIC::STRING:
      case BinaryOpIC::GENERIC:
        return unknown;
     default:
        return unknown;
    }
  }
  return unknown;
}


TypeInfo TypeFeedbackOracle::SwitchType(CaseClause* clause) {
  Handle<Object> object = GetInfo(clause->CompareId());
  TypeInfo unknown = TypeInfo::Unknown();
  if (!object->IsCode()) return unknown;
  Handle<Code> code = Handle<Code>::cast(object);
  if (!code->is_compare_ic_stub()) return unknown;

  CompareIC::State state = static_cast<CompareIC::State>(code->compare_state());
  switch (state) {
    case CompareIC::UNINITIALIZED:
      // Uninitialized means never executed.
      // TODO(fschneider): Introduce a separate value for never-executed ICs.
      return unknown;
    case CompareIC::SMIS:
      return TypeInfo::Smi();
    case CompareIC::HEAP_NUMBERS:
      return TypeInfo::Number();
    case CompareIC::OBJECTS:
      // TODO(kasperl): We really need a type for JS objects here.
      return TypeInfo::NonPrimitive();
    case CompareIC::GENERIC:
    default:
      return unknown;
  }
}


TypeInfo TypeFeedbackOracle::IncrementType(CountOperation* expr) {
  Handle<Object> object = GetInfo(expr->CountId());
  TypeInfo unknown = TypeInfo::Unknown();
  if (!object->IsCode()) return unknown;
  Handle<Code> code = Handle<Code>::cast(object);
  if (!code->is_binary_op_stub()) return unknown;

  BinaryOpIC::TypeInfo type = static_cast<BinaryOpIC::TypeInfo>(
      code->binary_op_type());
  switch (type) {
    case BinaryOpIC::UNINITIALIZED:
    case BinaryOpIC::SMI:
      return TypeInfo::Smi();
    case BinaryOpIC::INT32:
      return TypeInfo::Integer32();
    case BinaryOpIC::HEAP_NUMBER:
      return TypeInfo::Double();
    case BinaryOpIC::BOTH_STRING:
    case BinaryOpIC::STRING:
    case BinaryOpIC::GENERIC:
      return unknown;
    default:
      return unknown;
  }
  UNREACHABLE();
  return unknown;
}


ZoneMapList* TypeFeedbackOracle::CollectReceiverTypes(unsigned ast_id,
                                                      Handle<String> name,
                                                      Code::Flags flags) {
  Isolate* isolate = Isolate::Current();
  Handle<Object> object = GetInfo(ast_id);
  if (object->IsUndefined() || object->IsSmi()) return NULL;

  if (*object == isolate->builtins()->builtin(Builtins::kStoreIC_GlobalProxy)) {
    // TODO(fschneider): We could collect the maps and signal that
    // we need a generic store (or load) here.
    ASSERT(Handle<Code>::cast(object)->ic_state() == MEGAMORPHIC);
    return NULL;
  } else if (object->IsMap()) {
    ZoneMapList* types = new ZoneMapList(1);
    types->Add(Handle<Map>::cast(object));
    return types;
  } else if (Handle<Code>::cast(object)->ic_state() == MEGAMORPHIC) {
    ZoneMapList* types = new ZoneMapList(4);
    ASSERT(object->IsCode());
    isolate->stub_cache()->CollectMatchingMaps(types, *name, flags);
    return types->length() > 0 ? types : NULL;
  } else {
    return NULL;
  }
}


void TypeFeedbackOracle::SetInfo(unsigned ast_id, Object* target) {
  ASSERT(dictionary_->FindEntry(ast_id) == NumberDictionary::kNotFound);
  MaybeObject* maybe_result = dictionary_->AtNumberPut(ast_id, target);
  USE(maybe_result);
#ifdef DEBUG
  Object* result;
  // Dictionary has been allocated with sufficient size for all elements.
  ASSERT(maybe_result->ToObject(&result));
  ASSERT(*dictionary_ == result);
#endif
}


void TypeFeedbackOracle::PopulateMap(Handle<Code> code) {
  Isolate* isolate = Isolate::Current();
  HandleScope scope(isolate);

  const int kInitialCapacity = 16;
  List<int> code_positions(kInitialCapacity);
  List<unsigned> ast_ids(kInitialCapacity);
  CollectIds(*code, &code_positions, &ast_ids);

  ASSERT(dictionary_.is_null());  // Only initialize once.
  dictionary_ = isolate->factory()->NewNumberDictionary(
      code_positions.length());

  const int length = code_positions.length();
  ASSERT(ast_ids.length() == length);
  for (int i = 0; i < length; i++) {
    AssertNoAllocation no_allocation;
    RelocInfo info(code->instruction_start() + code_positions[i],
                   RelocInfo::CODE_TARGET, 0);
    Code* target = Code::GetCodeFromTargetAddress(info.target_address());
    unsigned id = ast_ids[i];
    InlineCacheState state = target->ic_state();
    Code::Kind kind = target->kind();

    if (kind == Code::BINARY_OP_IC ||
        kind == Code::UNARY_OP_IC ||
        kind == Code::COMPARE_IC) {
      SetInfo(id, target);
    } else if (state == MONOMORPHIC) {
      if (kind == Code::KEYED_LOAD_IC ||
          kind == Code::KEYED_STORE_IC) {
        SetInfo(id, target);
      } else if (kind != Code::CALL_IC ||
                 target->check_type() == RECEIVER_MAP_CHECK) {
        Map* map = target->FindFirstMap();
        if (map == NULL) {
          SetInfo(id, target);
        } else {
          SetInfo(id, map);
        }
      } else {
        ASSERT(target->kind() == Code::CALL_IC);
        CheckType check = target->check_type();
        ASSERT(check != RECEIVER_MAP_CHECK);
        SetInfo(id,  Smi::FromInt(check));
      }
    } else if (state == MEGAMORPHIC) {
      SetInfo(id, target);
    }
  }
  // Allocate handle in the parent scope.
  dictionary_ = scope.CloseAndEscape(dictionary_);
}


void TypeFeedbackOracle::CollectIds(Code* code,
                                    List<int>* code_positions,
                                    List<unsigned>* ast_ids) {
  AssertNoAllocation no_allocation;
  int mask = RelocInfo::ModeMask(RelocInfo::CODE_TARGET_WITH_ID);
  for (RelocIterator it(code, mask); !it.done(); it.next()) {
    RelocInfo* info = it.rinfo();
    ASSERT(RelocInfo::IsCodeTarget(info->rmode()));
    Code* target = Code::GetCodeFromTargetAddress(info->target_address());
    if (target->is_inline_cache_stub()) {
      InlineCacheState state = target->ic_state();
      Code::Kind kind = target->kind();
      if (kind == Code::BINARY_OP_IC) {
        if (target->binary_op_type() ==
            BinaryOpIC::GENERIC) {
          continue;
        }
      } else if (kind == Code::COMPARE_IC) {
        if (target->compare_state() == CompareIC::GENERIC) continue;
      } else {
        if (state != MONOMORPHIC && state != MEGAMORPHIC) continue;
      }
      code_positions->Add(
          static_cast<int>(info->pc() - code->instruction_start()));
      ASSERT(ast_ids->length() == 0 ||
             (*ast_ids)[ast_ids->length()-1] !=
             static_cast<unsigned>(info->data()));
      ast_ids->Add(static_cast<unsigned>(info->data()));
    }
  }
}

} }  // namespace v8::internal
