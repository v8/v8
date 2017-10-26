// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/builtin-deserializer.h"

#include "src/assembler-inl.h"
#include "src/interpreter/interpreter.h"
#include "src/objects-inl.h"
#include "src/snapshot/snapshot.h"

namespace v8 {
namespace internal {

using interpreter::Bytecode;
using interpreter::Bytecodes;
using interpreter::Interpreter;
using interpreter::OperandScale;

// Tracks the code object currently being deserialized (required for
// allocation).
class DeserializingCodeObjectScope {
 public:
  DeserializingCodeObjectScope(BuiltinDeserializer* builtin_deserializer,
                               int code_object_id)
      : builtin_deserializer_(builtin_deserializer) {
    DCHECK_EQ(BuiltinDeserializer::kNoCodeObjectId,
              builtin_deserializer->current_code_object_id_);
    builtin_deserializer->current_code_object_id_ = code_object_id;
  }

  ~DeserializingCodeObjectScope() {
    builtin_deserializer_->current_code_object_id_ =
        BuiltinDeserializer::kNoCodeObjectId;
  }

 private:
  BuiltinDeserializer* builtin_deserializer_;

  DISALLOW_COPY_AND_ASSIGN(DeserializingCodeObjectScope)
};

BuiltinDeserializer::BuiltinDeserializer(Isolate* isolate,
                                         const BuiltinSnapshotData* data)
    : Deserializer(data, false) {
  code_offsets_ = data->BuiltinOffsets();
  DCHECK_EQ(BSU::kNumberOfCodeObjects, code_offsets_.length());
  DCHECK(std::is_sorted(code_offsets_.begin(), code_offsets_.end()));

  Initialize(isolate);
}

void BuiltinDeserializer::DeserializeEagerBuiltinsAndHandlers() {
  DCHECK(!AllowHeapAllocation::IsAllowed());
  DCHECK_EQ(0, source()->position());

  // Deserialize builtins.

  Builtins* builtins = isolate()->builtins();
  for (int i = 0; i < BSU::kNumberOfBuiltins; i++) {
    if (IsLazyDeserializationEnabled() && Builtins::IsLazy(i)) {
      // Do nothing. These builtins have been replaced by DeserializeLazy in
      // InitializeBuiltinsTable.
      DCHECK_EQ(builtins->builtin(Builtins::kDeserializeLazy),
                builtins->builtin(i));
    } else {
      builtins->set_builtin(i, DeserializeBuiltinRaw(i));
    }
  }

#ifdef DEBUG
  for (int i = 0; i < BSU::kNumberOfBuiltins; i++) {
    Object* o = builtins->builtin(i);
    DCHECK(o->IsCode() && Code::cast(o)->is_builtin());
  }
#endif

  // Deserialize bytecode handlers.

  // The dispatch table has been initialized during memory reservation.
  Interpreter* interpreter = isolate()->interpreter();
  DCHECK(isolate()->interpreter()->IsDispatchTableInitialized());

  BSU::ForEachBytecode([=](Bytecode bytecode, OperandScale operand_scale) {
    // TODO(jgruber): Replace with DeserializeLazy handler.

    // Bytecodes without a dedicated handler are patched up in a second pass.
    if (!BSU::BytecodeHasDedicatedHandler(bytecode, operand_scale)) return;

    Code* code = DeserializeHandlerRaw(bytecode, operand_scale);
    interpreter->SetBytecodeHandler(bytecode, operand_scale, code);
  });

  // Patch up holes in the dispatch table.

  DCHECK(BSU::BytecodeHasDedicatedHandler(Bytecode::kIllegal,
                                          OperandScale::kSingle));
  Code* illegal_handler = interpreter->GetBytecodeHandler(
      Bytecode::kIllegal, OperandScale::kSingle);

  BSU::ForEachBytecode([=](Bytecode bytecode, OperandScale operand_scale) {
    if (BSU::BytecodeHasDedicatedHandler(bytecode, operand_scale)) return;

    Bytecode maybe_reused_bytecode;
    if (Bytecodes::ReusesExistingHandler(bytecode, &maybe_reused_bytecode)) {
      interpreter->SetBytecodeHandler(
          bytecode, operand_scale,
          interpreter->GetBytecodeHandler(maybe_reused_bytecode,
                                          operand_scale));
      return;
    }

    DCHECK(!Bytecodes::BytecodeHasHandler(bytecode, operand_scale));
    interpreter->SetBytecodeHandler(bytecode, operand_scale, illegal_handler);
  });

  DCHECK(isolate()->interpreter()->IsDispatchTableInitialized());
}

Code* BuiltinDeserializer::DeserializeBuiltin(int builtin_id) {
  allocator()->ReserveAndInitializeBuiltinsTableForBuiltin(builtin_id);
  DisallowHeapAllocation no_gc;
  return DeserializeBuiltinRaw(builtin_id);
}

Code* BuiltinDeserializer::DeserializeBuiltinRaw(int builtin_id) {
  DCHECK(!AllowHeapAllocation::IsAllowed());
  DCHECK(Builtins::IsBuiltinId(builtin_id));

  DeserializingCodeObjectScope scope(this, builtin_id);

  const int initial_position = source()->position();
  source()->set_position(code_offsets_[builtin_id]);

  Object* o = ReadDataSingle();
  DCHECK(o->IsCode() && Code::cast(o)->is_builtin());

  // Rewind.
  source()->set_position(initial_position);

  // Flush the instruction cache.
  Code* code = Code::cast(o);
  Assembler::FlushICache(isolate(), code->instruction_start(),
                         code->instruction_size());

  return code;
}

Code* BuiltinDeserializer::DeserializeHandlerRaw(Bytecode bytecode,
                                                 OperandScale operand_scale) {
  DCHECK(!AllowHeapAllocation::IsAllowed());
  DCHECK(BSU::BytecodeHasDedicatedHandler(bytecode, operand_scale));

  const int code_object_id = BSU::BytecodeToIndex(bytecode, operand_scale);
  DeserializingCodeObjectScope scope(this, code_object_id);

  const int initial_position = source()->position();
  source()->set_position(code_offsets_[code_object_id]);

  Object* o = ReadDataSingle();
  DCHECK(o->IsCode() && Code::cast(o)->kind() == Code::BYTECODE_HANDLER);

  // Rewind.
  source()->set_position(initial_position);

  // Flush the instruction cache.
  Code* code = Code::cast(o);
  Assembler::FlushICache(isolate(), code->instruction_start(),
                         code->instruction_size());

  return code;
}

uint32_t BuiltinDeserializer::ExtractCodeObjectSize(int code_object_id) {
  DCHECK_LT(code_object_id, BSU::kNumberOfCodeObjects);

  const int initial_position = source()->position();

  // Grab the size of the code object.
  source()->set_position(code_offsets_[code_object_id]);
  byte data = source()->Get();

  USE(data);
  DCHECK_EQ(kNewObject | kPlain | kStartOfObject | CODE_SPACE, data);
  const uint32_t result = source()->GetInt() << kObjectAlignmentBits;

  // Rewind.
  source()->set_position(initial_position);

  return result;
}

}  // namespace internal
}  // namespace v8
