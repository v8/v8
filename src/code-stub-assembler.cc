// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/code-stub-assembler.h"
#include "src/code-factory.h"
#include "src/frames-inl.h"
#include "src/frames.h"
#include "src/ic/stub-cache.h"

namespace v8 {
namespace internal {

using compiler::Node;

CodeStubAssembler::CodeStubAssembler(Isolate* isolate, Zone* zone,
                                     const CallInterfaceDescriptor& descriptor,
                                     Code::Flags flags, const char* name,
                                     size_t result_size)
    : compiler::CodeAssembler(isolate, zone, descriptor, flags, name,
                              result_size) {}

CodeStubAssembler::CodeStubAssembler(Isolate* isolate, Zone* zone,
                                     int parameter_count, Code::Flags flags,
                                     const char* name)
    : compiler::CodeAssembler(isolate, zone, parameter_count, flags, name) {}

void CodeStubAssembler::Assert(Node* condition) {
#if defined(DEBUG)
  Label ok(this);
  Comment("[ Assert");
  GotoIf(condition, &ok);
  DebugBreak();
  Goto(&ok);
  Bind(&ok);
  Comment("] Assert");
#endif
}

Node* CodeStubAssembler::BooleanMapConstant() {
  return HeapConstant(isolate()->factory()->boolean_map());
}

Node* CodeStubAssembler::EmptyStringConstant() {
  return LoadRoot(Heap::kempty_stringRootIndex);
}

Node* CodeStubAssembler::HeapNumberMapConstant() {
  return HeapConstant(isolate()->factory()->heap_number_map());
}

Node* CodeStubAssembler::NoContextConstant() {
  return SmiConstant(Smi::FromInt(0));
}

Node* CodeStubAssembler::NullConstant() {
  return LoadRoot(Heap::kNullValueRootIndex);
}

Node* CodeStubAssembler::UndefinedConstant() {
  return LoadRoot(Heap::kUndefinedValueRootIndex);
}

Node* CodeStubAssembler::TheHoleConstant() {
  return LoadRoot(Heap::kTheHoleValueRootIndex);
}

Node* CodeStubAssembler::HashSeed() {
  return SmiToWord32(LoadRoot(Heap::kHashSeedRootIndex));
}

Node* CodeStubAssembler::StaleRegisterConstant() {
  return LoadRoot(Heap::kStaleRegisterRootIndex);
}

Node* CodeStubAssembler::Float64Round(Node* x) {
  Node* one = Float64Constant(1.0);
  Node* one_half = Float64Constant(0.5);

  Variable var_x(this, MachineRepresentation::kFloat64);
  Label return_x(this);

  // Round up {x} towards Infinity.
  var_x.Bind(Float64Ceil(x));

  GotoIf(Float64LessThanOrEqual(Float64Sub(var_x.value(), one_half), x),
         &return_x);
  var_x.Bind(Float64Sub(var_x.value(), one));
  Goto(&return_x);

  Bind(&return_x);
  return var_x.value();
}

Node* CodeStubAssembler::Float64Ceil(Node* x) {
  if (IsFloat64RoundUpSupported()) {
    return Float64RoundUp(x);
  }

  Node* one = Float64Constant(1.0);
  Node* zero = Float64Constant(0.0);
  Node* two_52 = Float64Constant(4503599627370496.0E0);
  Node* minus_two_52 = Float64Constant(-4503599627370496.0E0);

  Variable var_x(this, MachineRepresentation::kFloat64);
  Label return_x(this), return_minus_x(this);
  var_x.Bind(x);

  // Check if {x} is greater than zero.
  Label if_xgreaterthanzero(this), if_xnotgreaterthanzero(this);
  Branch(Float64GreaterThan(x, zero), &if_xgreaterthanzero,
         &if_xnotgreaterthanzero);

  Bind(&if_xgreaterthanzero);
  {
    // Just return {x} unless it's in the range ]0,2^52[.
    GotoIf(Float64GreaterThanOrEqual(x, two_52), &return_x);

    // Round positive {x} towards Infinity.
    var_x.Bind(Float64Sub(Float64Add(two_52, x), two_52));
    GotoUnless(Float64LessThan(var_x.value(), x), &return_x);
    var_x.Bind(Float64Add(var_x.value(), one));
    Goto(&return_x);
  }

  Bind(&if_xnotgreaterthanzero);
  {
    // Just return {x} unless it's in the range ]-2^52,0[
    GotoIf(Float64LessThanOrEqual(x, minus_two_52), &return_x);
    GotoUnless(Float64LessThan(x, zero), &return_x);

    // Round negated {x} towards Infinity and return the result negated.
    Node* minus_x = Float64Neg(x);
    var_x.Bind(Float64Sub(Float64Add(two_52, minus_x), two_52));
    GotoUnless(Float64GreaterThan(var_x.value(), minus_x), &return_minus_x);
    var_x.Bind(Float64Sub(var_x.value(), one));
    Goto(&return_minus_x);
  }

  Bind(&return_minus_x);
  var_x.Bind(Float64Neg(var_x.value()));
  Goto(&return_x);

  Bind(&return_x);
  return var_x.value();
}

Node* CodeStubAssembler::Float64Floor(Node* x) {
  if (IsFloat64RoundDownSupported()) {
    return Float64RoundDown(x);
  }

  Node* one = Float64Constant(1.0);
  Node* zero = Float64Constant(0.0);
  Node* two_52 = Float64Constant(4503599627370496.0E0);
  Node* minus_two_52 = Float64Constant(-4503599627370496.0E0);

  Variable var_x(this, MachineRepresentation::kFloat64);
  Label return_x(this), return_minus_x(this);
  var_x.Bind(x);

  // Check if {x} is greater than zero.
  Label if_xgreaterthanzero(this), if_xnotgreaterthanzero(this);
  Branch(Float64GreaterThan(x, zero), &if_xgreaterthanzero,
         &if_xnotgreaterthanzero);

  Bind(&if_xgreaterthanzero);
  {
    // Just return {x} unless it's in the range ]0,2^52[.
    GotoIf(Float64GreaterThanOrEqual(x, two_52), &return_x);

    // Round positive {x} towards -Infinity.
    var_x.Bind(Float64Sub(Float64Add(two_52, x), two_52));
    GotoUnless(Float64GreaterThan(var_x.value(), x), &return_x);
    var_x.Bind(Float64Sub(var_x.value(), one));
    Goto(&return_x);
  }

  Bind(&if_xnotgreaterthanzero);
  {
    // Just return {x} unless it's in the range ]-2^52,0[
    GotoIf(Float64LessThanOrEqual(x, minus_two_52), &return_x);
    GotoUnless(Float64LessThan(x, zero), &return_x);

    // Round negated {x} towards -Infinity and return the result negated.
    Node* minus_x = Float64Neg(x);
    var_x.Bind(Float64Sub(Float64Add(two_52, minus_x), two_52));
    GotoUnless(Float64LessThan(var_x.value(), minus_x), &return_minus_x);
    var_x.Bind(Float64Add(var_x.value(), one));
    Goto(&return_minus_x);
  }

  Bind(&return_minus_x);
  var_x.Bind(Float64Neg(var_x.value()));
  Goto(&return_x);

  Bind(&return_x);
  return var_x.value();
}

Node* CodeStubAssembler::Float64Trunc(Node* x) {
  if (IsFloat64RoundTruncateSupported()) {
    return Float64RoundTruncate(x);
  }

  Node* one = Float64Constant(1.0);
  Node* zero = Float64Constant(0.0);
  Node* two_52 = Float64Constant(4503599627370496.0E0);
  Node* minus_two_52 = Float64Constant(-4503599627370496.0E0);

  Variable var_x(this, MachineRepresentation::kFloat64);
  Label return_x(this), return_minus_x(this);
  var_x.Bind(x);

  // Check if {x} is greater than 0.
  Label if_xgreaterthanzero(this), if_xnotgreaterthanzero(this);
  Branch(Float64GreaterThan(x, zero), &if_xgreaterthanzero,
         &if_xnotgreaterthanzero);

  Bind(&if_xgreaterthanzero);
  {
    if (IsFloat64RoundDownSupported()) {
      var_x.Bind(Float64RoundDown(x));
    } else {
      // Just return {x} unless it's in the range ]0,2^52[.
      GotoIf(Float64GreaterThanOrEqual(x, two_52), &return_x);

      // Round positive {x} towards -Infinity.
      var_x.Bind(Float64Sub(Float64Add(two_52, x), two_52));
      GotoUnless(Float64GreaterThan(var_x.value(), x), &return_x);
      var_x.Bind(Float64Sub(var_x.value(), one));
    }
    Goto(&return_x);
  }

  Bind(&if_xnotgreaterthanzero);
  {
    if (IsFloat64RoundUpSupported()) {
      var_x.Bind(Float64RoundUp(x));
      Goto(&return_x);
    } else {
      // Just return {x} unless its in the range ]-2^52,0[.
      GotoIf(Float64LessThanOrEqual(x, minus_two_52), &return_x);
      GotoUnless(Float64LessThan(x, zero), &return_x);

      // Round negated {x} towards -Infinity and return result negated.
      Node* minus_x = Float64Neg(x);
      var_x.Bind(Float64Sub(Float64Add(two_52, minus_x), two_52));
      GotoUnless(Float64GreaterThan(var_x.value(), minus_x), &return_minus_x);
      var_x.Bind(Float64Sub(var_x.value(), one));
      Goto(&return_minus_x);
    }
  }

  Bind(&return_minus_x);
  var_x.Bind(Float64Neg(var_x.value()));
  Goto(&return_x);

  Bind(&return_x);
  return var_x.value();
}

Node* CodeStubAssembler::SmiFromWord32(Node* value) {
  value = ChangeInt32ToIntPtr(value);
  return WordShl(value, SmiShiftBitsConstant());
}

Node* CodeStubAssembler::SmiTag(Node* value) {
  int32_t constant_value;
  if (ToInt32Constant(value, constant_value) && Smi::IsValid(constant_value)) {
    return SmiConstant(Smi::FromInt(constant_value));
  }
  return WordShl(value, SmiShiftBitsConstant());
}

Node* CodeStubAssembler::SmiUntag(Node* value) {
  return WordSar(value, SmiShiftBitsConstant());
}

Node* CodeStubAssembler::SmiToWord32(Node* value) {
  Node* result = WordSar(value, SmiShiftBitsConstant());
  if (Is64()) {
    result = TruncateInt64ToInt32(result);
  }
  return result;
}

Node* CodeStubAssembler::SmiToFloat64(Node* value) {
  return ChangeInt32ToFloat64(SmiToWord32(value));
}

Node* CodeStubAssembler::SmiAdd(Node* a, Node* b) { return IntPtrAdd(a, b); }

Node* CodeStubAssembler::SmiAddWithOverflow(Node* a, Node* b) {
  return IntPtrAddWithOverflow(a, b);
}

Node* CodeStubAssembler::SmiSub(Node* a, Node* b) { return IntPtrSub(a, b); }

Node* CodeStubAssembler::SmiSubWithOverflow(Node* a, Node* b) {
  return IntPtrSubWithOverflow(a, b);
}

Node* CodeStubAssembler::SmiEqual(Node* a, Node* b) { return WordEqual(a, b); }

Node* CodeStubAssembler::SmiAboveOrEqual(Node* a, Node* b) {
  return UintPtrGreaterThanOrEqual(a, b);
}

Node* CodeStubAssembler::SmiLessThan(Node* a, Node* b) {
  return IntPtrLessThan(a, b);
}

Node* CodeStubAssembler::SmiLessThanOrEqual(Node* a, Node* b) {
  return IntPtrLessThanOrEqual(a, b);
}

Node* CodeStubAssembler::SmiMin(Node* a, Node* b) {
  // TODO(bmeurer): Consider using Select once available.
  Variable min(this, MachineRepresentation::kTagged);
  Label if_a(this), if_b(this), join(this);
  BranchIfSmiLessThan(a, b, &if_a, &if_b);
  Bind(&if_a);
  min.Bind(a);
  Goto(&join);
  Bind(&if_b);
  min.Bind(b);
  Goto(&join);
  Bind(&join);
  return min.value();
}

Node* CodeStubAssembler::WordIsSmi(Node* a) {
  return WordEqual(WordAnd(a, IntPtrConstant(kSmiTagMask)), IntPtrConstant(0));
}

Node* CodeStubAssembler::WordIsPositiveSmi(Node* a) {
  return WordEqual(WordAnd(a, IntPtrConstant(kSmiTagMask | kSmiSignMask)),
                   IntPtrConstant(0));
}

Node* CodeStubAssembler::AllocateRawUnaligned(Node* size_in_bytes,
                                              AllocationFlags flags,
                                              Node* top_address,
                                              Node* limit_address) {
  Node* top = Load(MachineType::Pointer(), top_address);
  Node* limit = Load(MachineType::Pointer(), limit_address);

  // If there's not enough space, call the runtime.
  Variable result(this, MachineRepresentation::kTagged);
  Label runtime_call(this, Label::kDeferred), no_runtime_call(this);
  Label merge_runtime(this, &result);

  Node* new_top = IntPtrAdd(top, size_in_bytes);
  Branch(UintPtrGreaterThanOrEqual(new_top, limit), &runtime_call,
         &no_runtime_call);

  Bind(&runtime_call);
  // AllocateInTargetSpace does not use the context.
  Node* context = SmiConstant(Smi::FromInt(0));

  Node* runtime_result;
  if (flags & kPretenured) {
    Node* runtime_flags = SmiConstant(
        Smi::FromInt(AllocateDoubleAlignFlag::encode(false) |
                     AllocateTargetSpace::encode(AllocationSpace::OLD_SPACE)));
    runtime_result = CallRuntime(Runtime::kAllocateInTargetSpace, context,
                                 SmiTag(size_in_bytes), runtime_flags);
  } else {
    runtime_result = CallRuntime(Runtime::kAllocateInNewSpace, context,
                                 SmiTag(size_in_bytes));
  }
  result.Bind(runtime_result);
  Goto(&merge_runtime);

  // When there is enough space, return `top' and bump it up.
  Bind(&no_runtime_call);
  Node* no_runtime_result = top;
  StoreNoWriteBarrier(MachineType::PointerRepresentation(), top_address,
                      new_top);
  no_runtime_result = BitcastWordToTagged(
      IntPtrAdd(no_runtime_result, IntPtrConstant(kHeapObjectTag)));
  result.Bind(no_runtime_result);
  Goto(&merge_runtime);

  Bind(&merge_runtime);
  return result.value();
}

Node* CodeStubAssembler::AllocateRawAligned(Node* size_in_bytes,
                                            AllocationFlags flags,
                                            Node* top_address,
                                            Node* limit_address) {
  Node* top = Load(MachineType::Pointer(), top_address);
  Node* limit = Load(MachineType::Pointer(), limit_address);
  Variable adjusted_size(this, MachineType::PointerRepresentation());
  adjusted_size.Bind(size_in_bytes);
  if (flags & kDoubleAlignment) {
    // TODO(epertoso): Simd128 alignment.
    Label aligned(this), not_aligned(this), merge(this, &adjusted_size);
    Branch(WordAnd(top, IntPtrConstant(kDoubleAlignmentMask)), &not_aligned,
           &aligned);

    Bind(&not_aligned);
    Node* not_aligned_size =
        IntPtrAdd(size_in_bytes, IntPtrConstant(kPointerSize));
    adjusted_size.Bind(not_aligned_size);
    Goto(&merge);

    Bind(&aligned);
    Goto(&merge);

    Bind(&merge);
  }

  Variable address(this, MachineRepresentation::kTagged);
  address.Bind(AllocateRawUnaligned(adjusted_size.value(), kNone, top, limit));

  Label needs_filler(this), doesnt_need_filler(this),
      merge_address(this, &address);
  Branch(IntPtrEqual(adjusted_size.value(), size_in_bytes), &doesnt_need_filler,
         &needs_filler);

  Bind(&needs_filler);
  // Store a filler and increase the address by kPointerSize.
  // TODO(epertoso): this code assumes that we only align to kDoubleSize. Change
  // it when Simd128 alignment is supported.
  StoreNoWriteBarrier(MachineType::PointerRepresentation(), top,
                      LoadRoot(Heap::kOnePointerFillerMapRootIndex));
  address.Bind(BitcastWordToTagged(
      IntPtrAdd(address.value(), IntPtrConstant(kPointerSize))));
  Goto(&merge_address);

  Bind(&doesnt_need_filler);
  Goto(&merge_address);

  Bind(&merge_address);
  // Update the top.
  StoreNoWriteBarrier(MachineType::PointerRepresentation(), top_address,
                      IntPtrAdd(top, adjusted_size.value()));
  return address.value();
}

Node* CodeStubAssembler::Allocate(Node* size_in_bytes, AllocationFlags flags) {
  bool const new_space = !(flags & kPretenured);
  Node* top_address = ExternalConstant(
      new_space
          ? ExternalReference::new_space_allocation_top_address(isolate())
          : ExternalReference::old_space_allocation_top_address(isolate()));
  Node* limit_address = ExternalConstant(
      new_space
          ? ExternalReference::new_space_allocation_limit_address(isolate())
          : ExternalReference::old_space_allocation_limit_address(isolate()));

#ifdef V8_HOST_ARCH_32_BIT
  if (flags & kDoubleAlignment) {
    return AllocateRawAligned(size_in_bytes, flags, top_address, limit_address);
  }
#endif

  return AllocateRawUnaligned(size_in_bytes, flags, top_address, limit_address);
}

Node* CodeStubAssembler::Allocate(int size_in_bytes, AllocationFlags flags) {
  return CodeStubAssembler::Allocate(IntPtrConstant(size_in_bytes), flags);
}

Node* CodeStubAssembler::InnerAllocate(Node* previous, Node* offset) {
  return BitcastWordToTagged(IntPtrAdd(previous, offset));
}

Node* CodeStubAssembler::InnerAllocate(Node* previous, int offset) {
  return InnerAllocate(previous, IntPtrConstant(offset));
}

compiler::Node* CodeStubAssembler::LoadFromFrame(int offset, MachineType rep) {
  Node* frame_pointer = LoadFramePointer();
  return Load(rep, frame_pointer, IntPtrConstant(offset));
}

compiler::Node* CodeStubAssembler::LoadFromParentFrame(int offset,
                                                       MachineType rep) {
  Node* frame_pointer = LoadParentFramePointer();
  return Load(rep, frame_pointer, IntPtrConstant(offset));
}

Node* CodeStubAssembler::LoadBufferObject(Node* buffer, int offset,
                                          MachineType rep) {
  return Load(rep, buffer, IntPtrConstant(offset));
}

Node* CodeStubAssembler::LoadObjectField(Node* object, int offset,
                                         MachineType rep) {
  return Load(rep, object, IntPtrConstant(offset - kHeapObjectTag));
}

Node* CodeStubAssembler::LoadObjectField(Node* object, Node* offset,
                                         MachineType rep) {
  return Load(rep, object, IntPtrSub(offset, IntPtrConstant(kHeapObjectTag)));
}

Node* CodeStubAssembler::LoadHeapNumberValue(Node* object) {
  return LoadObjectField(object, HeapNumber::kValueOffset,
                         MachineType::Float64());
}

Node* CodeStubAssembler::LoadMap(Node* object) {
  return LoadObjectField(object, HeapObject::kMapOffset);
}

Node* CodeStubAssembler::LoadInstanceType(Node* object) {
  return LoadMapInstanceType(LoadMap(object));
}

void CodeStubAssembler::AssertInstanceType(Node* object,
                                           InstanceType instance_type) {
  Assert(Word32Equal(LoadInstanceType(object), Int32Constant(instance_type)));
}

Node* CodeStubAssembler::LoadProperties(Node* object) {
  return LoadObjectField(object, JSObject::kPropertiesOffset);
}

Node* CodeStubAssembler::LoadElements(Node* object) {
  return LoadObjectField(object, JSObject::kElementsOffset);
}

Node* CodeStubAssembler::LoadFixedArrayBaseLength(Node* array) {
  return LoadObjectField(array, FixedArrayBase::kLengthOffset);
}

Node* CodeStubAssembler::LoadMapBitField(Node* map) {
  return LoadObjectField(map, Map::kBitFieldOffset, MachineType::Uint8());
}

Node* CodeStubAssembler::LoadMapBitField2(Node* map) {
  return LoadObjectField(map, Map::kBitField2Offset, MachineType::Uint8());
}

Node* CodeStubAssembler::LoadMapBitField3(Node* map) {
  return LoadObjectField(map, Map::kBitField3Offset, MachineType::Uint32());
}

Node* CodeStubAssembler::LoadMapInstanceType(Node* map) {
  return LoadObjectField(map, Map::kInstanceTypeOffset, MachineType::Uint8());
}

Node* CodeStubAssembler::LoadMapDescriptors(Node* map) {
  return LoadObjectField(map, Map::kDescriptorsOffset);
}

Node* CodeStubAssembler::LoadMapPrototype(Node* map) {
  return LoadObjectField(map, Map::kPrototypeOffset);
}

Node* CodeStubAssembler::LoadMapInstanceSize(Node* map) {
  return LoadObjectField(map, Map::kInstanceSizeOffset, MachineType::Uint8());
}

Node* CodeStubAssembler::LoadMapInobjectProperties(Node* map) {
  // See Map::GetInObjectProperties() for details.
  STATIC_ASSERT(LAST_JS_OBJECT_TYPE == LAST_TYPE);
  Assert(Int32GreaterThanOrEqual(LoadMapInstanceType(map),
                                 Int32Constant(FIRST_JS_OBJECT_TYPE)));
  return LoadObjectField(
      map, Map::kInObjectPropertiesOrConstructorFunctionIndexOffset,
      MachineType::Uint8());
}

Node* CodeStubAssembler::LoadNameHashField(Node* name) {
  return LoadObjectField(name, Name::kHashFieldOffset, MachineType::Uint32());
}

Node* CodeStubAssembler::LoadNameHash(Node* name, Label* if_hash_not_computed) {
  Node* hash_field = LoadNameHashField(name);
  if (if_hash_not_computed != nullptr) {
    GotoIf(WordEqual(
               Word32And(hash_field, Int32Constant(Name::kHashNotComputedMask)),
               Int32Constant(0)),
           if_hash_not_computed);
  }
  return Word32Shr(hash_field, Int32Constant(Name::kHashShift));
}

Node* CodeStubAssembler::LoadStringLength(Node* object) {
  return LoadObjectField(object, String::kLengthOffset);
}

Node* CodeStubAssembler::LoadJSValueValue(Node* object) {
  return LoadObjectField(object, JSValue::kValueOffset);
}

Node* CodeStubAssembler::LoadWeakCellValue(Node* weak_cell, Label* if_cleared) {
  Node* value = LoadObjectField(weak_cell, WeakCell::kValueOffset);
  if (if_cleared != nullptr) {
    GotoIf(WordEqual(value, IntPtrConstant(0)), if_cleared);
  }
  return value;
}

Node* CodeStubAssembler::AllocateUninitializedFixedArray(Node* length) {
  Node* header_size = IntPtrConstant(FixedArray::kHeaderSize);
  Node* data_size = WordShl(length, IntPtrConstant(kPointerSizeLog2));
  Node* total_size = IntPtrAdd(data_size, header_size);

  Node* result = Allocate(total_size, kNone);
  StoreMapNoWriteBarrier(result, LoadRoot(Heap::kFixedArrayMapRootIndex));
  StoreObjectFieldNoWriteBarrier(result, FixedArray::kLengthOffset,
      SmiTag(length));

  return result;
}

Node* CodeStubAssembler::LoadFixedArrayElement(Node* object, Node* index_node,
                                               int additional_offset,
                                               ParameterMode parameter_mode) {
  int32_t header_size =
      FixedArray::kHeaderSize + additional_offset - kHeapObjectTag;
  Node* offset = ElementOffsetFromIndex(index_node, FAST_HOLEY_ELEMENTS,
                                        parameter_mode, header_size);
  return Load(MachineType::AnyTagged(), object, offset);
}

Node* CodeStubAssembler::LoadFixedDoubleArrayElement(
    Node* object, Node* index_node, MachineType machine_type,
    int additional_offset, ParameterMode parameter_mode) {
  int32_t header_size =
      FixedDoubleArray::kHeaderSize + additional_offset - kHeapObjectTag;
  Node* offset = ElementOffsetFromIndex(index_node, FAST_HOLEY_DOUBLE_ELEMENTS,
                                        parameter_mode, header_size);
  return Load(machine_type, object, offset);
}

Node* CodeStubAssembler::LoadNativeContext(Node* context) {
  return LoadFixedArrayElement(context,
                               Int32Constant(Context::NATIVE_CONTEXT_INDEX));
}

Node* CodeStubAssembler::LoadJSArrayElementsMap(ElementsKind kind,
                                                Node* native_context) {
  return LoadFixedArrayElement(native_context,
                               Int32Constant(Context::ArrayMapIndex(kind)));
}

Node* CodeStubAssembler::StoreHeapNumberValue(Node* object, Node* value) {
  return StoreNoWriteBarrier(
      MachineRepresentation::kFloat64, object,
      IntPtrConstant(HeapNumber::kValueOffset - kHeapObjectTag), value);
}

Node* CodeStubAssembler::StoreObjectField(
    Node* object, int offset, Node* value) {
  return Store(MachineRepresentation::kTagged, object,
               IntPtrConstant(offset - kHeapObjectTag), value);
}

Node* CodeStubAssembler::StoreObjectFieldNoWriteBarrier(
    Node* object, int offset, Node* value, MachineRepresentation rep) {
  return StoreNoWriteBarrier(rep, object,
                             IntPtrConstant(offset - kHeapObjectTag), value);
}

Node* CodeStubAssembler::StoreMapNoWriteBarrier(Node* object, Node* map) {
  return StoreNoWriteBarrier(
      MachineRepresentation::kTagged, object,
      IntPtrConstant(HeapNumber::kMapOffset - kHeapObjectTag), map);
}

Node* CodeStubAssembler::StoreFixedArrayElement(Node* object, Node* index_node,
                                                Node* value,
                                                WriteBarrierMode barrier_mode,
                                                ParameterMode parameter_mode) {
  DCHECK(barrier_mode == SKIP_WRITE_BARRIER ||
         barrier_mode == UPDATE_WRITE_BARRIER);
  Node* offset =
      ElementOffsetFromIndex(index_node, FAST_HOLEY_ELEMENTS, parameter_mode,
                             FixedArray::kHeaderSize - kHeapObjectTag);
  MachineRepresentation rep = MachineRepresentation::kTagged;
  if (barrier_mode == SKIP_WRITE_BARRIER) {
    return StoreNoWriteBarrier(rep, object, offset, value);
  } else {
    return Store(rep, object, offset, value);
  }
}

Node* CodeStubAssembler::StoreFixedDoubleArrayElement(
    Node* object, Node* index_node, Node* value, ParameterMode parameter_mode) {
  Node* offset =
      ElementOffsetFromIndex(index_node, FAST_DOUBLE_ELEMENTS, parameter_mode,
                             FixedArray::kHeaderSize - kHeapObjectTag);
  MachineRepresentation rep = MachineRepresentation::kFloat64;
  return StoreNoWriteBarrier(rep, object, offset, value);
}

Node* CodeStubAssembler::AllocateHeapNumber() {
  Node* result = Allocate(HeapNumber::kSize, kNone);
  StoreMapNoWriteBarrier(result, HeapNumberMapConstant());
  return result;
}

Node* CodeStubAssembler::AllocateHeapNumberWithValue(Node* value) {
  Node* result = AllocateHeapNumber();
  StoreHeapNumberValue(result, value);
  return result;
}

Node* CodeStubAssembler::AllocateSeqOneByteString(int length) {
  Node* result = Allocate(SeqOneByteString::SizeFor(length));
  StoreMapNoWriteBarrier(result, LoadRoot(Heap::kOneByteStringMapRootIndex));
  StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kLengthOffset,
                                 SmiConstant(Smi::FromInt(length)));
  StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kHashFieldOffset,
                                 IntPtrConstant(String::kEmptyHashField),
                                 MachineRepresentation::kWord32);
  return result;
}

Node* CodeStubAssembler::AllocateSeqOneByteString(Node* context, Node* length) {
  Variable var_result(this, MachineRepresentation::kTagged);

  // Compute the SeqOneByteString size and check if it fits into new space.
  Label if_sizeissmall(this), if_notsizeissmall(this, Label::kDeferred),
      if_join(this);
  Node* size = WordAnd(
      IntPtrAdd(
          IntPtrAdd(length, IntPtrConstant(SeqOneByteString::kHeaderSize)),
          IntPtrConstant(kObjectAlignmentMask)),
      IntPtrConstant(~kObjectAlignmentMask));
  Branch(IntPtrLessThanOrEqual(size,
                               IntPtrConstant(Page::kMaxRegularHeapObjectSize)),
         &if_sizeissmall, &if_notsizeissmall);

  Bind(&if_sizeissmall);
  {
    // Just allocate the SeqOneByteString in new space.
    Node* result = Allocate(size);
    StoreMapNoWriteBarrier(result, LoadRoot(Heap::kOneByteStringMapRootIndex));
    StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kLengthOffset,
                                   SmiFromWord(length));
    StoreObjectFieldNoWriteBarrier(result, SeqOneByteString::kHashFieldOffset,
                                   IntPtrConstant(String::kEmptyHashField),
                                   MachineRepresentation::kWord32);
    var_result.Bind(result);
    Goto(&if_join);
  }

  Bind(&if_notsizeissmall);
  {
    // We might need to allocate in large object space, go to the runtime.
    Node* result = CallRuntime(Runtime::kAllocateSeqOneByteString, context,
                               SmiFromWord(length));
    var_result.Bind(result);
    Goto(&if_join);
  }

  Bind(&if_join);
  return var_result.value();
}

Node* CodeStubAssembler::AllocateSeqTwoByteString(int length) {
  Node* result = Allocate(SeqTwoByteString::SizeFor(length));
  StoreMapNoWriteBarrier(result, LoadRoot(Heap::kStringMapRootIndex));
  StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kLengthOffset,
                                 SmiConstant(Smi::FromInt(length)));
  StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kHashFieldOffset,
                                 IntPtrConstant(String::kEmptyHashField),
                                 MachineRepresentation::kWord32);
  return result;
}

Node* CodeStubAssembler::AllocateSeqTwoByteString(Node* context, Node* length) {
  Variable var_result(this, MachineRepresentation::kTagged);

  // Compute the SeqTwoByteString size and check if it fits into new space.
  Label if_sizeissmall(this), if_notsizeissmall(this, Label::kDeferred),
      if_join(this);
  Node* size = WordAnd(
      IntPtrAdd(IntPtrAdd(WordShl(length, 1),
                          IntPtrConstant(SeqTwoByteString::kHeaderSize)),
                IntPtrConstant(kObjectAlignmentMask)),
      IntPtrConstant(~kObjectAlignmentMask));
  Branch(IntPtrLessThanOrEqual(size,
                               IntPtrConstant(Page::kMaxRegularHeapObjectSize)),
         &if_sizeissmall, &if_notsizeissmall);

  Bind(&if_sizeissmall);
  {
    // Just allocate the SeqTwoByteString in new space.
    Node* result = Allocate(size);
    StoreMapNoWriteBarrier(result, LoadRoot(Heap::kStringMapRootIndex));
    StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kLengthOffset,
                                   SmiFromWord(length));
    StoreObjectFieldNoWriteBarrier(result, SeqTwoByteString::kHashFieldOffset,
                                   IntPtrConstant(String::kEmptyHashField),
                                   MachineRepresentation::kWord32);
    var_result.Bind(result);
    Goto(&if_join);
  }

  Bind(&if_notsizeissmall);
  {
    // We might need to allocate in large object space, go to the runtime.
    Node* result = CallRuntime(Runtime::kAllocateSeqTwoByteString, context,
                               SmiFromWord(length));
    var_result.Bind(result);
    Goto(&if_join);
  }

  Bind(&if_join);
  return var_result.value();
}

Node* CodeStubAssembler::AllocateJSArray(ElementsKind kind, Node* array_map,
                                         Node* capacity_node, Node* length_node,
                                         compiler::Node* allocation_site,
                                         ParameterMode mode) {
  bool is_double = IsFastDoubleElementsKind(kind);
  int base_size = JSArray::kSize + FixedArray::kHeaderSize;
  int elements_offset = JSArray::kSize;

  Comment("begin allocation of JSArray");

  if (allocation_site != nullptr) {
    base_size += AllocationMemento::kSize;
    elements_offset += AllocationMemento::kSize;
  }

  int32_t capacity;
  bool constant_capacity = ToInt32Constant(capacity_node, capacity);
  Node* total_size =
      ElementOffsetFromIndex(capacity_node, kind, mode, base_size);

  // Allocate both array and elements object, and initialize the JSArray.
  Heap* heap = isolate()->heap();
  Node* array = Allocate(total_size);
  StoreMapNoWriteBarrier(array, array_map);
  Node* empty_properties =
      HeapConstant(Handle<HeapObject>(heap->empty_fixed_array()));
  StoreObjectFieldNoWriteBarrier(array, JSArray::kPropertiesOffset,
                                 empty_properties);
  StoreObjectFieldNoWriteBarrier(
      array, JSArray::kLengthOffset,
      mode == SMI_PARAMETERS ? length_node : SmiTag(length_node));

  if (allocation_site != nullptr) {
    InitializeAllocationMemento(array, JSArray::kSize, allocation_site);
  }

  // Setup elements object.
  Node* elements = InnerAllocate(array, elements_offset);
  StoreObjectFieldNoWriteBarrier(array, JSArray::kElementsOffset, elements);
  Handle<Map> elements_map(is_double ? heap->fixed_double_array_map()
                                     : heap->fixed_array_map());
  StoreMapNoWriteBarrier(elements, HeapConstant(elements_map));
  StoreObjectFieldNoWriteBarrier(
      elements, FixedArray::kLengthOffset,
      mode == SMI_PARAMETERS ? capacity_node : SmiTag(capacity_node));

  int const first_element_offset = FixedArray::kHeaderSize - kHeapObjectTag;
  Node* hole = HeapConstant(Handle<HeapObject>(heap->the_hole_value()));
  Node* double_hole =
      Is64() ? Int64Constant(kHoleNanInt64) : Int32Constant(kHoleNanLower32);
  DCHECK_EQ(kHoleNanLower32, kHoleNanUpper32);
  if (constant_capacity && capacity <= kElementLoopUnrollThreshold) {
    for (int i = 0; i < capacity; ++i) {
      if (is_double) {
        Node* offset = ElementOffsetFromIndex(Int32Constant(i), kind, mode,
                                              first_element_offset);
        // Don't use doubles to store the hole double, since manipulating the
        // signaling NaN used for the hole in C++, e.g. with bit_cast, will
        // change its value on ia32 (the x87 stack is used to return values
        // and stores to the stack silently clear the signalling bit).
        //
        // TODO(danno): When we have a Float32/Float64 wrapper class that
        // preserves double bits during manipulation, remove this code/change
        // this to an indexed Float64 store.
        if (Is64()) {
          StoreNoWriteBarrier(MachineRepresentation::kWord64, elements, offset,
                              double_hole);
        } else {
          StoreNoWriteBarrier(MachineRepresentation::kWord32, elements, offset,
                              double_hole);
          offset = ElementOffsetFromIndex(Int32Constant(i), kind, mode,
                                          first_element_offset + kPointerSize);
          StoreNoWriteBarrier(MachineRepresentation::kWord32, elements, offset,
                              double_hole);
        }
      } else {
        StoreFixedArrayElement(elements, Int32Constant(i), hole,
                               SKIP_WRITE_BARRIER);
      }
    }
  } else {
    Variable current(this, MachineRepresentation::kTagged);
    Label test(this);
    Label decrement(this, &current);
    Label done(this);
    Node* limit = IntPtrAdd(elements, IntPtrConstant(first_element_offset));
    current.Bind(
        IntPtrAdd(limit, ElementOffsetFromIndex(capacity_node, kind, mode, 0)));

    Branch(WordEqual(current.value(), limit), &done, &decrement);

    Bind(&decrement);
    current.Bind(IntPtrSub(
        current.value(),
        Int32Constant(IsFastDoubleElementsKind(kind) ? kDoubleSize
                                                     : kPointerSize)));
    if (is_double) {
      // Don't use doubles to store the hole double, since manipulating the
      // signaling NaN used for the hole in C++, e.g. with bit_cast, will
      // change its value on ia32 (the x87 stack is used to return values
      // and stores to the stack silently clear the signalling bit).
      //
      // TODO(danno): When we have a Float32/Float64 wrapper class that
      // preserves double bits during manipulation, remove this code/change
      // this to an indexed Float64 store.
      if (Is64()) {
        StoreNoWriteBarrier(MachineRepresentation::kWord64, current.value(),
                            double_hole);
      } else {
        StoreNoWriteBarrier(MachineRepresentation::kWord32, current.value(),
                            double_hole);
        StoreNoWriteBarrier(
            MachineRepresentation::kWord32,
            IntPtrAdd(current.value(), Int32Constant(kPointerSize)),
            double_hole);
      }
    } else {
      StoreNoWriteBarrier(MachineRepresentation::kTagged, current.value(),
                          hole);
    }
    Node* compare = WordNotEqual(current.value(), limit);
    Branch(compare, &decrement, &done);

    Bind(&done);
  }

  return array;
}

void CodeStubAssembler::InitializeAllocationMemento(
    compiler::Node* base_allocation, int base_allocation_size,
    compiler::Node* allocation_site) {
  StoreObjectFieldNoWriteBarrier(
      base_allocation, AllocationMemento::kMapOffset + base_allocation_size,
      HeapConstant(Handle<Map>(isolate()->heap()->allocation_memento_map())));
  StoreObjectFieldNoWriteBarrier(
      base_allocation,
      AllocationMemento::kAllocationSiteOffset + base_allocation_size,
      allocation_site);
  if (FLAG_allocation_site_pretenuring) {
    Node* count = LoadObjectField(allocation_site,
                                  AllocationSite::kPretenureCreateCountOffset);
    Node* incremented_count = IntPtrAdd(count, SmiConstant(Smi::FromInt(1)));
    StoreObjectFieldNoWriteBarrier(allocation_site,
                                   AllocationSite::kPretenureCreateCountOffset,
                                   incremented_count);
  }
}

Node* CodeStubAssembler::TruncateTaggedToFloat64(Node* context, Node* value) {
  // We might need to loop once due to ToNumber conversion.
  Variable var_value(this, MachineRepresentation::kTagged),
      var_result(this, MachineRepresentation::kFloat64);
  Label loop(this, &var_value), done_loop(this, &var_result);
  var_value.Bind(value);
  Goto(&loop);
  Bind(&loop);
  {
    // Load the current {value}.
    value = var_value.value();

    // Check if the {value} is a Smi or a HeapObject.
    Label if_valueissmi(this), if_valueisnotsmi(this);
    Branch(WordIsSmi(value), &if_valueissmi, &if_valueisnotsmi);

    Bind(&if_valueissmi);
    {
      // Convert the Smi {value}.
      var_result.Bind(SmiToFloat64(value));
      Goto(&done_loop);
    }

    Bind(&if_valueisnotsmi);
    {
      // Check if {value} is a HeapNumber.
      Label if_valueisheapnumber(this),
          if_valueisnotheapnumber(this, Label::kDeferred);
      Branch(WordEqual(LoadMap(value), HeapNumberMapConstant()),
             &if_valueisheapnumber, &if_valueisnotheapnumber);

      Bind(&if_valueisheapnumber);
      {
        // Load the floating point value.
        var_result.Bind(LoadHeapNumberValue(value));
        Goto(&done_loop);
      }

      Bind(&if_valueisnotheapnumber);
      {
        // Convert the {value} to a Number first.
        Callable callable = CodeFactory::NonNumberToNumber(isolate());
        var_value.Bind(CallStub(callable, context, value));
        Goto(&loop);
      }
    }
  }
  Bind(&done_loop);
  return var_result.value();
}

Node* CodeStubAssembler::TruncateTaggedToWord32(Node* context, Node* value) {
  // We might need to loop once due to ToNumber conversion.
  Variable var_value(this, MachineRepresentation::kTagged),
      var_result(this, MachineRepresentation::kWord32);
  Label loop(this, &var_value), done_loop(this, &var_result);
  var_value.Bind(value);
  Goto(&loop);
  Bind(&loop);
  {
    // Load the current {value}.
    value = var_value.value();

    // Check if the {value} is a Smi or a HeapObject.
    Label if_valueissmi(this), if_valueisnotsmi(this);
    Branch(WordIsSmi(value), &if_valueissmi, &if_valueisnotsmi);

    Bind(&if_valueissmi);
    {
      // Convert the Smi {value}.
      var_result.Bind(SmiToWord32(value));
      Goto(&done_loop);
    }

    Bind(&if_valueisnotsmi);
    {
      // Check if {value} is a HeapNumber.
      Label if_valueisheapnumber(this),
          if_valueisnotheapnumber(this, Label::kDeferred);
      Branch(WordEqual(LoadMap(value), HeapNumberMapConstant()),
             &if_valueisheapnumber, &if_valueisnotheapnumber);

      Bind(&if_valueisheapnumber);
      {
        // Truncate the floating point value.
        var_result.Bind(TruncateHeapNumberValueToWord32(value));
        Goto(&done_loop);
      }

      Bind(&if_valueisnotheapnumber);
      {
        // Convert the {value} to a Number first.
        Callable callable = CodeFactory::NonNumberToNumber(isolate());
        var_value.Bind(CallStub(callable, context, value));
        Goto(&loop);
      }
    }
  }
  Bind(&done_loop);
  return var_result.value();
}

Node* CodeStubAssembler::TruncateHeapNumberValueToWord32(Node* object) {
  Node* value = LoadHeapNumberValue(object);
  return TruncateFloat64ToWord32(value);
}

Node* CodeStubAssembler::ChangeFloat64ToTagged(Node* value) {
  Node* value32 = RoundFloat64ToInt32(value);
  Node* value64 = ChangeInt32ToFloat64(value32);

  Label if_valueisint32(this), if_valueisheapnumber(this), if_join(this);

  Label if_valueisequal(this), if_valueisnotequal(this);
  Branch(Float64Equal(value, value64), &if_valueisequal, &if_valueisnotequal);
  Bind(&if_valueisequal);
  {
    GotoUnless(Word32Equal(value32, Int32Constant(0)), &if_valueisint32);
    BranchIfInt32LessThan(Float64ExtractHighWord32(value), Int32Constant(0),
                          &if_valueisheapnumber, &if_valueisint32);
  }
  Bind(&if_valueisnotequal);
  Goto(&if_valueisheapnumber);

  Variable var_result(this, MachineRepresentation::kTagged);
  Bind(&if_valueisint32);
  {
    if (Is64()) {
      Node* result = SmiTag(ChangeInt32ToInt64(value32));
      var_result.Bind(result);
      Goto(&if_join);
    } else {
      Node* pair = Int32AddWithOverflow(value32, value32);
      Node* overflow = Projection(1, pair);
      Label if_overflow(this, Label::kDeferred), if_notoverflow(this);
      Branch(overflow, &if_overflow, &if_notoverflow);
      Bind(&if_overflow);
      Goto(&if_valueisheapnumber);
      Bind(&if_notoverflow);
      {
        Node* result = Projection(0, pair);
        var_result.Bind(result);
        Goto(&if_join);
      }
    }
  }
  Bind(&if_valueisheapnumber);
  {
    Node* result = AllocateHeapNumberWithValue(value);
    var_result.Bind(result);
    Goto(&if_join);
  }
  Bind(&if_join);
  return var_result.value();
}

Node* CodeStubAssembler::ChangeInt32ToTagged(Node* value) {
  if (Is64()) {
    return SmiTag(ChangeInt32ToInt64(value));
  }
  Variable var_result(this, MachineRepresentation::kTagged);
  Node* pair = Int32AddWithOverflow(value, value);
  Node* overflow = Projection(1, pair);
  Label if_overflow(this, Label::kDeferred), if_notoverflow(this),
      if_join(this);
  Branch(overflow, &if_overflow, &if_notoverflow);
  Bind(&if_overflow);
  {
    Node* value64 = ChangeInt32ToFloat64(value);
    Node* result = AllocateHeapNumberWithValue(value64);
    var_result.Bind(result);
  }
  Goto(&if_join);
  Bind(&if_notoverflow);
  {
    Node* result = Projection(0, pair);
    var_result.Bind(result);
  }
  Goto(&if_join);
  Bind(&if_join);
  return var_result.value();
}

Node* CodeStubAssembler::ChangeUint32ToTagged(Node* value) {
  Label if_overflow(this, Label::kDeferred), if_not_overflow(this),
      if_join(this);
  Variable var_result(this, MachineRepresentation::kTagged);
  // If {value} > 2^31 - 1, we need to store it in a HeapNumber.
  Branch(Int32LessThan(value, Int32Constant(0)), &if_overflow,
         &if_not_overflow);
  Bind(&if_not_overflow);
  {
    if (Is64()) {
      var_result.Bind(SmiTag(ChangeUint32ToUint64(value)));
    } else {
      // If tagging {value} results in an overflow, we need to use a HeapNumber
      // to represent it.
      Node* pair = Int32AddWithOverflow(value, value);
      Node* overflow = Projection(1, pair);
      GotoIf(overflow, &if_overflow);

      Node* result = Projection(0, pair);
      var_result.Bind(result);
    }
  }
  Goto(&if_join);

  Bind(&if_overflow);
  {
    Node* float64_value = ChangeUint32ToFloat64(value);
    var_result.Bind(AllocateHeapNumberWithValue(float64_value));
  }
  Goto(&if_join);

  Bind(&if_join);
  return var_result.value();
}

Node* CodeStubAssembler::ToThisString(Node* context, Node* value,
                                      char const* method_name) {
  Variable var_value(this, MachineRepresentation::kTagged);
  var_value.Bind(value);

  // Check if the {value} is a Smi or a HeapObject.
  Label if_valueissmi(this, Label::kDeferred), if_valueisnotsmi(this),
      if_valueisstring(this);
  Branch(WordIsSmi(value), &if_valueissmi, &if_valueisnotsmi);
  Bind(&if_valueisnotsmi);
  {
    // Load the instance type of the {value}.
    Node* value_instance_type = LoadInstanceType(value);

    // Check if the {value} is already String.
    Label if_valueisnotstring(this, Label::kDeferred);
    Branch(
        Int32LessThan(value_instance_type, Int32Constant(FIRST_NONSTRING_TYPE)),
        &if_valueisstring, &if_valueisnotstring);
    Bind(&if_valueisnotstring);
    {
      // Check if the {value} is null.
      Label if_valueisnullorundefined(this, Label::kDeferred),
          if_valueisnotnullorundefined(this, Label::kDeferred),
          if_valueisnotnull(this, Label::kDeferred);
      Branch(WordEqual(value, NullConstant()), &if_valueisnullorundefined,
             &if_valueisnotnull);
      Bind(&if_valueisnotnull);
      {
        // Check if the {value} is undefined.
        Branch(WordEqual(value, UndefinedConstant()),
               &if_valueisnullorundefined, &if_valueisnotnullorundefined);
        Bind(&if_valueisnotnullorundefined);
        {
          // Convert the {value} to a String.
          Callable callable = CodeFactory::ToString(isolate());
          var_value.Bind(CallStub(callable, context, value));
          Goto(&if_valueisstring);
        }
      }

      Bind(&if_valueisnullorundefined);
      {
        // The {value} is either null or undefined.
        CallRuntime(Runtime::kThrowCalledOnNullOrUndefined, context,
                    HeapConstant(factory()->NewStringFromAsciiChecked(
                        method_name, TENURED)));
        Goto(&if_valueisstring);  // Never reached.
      }
    }
  }
  Bind(&if_valueissmi);
  {
    // The {value} is a Smi, convert it to a String.
    Callable callable = CodeFactory::NumberToString(isolate());
    var_value.Bind(CallStub(callable, context, value));
    Goto(&if_valueisstring);
  }
  Bind(&if_valueisstring);
  return var_value.value();
}

Node* CodeStubAssembler::StringCharCodeAt(Node* string, Node* index) {
  // Translate the {index} into a Word.
  index = SmiToWord(index);

  // We may need to loop in case of cons or sliced strings.
  Variable var_index(this, MachineType::PointerRepresentation());
  Variable var_result(this, MachineRepresentation::kWord32);
  Variable var_string(this, MachineRepresentation::kTagged);
  Variable* loop_vars[] = {&var_index, &var_string};
  Label done_loop(this, &var_result), loop(this, 2, loop_vars);
  var_string.Bind(string);
  var_index.Bind(index);
  Goto(&loop);
  Bind(&loop);
  {
    // Load the current {index}.
    index = var_index.value();

    // Load the current {string}.
    string = var_string.value();

    // Load the instance type of the {string}.
    Node* string_instance_type = LoadInstanceType(string);

    // Check if the {string} is a SeqString.
    Label if_stringissequential(this), if_stringisnotsequential(this);
    Branch(Word32Equal(Word32And(string_instance_type,
                                 Int32Constant(kStringRepresentationMask)),
                       Int32Constant(kSeqStringTag)),
           &if_stringissequential, &if_stringisnotsequential);

    Bind(&if_stringissequential);
    {
      // Check if the {string} is a TwoByteSeqString or a OneByteSeqString.
      Label if_stringistwobyte(this), if_stringisonebyte(this);
      Branch(Word32Equal(Word32And(string_instance_type,
                                   Int32Constant(kStringEncodingMask)),
                         Int32Constant(kTwoByteStringTag)),
             &if_stringistwobyte, &if_stringisonebyte);

      Bind(&if_stringisonebyte);
      {
        var_result.Bind(
            Load(MachineType::Uint8(), string,
                 IntPtrAdd(index, IntPtrConstant(SeqOneByteString::kHeaderSize -
                                                 kHeapObjectTag))));
        Goto(&done_loop);
      }

      Bind(&if_stringistwobyte);
      {
        var_result.Bind(
            Load(MachineType::Uint16(), string,
                 IntPtrAdd(WordShl(index, IntPtrConstant(1)),
                           IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                          kHeapObjectTag))));
        Goto(&done_loop);
      }
    }

    Bind(&if_stringisnotsequential);
    {
      // Check if the {string} is a ConsString.
      Label if_stringiscons(this), if_stringisnotcons(this);
      Branch(Word32Equal(Word32And(string_instance_type,
                                   Int32Constant(kStringRepresentationMask)),
                         Int32Constant(kConsStringTag)),
             &if_stringiscons, &if_stringisnotcons);

      Bind(&if_stringiscons);
      {
        // Check whether the right hand side is the empty string (i.e. if
        // this is really a flat string in a cons string). If that is not
        // the case we flatten the string first.
        Label if_rhsisempty(this), if_rhsisnotempty(this, Label::kDeferred);
        Node* rhs = LoadObjectField(string, ConsString::kSecondOffset);
        Branch(WordEqual(rhs, EmptyStringConstant()), &if_rhsisempty,
               &if_rhsisnotempty);

        Bind(&if_rhsisempty);
        {
          // Just operate on the left hand side of the {string}.
          var_string.Bind(LoadObjectField(string, ConsString::kFirstOffset));
          Goto(&loop);
        }

        Bind(&if_rhsisnotempty);
        {
          // Flatten the {string} and lookup in the resulting string.
          var_string.Bind(CallRuntime(Runtime::kFlattenString,
                                      NoContextConstant(), string));
          Goto(&loop);
        }
      }

      Bind(&if_stringisnotcons);
      {
        // Check if the {string} is an ExternalString.
        Label if_stringisexternal(this), if_stringisnotexternal(this);
        Branch(Word32Equal(Word32And(string_instance_type,
                                     Int32Constant(kStringRepresentationMask)),
                           Int32Constant(kExternalStringTag)),
               &if_stringisexternal, &if_stringisnotexternal);

        Bind(&if_stringisexternal);
        {
          // Check if the {string} is a short external string.
          Label if_stringisshort(this),
              if_stringisnotshort(this, Label::kDeferred);
          Branch(Word32Equal(Word32And(string_instance_type,
                                       Int32Constant(kShortExternalStringMask)),
                             Int32Constant(0)),
                 &if_stringisshort, &if_stringisnotshort);

          Bind(&if_stringisshort);
          {
            // Load the actual resource data from the {string}.
            Node* string_resource_data =
                LoadObjectField(string, ExternalString::kResourceDataOffset,
                                MachineType::Pointer());

            // Check if the {string} is a TwoByteExternalString or a
            // OneByteExternalString.
            Label if_stringistwobyte(this), if_stringisonebyte(this);
            Branch(Word32Equal(Word32And(string_instance_type,
                                         Int32Constant(kStringEncodingMask)),
                               Int32Constant(kTwoByteStringTag)),
                   &if_stringistwobyte, &if_stringisonebyte);

            Bind(&if_stringisonebyte);
            {
              var_result.Bind(
                  Load(MachineType::Uint8(), string_resource_data, index));
              Goto(&done_loop);
            }

            Bind(&if_stringistwobyte);
            {
              var_result.Bind(Load(MachineType::Uint16(), string_resource_data,
                                   WordShl(index, IntPtrConstant(1))));
              Goto(&done_loop);
            }
          }

          Bind(&if_stringisnotshort);
          {
            // The {string} might be compressed, call the runtime.
            var_result.Bind(SmiToWord32(
                CallRuntime(Runtime::kExternalStringGetChar,
                            NoContextConstant(), string, SmiTag(index))));
            Goto(&done_loop);
          }
        }

        Bind(&if_stringisnotexternal);
        {
          // The {string} is a SlicedString, continue with its parent.
          Node* string_offset =
              SmiToWord(LoadObjectField(string, SlicedString::kOffsetOffset));
          Node* string_parent =
              LoadObjectField(string, SlicedString::kParentOffset);
          var_index.Bind(IntPtrAdd(index, string_offset));
          var_string.Bind(string_parent);
          Goto(&loop);
        }
      }
    }
  }

  Bind(&done_loop);
  return var_result.value();
}

Node* CodeStubAssembler::StringFromCharCode(Node* code) {
  Variable var_result(this, MachineRepresentation::kTagged);

  // Check if the {code} is a one-byte char code.
  Label if_codeisonebyte(this), if_codeistwobyte(this, Label::kDeferred),
      if_done(this);
  Branch(Int32LessThanOrEqual(code, Int32Constant(String::kMaxOneByteCharCode)),
         &if_codeisonebyte, &if_codeistwobyte);
  Bind(&if_codeisonebyte);
  {
    // Load the isolate wide single character string cache.
    Node* cache = LoadRoot(Heap::kSingleCharacterStringCacheRootIndex);

    // Check if we have an entry for the {code} in the single character string
    // cache already.
    Label if_entryisundefined(this, Label::kDeferred),
        if_entryisnotundefined(this);
    Node* entry = LoadFixedArrayElement(cache, code);
    Branch(WordEqual(entry, UndefinedConstant()), &if_entryisundefined,
           &if_entryisnotundefined);

    Bind(&if_entryisundefined);
    {
      // Allocate a new SeqOneByteString for {code} and store it in the {cache}.
      Node* result = AllocateSeqOneByteString(1);
      StoreNoWriteBarrier(
          MachineRepresentation::kWord8, result,
          IntPtrConstant(SeqOneByteString::kHeaderSize - kHeapObjectTag), code);
      StoreFixedArrayElement(cache, code, result);
      var_result.Bind(result);
      Goto(&if_done);
    }

    Bind(&if_entryisnotundefined);
    {
      // Return the entry from the {cache}.
      var_result.Bind(entry);
      Goto(&if_done);
    }
  }

  Bind(&if_codeistwobyte);
  {
    // Allocate a new SeqTwoByteString for {code}.
    Node* result = AllocateSeqTwoByteString(1);
    StoreNoWriteBarrier(
        MachineRepresentation::kWord16, result,
        IntPtrConstant(SeqTwoByteString::kHeaderSize - kHeapObjectTag), code);
    var_result.Bind(result);
    Goto(&if_done);
  }

  Bind(&if_done);
  return var_result.value();
}

Node* CodeStubAssembler::BitFieldDecode(Node* word32, uint32_t shift,
                                        uint32_t mask) {
  return Word32Shr(Word32And(word32, Int32Constant(mask)),
                   Int32Constant(shift));
}

void CodeStubAssembler::SetCounter(StatsCounter* counter, int value) {
  if (FLAG_native_code_counters && counter->Enabled()) {
    Node* counter_address = ExternalConstant(ExternalReference(counter));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, counter_address,
                        Int32Constant(value));
  }
}

void CodeStubAssembler::IncrementCounter(StatsCounter* counter, int delta) {
  DCHECK(delta > 0);
  if (FLAG_native_code_counters && counter->Enabled()) {
    Node* counter_address = ExternalConstant(ExternalReference(counter));
    Node* value = Load(MachineType::Int32(), counter_address);
    value = Int32Add(value, Int32Constant(delta));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, counter_address, value);
  }
}

void CodeStubAssembler::DecrementCounter(StatsCounter* counter, int delta) {
  DCHECK(delta > 0);
  if (FLAG_native_code_counters && counter->Enabled()) {
    Node* counter_address = ExternalConstant(ExternalReference(counter));
    Node* value = Load(MachineType::Int32(), counter_address);
    value = Int32Sub(value, Int32Constant(delta));
    StoreNoWriteBarrier(MachineRepresentation::kWord32, counter_address, value);
  }
}

void CodeStubAssembler::TryToName(Node* key, Label* if_keyisindex,
                                  Variable* var_index, Label* if_keyisunique,
                                  Label* if_bailout) {
  DCHECK_EQ(MachineRepresentation::kWord32, var_index->rep());
  Comment("TryToName");

  Label if_keyissmi(this), if_keyisnotsmi(this);
  Branch(WordIsSmi(key), &if_keyissmi, &if_keyisnotsmi);
  Bind(&if_keyissmi);
  {
    // Negative smi keys are named properties. Handle in the runtime.
    GotoUnless(WordIsPositiveSmi(key), if_bailout);

    var_index->Bind(SmiToWord32(key));
    Goto(if_keyisindex);
  }

  Bind(&if_keyisnotsmi);

  Node* key_instance_type = LoadInstanceType(key);
  // Symbols are unique.
  GotoIf(Word32Equal(key_instance_type, Int32Constant(SYMBOL_TYPE)),
         if_keyisunique);

  Label if_keyisinternalized(this);
  Node* bits =
      WordAnd(key_instance_type,
              Int32Constant(kIsNotStringMask | kIsNotInternalizedMask));
  Branch(Word32Equal(bits, Int32Constant(kStringTag | kInternalizedTag)),
         &if_keyisinternalized, if_bailout);
  Bind(&if_keyisinternalized);

  // Check whether the key is an array index passed in as string. Handle
  // uniform with smi keys if so.
  // TODO(verwaest): Also support non-internalized strings.
  Node* hash = LoadNameHashField(key);
  Node* bit = Word32And(hash, Int32Constant(Name::kIsNotArrayIndexMask));
  GotoIf(Word32NotEqual(bit, Int32Constant(0)), if_keyisunique);
  // Key is an index. Check if it is small enough to be encoded in the
  // hash_field. Handle too big array index in runtime.
  bit = Word32And(hash, Int32Constant(Name::kContainsCachedArrayIndexMask));
  GotoIf(Word32NotEqual(bit, Int32Constant(0)), if_bailout);
  var_index->Bind(BitFieldDecode<Name::ArrayIndexValueBits>(hash));
  Goto(if_keyisindex);
}

template <typename Dictionary>
Node* CodeStubAssembler::EntryToIndex(Node* entry, int field_index) {
  Node* entry_index = Int32Mul(entry, Int32Constant(Dictionary::kEntrySize));
  return Int32Add(entry_index,
                  Int32Constant(Dictionary::kElementsStartIndex + field_index));
}

template <typename Dictionary>
void CodeStubAssembler::NameDictionaryLookup(Node* dictionary,
                                             Node* unique_name, Label* if_found,
                                             Variable* var_name_index,
                                             Label* if_not_found,
                                             int inlined_probes) {
  DCHECK_EQ(MachineRepresentation::kWord32, var_name_index->rep());
  Comment("NameDictionaryLookup");

  Node* capacity = SmiToWord32(LoadFixedArrayElement(
      dictionary, Int32Constant(Dictionary::kCapacityIndex)));
  Node* mask = Int32Sub(capacity, Int32Constant(1));
  Node* hash = LoadNameHash(unique_name);

  // See Dictionary::FirstProbe().
  Node* count = Int32Constant(0);
  Node* entry = Word32And(hash, mask);

  for (int i = 0; i < inlined_probes; i++) {
    Node* index = EntryToIndex<Dictionary>(entry);
    var_name_index->Bind(index);

    Node* current = LoadFixedArrayElement(dictionary, index);
    GotoIf(WordEqual(current, unique_name), if_found);

    // See Dictionary::NextProbe().
    count = Int32Constant(i + 1);
    entry = Word32And(Int32Add(entry, count), mask);
  }

  Node* undefined = UndefinedConstant();

  Variable var_count(this, MachineRepresentation::kWord32);
  Variable var_entry(this, MachineRepresentation::kWord32);
  Variable* loop_vars[] = {&var_count, &var_entry, var_name_index};
  Label loop(this, 3, loop_vars);
  var_count.Bind(count);
  var_entry.Bind(entry);
  Goto(&loop);
  Bind(&loop);
  {
    Node* count = var_count.value();
    Node* entry = var_entry.value();

    Node* index = EntryToIndex<Dictionary>(entry);
    var_name_index->Bind(index);

    Node* current = LoadFixedArrayElement(dictionary, index);
    GotoIf(WordEqual(current, undefined), if_not_found);
    GotoIf(WordEqual(current, unique_name), if_found);

    // See Dictionary::NextProbe().
    count = Int32Add(count, Int32Constant(1));
    entry = Word32And(Int32Add(entry, count), mask);

    var_count.Bind(count);
    var_entry.Bind(entry);
    Goto(&loop);
  }
}

// Instantiate template methods to workaround GCC compilation issue.
template void CodeStubAssembler::NameDictionaryLookup<NameDictionary>(
    Node*, Node*, Label*, Variable*, Label*, int);
template void CodeStubAssembler::NameDictionaryLookup<GlobalDictionary>(
    Node*, Node*, Label*, Variable*, Label*, int);

Node* CodeStubAssembler::ComputeIntegerHash(Node* key, Node* seed) {
  // See v8::internal::ComputeIntegerHash()
  Node* hash = key;
  hash = Word32Xor(hash, seed);
  hash = Int32Add(Word32Xor(hash, Int32Constant(0xffffffff)),
                  Word32Shl(hash, Int32Constant(15)));
  hash = Word32Xor(hash, Word32Shr(hash, Int32Constant(12)));
  hash = Int32Add(hash, Word32Shl(hash, Int32Constant(2)));
  hash = Word32Xor(hash, Word32Shr(hash, Int32Constant(4)));
  hash = Int32Mul(hash, Int32Constant(2057));
  hash = Word32Xor(hash, Word32Shr(hash, Int32Constant(16)));
  return Word32And(hash, Int32Constant(0x3fffffff));
}

template <typename Dictionary>
void CodeStubAssembler::NumberDictionaryLookup(Node* dictionary, Node* key,
                                               Label* if_found,
                                               Variable* var_entry,
                                               Label* if_not_found) {
  DCHECK_EQ(MachineRepresentation::kWord32, var_entry->rep());
  Comment("NumberDictionaryLookup");

  Node* capacity = SmiToWord32(LoadFixedArrayElement(
      dictionary, Int32Constant(Dictionary::kCapacityIndex)));
  Node* mask = Int32Sub(capacity, Int32Constant(1));

  Node* seed;
  if (Dictionary::ShapeT::UsesSeed) {
    seed = HashSeed();
  } else {
    seed = Int32Constant(kZeroHashSeed);
  }
  Node* hash = ComputeIntegerHash(key, seed);
  Node* key_as_float64 = ChangeUint32ToFloat64(key);

  // See Dictionary::FirstProbe().
  Node* count = Int32Constant(0);
  Node* entry = Word32And(hash, mask);

  Node* undefined = UndefinedConstant();
  Node* the_hole = TheHoleConstant();

  Variable var_count(this, MachineRepresentation::kWord32);
  Variable* loop_vars[] = {&var_count, var_entry};
  Label loop(this, 2, loop_vars);
  var_count.Bind(count);
  var_entry->Bind(entry);
  Goto(&loop);
  Bind(&loop);
  {
    Node* count = var_count.value();
    Node* entry = var_entry->value();

    Node* index = EntryToIndex<Dictionary>(entry);
    Node* current = LoadFixedArrayElement(dictionary, index);
    GotoIf(WordEqual(current, undefined), if_not_found);
    Label next_probe(this);
    {
      Label if_currentissmi(this), if_currentisnotsmi(this);
      Branch(WordIsSmi(current), &if_currentissmi, &if_currentisnotsmi);
      Bind(&if_currentissmi);
      {
        Node* current_value = SmiToWord32(current);
        Branch(Word32Equal(current_value, key), if_found, &next_probe);
      }
      Bind(&if_currentisnotsmi);
      {
        GotoIf(WordEqual(current, the_hole), &next_probe);
        // Current must be the Number.
        Node* current_value = LoadHeapNumberValue(current);
        Branch(Float64Equal(current_value, key_as_float64), if_found,
               &next_probe);
      }
    }

    Bind(&next_probe);
    // See Dictionary::NextProbe().
    count = Int32Add(count, Int32Constant(1));
    entry = Word32And(Int32Add(entry, count), mask);

    var_count.Bind(count);
    var_entry->Bind(entry);
    Goto(&loop);
  }
}

void CodeStubAssembler::TryLookupProperty(
    Node* object, Node* map, Node* instance_type, Node* unique_name,
    Label* if_found_fast, Label* if_found_dict, Label* if_found_global,
    Variable* var_meta_storage, Variable* var_name_index, Label* if_not_found,
    Label* if_bailout) {
  DCHECK_EQ(MachineRepresentation::kTagged, var_meta_storage->rep());
  DCHECK_EQ(MachineRepresentation::kWord32, var_name_index->rep());

  Label if_objectisspecial(this);
  STATIC_ASSERT(JS_GLOBAL_OBJECT_TYPE <= LAST_SPECIAL_RECEIVER_TYPE);
  GotoIf(Int32LessThanOrEqual(instance_type,
                              Int32Constant(LAST_SPECIAL_RECEIVER_TYPE)),
         &if_objectisspecial);

  Node* bit_field = LoadMapBitField(map);
  Node* mask = Int32Constant(1 << Map::kHasNamedInterceptor |
                             1 << Map::kIsAccessCheckNeeded);
  Assert(Word32Equal(Word32And(bit_field, mask), Int32Constant(0)));

  Node* bit_field3 = LoadMapBitField3(map);
  Node* bit = BitFieldDecode<Map::DictionaryMap>(bit_field3);
  Label if_isfastmap(this), if_isslowmap(this);
  Branch(Word32Equal(bit, Int32Constant(0)), &if_isfastmap, &if_isslowmap);
  Bind(&if_isfastmap);
  {
    Comment("DescriptorArrayLookup");
    Node* nof = BitFieldDecode<Map::NumberOfOwnDescriptorsBits>(bit_field3);
    // Bail out to the runtime for large numbers of own descriptors. The stub
    // only does linear search, which becomes too expensive in that case.
    {
      static const int32_t kMaxLinear = 210;
      GotoIf(Int32GreaterThan(nof, Int32Constant(kMaxLinear)), if_bailout);
    }
    Node* descriptors = LoadMapDescriptors(map);
    var_meta_storage->Bind(descriptors);

    Variable var_descriptor(this, MachineRepresentation::kWord32);
    Label loop(this, &var_descriptor);
    var_descriptor.Bind(Int32Constant(0));
    Goto(&loop);
    Bind(&loop);
    {
      Node* index = var_descriptor.value();
      Node* name_offset = Int32Constant(DescriptorArray::ToKeyIndex(0));
      Node* factor = Int32Constant(DescriptorArray::kDescriptorSize);
      GotoIf(Word32Equal(index, nof), if_not_found);

      Node* name_index = Int32Add(name_offset, Int32Mul(index, factor));
      Node* name = LoadFixedArrayElement(descriptors, name_index);

      var_name_index->Bind(name_index);
      GotoIf(WordEqual(name, unique_name), if_found_fast);

      var_descriptor.Bind(Int32Add(index, Int32Constant(1)));
      Goto(&loop);
    }
  }
  Bind(&if_isslowmap);
  {
    Node* dictionary = LoadProperties(object);
    var_meta_storage->Bind(dictionary);

    NameDictionaryLookup<NameDictionary>(dictionary, unique_name, if_found_dict,
                                         var_name_index, if_not_found);
  }
  Bind(&if_objectisspecial);
  {
    // Handle global object here and other special objects in runtime.
    GotoUnless(Word32Equal(instance_type, Int32Constant(JS_GLOBAL_OBJECT_TYPE)),
               if_bailout);

    // Handle interceptors and access checks in runtime.
    Node* bit_field = LoadMapBitField(map);
    Node* mask = Int32Constant(1 << Map::kHasNamedInterceptor |
                               1 << Map::kIsAccessCheckNeeded);
    GotoIf(Word32NotEqual(Word32And(bit_field, mask), Int32Constant(0)),
           if_bailout);

    Node* dictionary = LoadProperties(object);
    var_meta_storage->Bind(dictionary);

    NameDictionaryLookup<GlobalDictionary>(
        dictionary, unique_name, if_found_global, var_name_index, if_not_found);
  }
}

void CodeStubAssembler::TryHasOwnProperty(compiler::Node* object,
                                          compiler::Node* map,
                                          compiler::Node* instance_type,
                                          compiler::Node* unique_name,
                                          Label* if_found, Label* if_not_found,
                                          Label* if_bailout) {
  Comment("TryHasOwnProperty");
  Variable var_meta_storage(this, MachineRepresentation::kTagged);
  Variable var_name_index(this, MachineRepresentation::kWord32);

  Label if_found_global(this);
  TryLookupProperty(object, map, instance_type, unique_name, if_found, if_found,
                    &if_found_global, &var_meta_storage, &var_name_index,
                    if_not_found, if_bailout);
  Bind(&if_found_global);
  {
    Variable var_value(this, MachineRepresentation::kTagged);
    Variable var_details(this, MachineRepresentation::kWord32);
    // Check if the property cell is not deleted.
    LoadPropertyFromGlobalDictionary(var_meta_storage.value(),
                                     var_name_index.value(), &var_value,
                                     &var_details, if_not_found);
    Goto(if_found);
  }
}

void CodeStubAssembler::LoadPropertyFromFastObject(Node* object, Node* map,
                                                   Node* descriptors,
                                                   Node* name_index,
                                                   Variable* var_details,
                                                   Variable* var_value) {
  DCHECK_EQ(MachineRepresentation::kWord32, var_details->rep());
  DCHECK_EQ(MachineRepresentation::kTagged, var_value->rep());
  Comment("[ LoadPropertyFromFastObject");

  const int name_to_details_offset =
      (DescriptorArray::kDescriptorDetails - DescriptorArray::kDescriptorKey) *
      kPointerSize;
  const int name_to_value_offset =
      (DescriptorArray::kDescriptorValue - DescriptorArray::kDescriptorKey) *
      kPointerSize;

  Node* details = SmiToWord32(
      LoadFixedArrayElement(descriptors, name_index, name_to_details_offset));
  var_details->Bind(details);

  Node* location = BitFieldDecode<PropertyDetails::LocationField>(details);

  Label if_in_field(this), if_in_descriptor(this), done(this);
  Branch(Word32Equal(location, Int32Constant(kField)), &if_in_field,
         &if_in_descriptor);
  Bind(&if_in_field);
  {
    Node* field_index =
        BitFieldDecode<PropertyDetails::FieldIndexField>(details);
    Node* representation =
        BitFieldDecode<PropertyDetails::RepresentationField>(details);

    Node* inobject_properties = LoadMapInobjectProperties(map);

    Label if_inobject(this), if_backing_store(this);
    Variable var_double_value(this, MachineRepresentation::kFloat64);
    Label rebox_double(this, &var_double_value);
    BranchIfInt32LessThan(field_index, inobject_properties, &if_inobject,
                          &if_backing_store);
    Bind(&if_inobject);
    {
      Comment("if_inobject");
      Node* field_offset = ChangeInt32ToIntPtr(
          Int32Mul(Int32Sub(LoadMapInstanceSize(map),
                            Int32Sub(inobject_properties, field_index)),
                   Int32Constant(kPointerSize)));

      Label if_double(this), if_tagged(this);
      BranchIfWord32NotEqual(representation,
                             Int32Constant(Representation::kDouble), &if_tagged,
                             &if_double);
      Bind(&if_tagged);
      {
        var_value->Bind(LoadObjectField(object, field_offset));
        Goto(&done);
      }
      Bind(&if_double);
      {
        if (FLAG_unbox_double_fields) {
          var_double_value.Bind(
              LoadObjectField(object, field_offset, MachineType::Float64()));
        } else {
          Node* mutable_heap_number = LoadObjectField(object, field_offset);
          var_double_value.Bind(LoadHeapNumberValue(mutable_heap_number));
        }
        Goto(&rebox_double);
      }
    }
    Bind(&if_backing_store);
    {
      Comment("if_backing_store");
      Node* properties = LoadProperties(object);
      field_index = Int32Sub(field_index, inobject_properties);
      Node* value = LoadFixedArrayElement(properties, field_index);

      Label if_double(this), if_tagged(this);
      BranchIfWord32NotEqual(representation,
                             Int32Constant(Representation::kDouble), &if_tagged,
                             &if_double);
      Bind(&if_tagged);
      {
        var_value->Bind(value);
        Goto(&done);
      }
      Bind(&if_double);
      {
        var_double_value.Bind(LoadHeapNumberValue(value));
        Goto(&rebox_double);
      }
    }
    Bind(&rebox_double);
    {
      Comment("rebox_double");
      Node* heap_number = AllocateHeapNumber();
      StoreHeapNumberValue(heap_number, var_double_value.value());
      var_value->Bind(heap_number);
      Goto(&done);
    }
  }
  Bind(&if_in_descriptor);
  {
    Node* value =
        LoadFixedArrayElement(descriptors, name_index, name_to_value_offset);
    var_value->Bind(value);
    Goto(&done);
  }
  Bind(&done);

  Comment("] LoadPropertyFromFastObject");
}

void CodeStubAssembler::LoadPropertyFromNameDictionary(Node* dictionary,
                                                       Node* name_index,
                                                       Variable* var_details,
                                                       Variable* var_value) {
  Comment("LoadPropertyFromNameDictionary");

  const int name_to_details_offset =
      (NameDictionary::kEntryDetailsIndex - NameDictionary::kEntryKeyIndex) *
      kPointerSize;
  const int name_to_value_offset =
      (NameDictionary::kEntryValueIndex - NameDictionary::kEntryKeyIndex) *
      kPointerSize;

  Node* details = SmiToWord32(
      LoadFixedArrayElement(dictionary, name_index, name_to_details_offset));

  var_details->Bind(details);
  var_value->Bind(
      LoadFixedArrayElement(dictionary, name_index, name_to_value_offset));

  Comment("] LoadPropertyFromNameDictionary");
}

void CodeStubAssembler::LoadPropertyFromGlobalDictionary(Node* dictionary,
                                                         Node* name_index,
                                                         Variable* var_details,
                                                         Variable* var_value,
                                                         Label* if_deleted) {
  Comment("[ LoadPropertyFromGlobalDictionary");

  const int name_to_value_offset =
      (GlobalDictionary::kEntryValueIndex - GlobalDictionary::kEntryKeyIndex) *
      kPointerSize;

  Node* property_cell =
      LoadFixedArrayElement(dictionary, name_index, name_to_value_offset);

  Node* value = LoadObjectField(property_cell, PropertyCell::kValueOffset);
  GotoIf(WordEqual(value, TheHoleConstant()), if_deleted);

  var_value->Bind(value);

  Node* details =
      SmiToWord32(LoadObjectField(property_cell, PropertyCell::kDetailsOffset));
  var_details->Bind(details);

  Comment("] LoadPropertyFromGlobalDictionary");
}

void CodeStubAssembler::TryGetOwnProperty(
    Node* context, Node* receiver, Node* object, Node* map, Node* instance_type,
    Node* unique_name, Label* if_found_value, Variable* var_value,
    Label* if_not_found, Label* if_bailout) {
  DCHECK_EQ(MachineRepresentation::kTagged, var_value->rep());
  Comment("TryGetOwnProperty");

  Variable var_meta_storage(this, MachineRepresentation::kTagged);
  Variable var_entry(this, MachineRepresentation::kWord32);

  Label if_found_fast(this), if_found_dict(this), if_found_global(this);

  Variable var_details(this, MachineRepresentation::kWord32);
  Variable* vars[] = {var_value, &var_details};
  Label if_found(this, 2, vars);

  TryLookupProperty(object, map, instance_type, unique_name, &if_found_fast,
                    &if_found_dict, &if_found_global, &var_meta_storage,
                    &var_entry, if_not_found, if_bailout);
  Bind(&if_found_fast);
  {
    Node* descriptors = var_meta_storage.value();
    Node* name_index = var_entry.value();

    LoadPropertyFromFastObject(object, map, descriptors, name_index,
                               &var_details, var_value);
    Goto(&if_found);
  }
  Bind(&if_found_dict);
  {
    Node* dictionary = var_meta_storage.value();
    Node* entry = var_entry.value();
    LoadPropertyFromNameDictionary(dictionary, entry, &var_details, var_value);
    Goto(&if_found);
  }
  Bind(&if_found_global);
  {
    Node* dictionary = var_meta_storage.value();
    Node* entry = var_entry.value();

    LoadPropertyFromGlobalDictionary(dictionary, entry, &var_details, var_value,
                                     if_not_found);
    Goto(&if_found);
  }
  // Here we have details and value which could be an accessor.
  Bind(&if_found);
  {
    Node* details = var_details.value();
    Node* kind = BitFieldDecode<PropertyDetails::KindField>(details);

    Label if_accessor(this);
    Branch(Word32Equal(kind, Int32Constant(kData)), if_found_value,
           &if_accessor);
    Bind(&if_accessor);
    {
      Node* accessor_pair = var_value->value();
      GotoIf(Word32Equal(LoadInstanceType(accessor_pair),
                         Int32Constant(ACCESSOR_INFO_TYPE)),
             if_bailout);
      AssertInstanceType(accessor_pair, ACCESSOR_PAIR_TYPE);
      Node* getter =
          LoadObjectField(accessor_pair, AccessorPair::kGetterOffset);
      Node* getter_map = LoadMap(getter);
      Node* instance_type = LoadMapInstanceType(getter_map);
      // FunctionTemplateInfo getters are not supported yet.
      GotoIf(Word32Equal(instance_type,
                         Int32Constant(FUNCTION_TEMPLATE_INFO_TYPE)),
             if_bailout);

      // Return undefined if the {getter} is not callable.
      var_value->Bind(UndefinedConstant());
      GotoIf(Word32Equal(Word32And(LoadMapBitField(getter_map),
                                   Int32Constant(1 << Map::kIsCallable)),
                         Int32Constant(0)),
             if_found_value);

      // Call the accessor.
      Callable callable = CodeFactory::Call(isolate());
      Node* result = CallJS(callable, context, getter, receiver);
      var_value->Bind(result);
      Goto(if_found_value);
    }
  }
}

void CodeStubAssembler::TryLookupElement(Node* object, Node* map,
                                         Node* instance_type, Node* index,
                                         Label* if_found, Label* if_not_found,
                                         Label* if_bailout) {
  // Handle special objects in runtime.
  GotoIf(Int32LessThanOrEqual(instance_type,
                              Int32Constant(LAST_SPECIAL_RECEIVER_TYPE)),
         if_bailout);

  Node* bit_field2 = LoadMapBitField2(map);
  Node* elements_kind = BitFieldDecode<Map::ElementsKindBits>(bit_field2);

  // TODO(verwaest): Support other elements kinds as well.
  Label if_isobjectorsmi(this), if_isdouble(this), if_isdictionary(this),
      if_isfaststringwrapper(this), if_isslowstringwrapper(this);
  // clang-format off
  int32_t values[] = {
      // Handled by {if_isobjectorsmi}.
      FAST_SMI_ELEMENTS, FAST_HOLEY_SMI_ELEMENTS, FAST_ELEMENTS,
          FAST_HOLEY_ELEMENTS,
      // Handled by {if_isdouble}.
      FAST_DOUBLE_ELEMENTS, FAST_HOLEY_DOUBLE_ELEMENTS,
      // Handled by {if_isdictionary}.
      DICTIONARY_ELEMENTS,
      // Handled by {if_isfaststringwrapper}.
      FAST_STRING_WRAPPER_ELEMENTS,
      // Handled by {if_isslowstringwrapper}.
      SLOW_STRING_WRAPPER_ELEMENTS,
      // Handled by {if_not_found}.
      NO_ELEMENTS,
  };
  Label* labels[] = {
      &if_isobjectorsmi, &if_isobjectorsmi, &if_isobjectorsmi,
          &if_isobjectorsmi,
      &if_isdouble, &if_isdouble,
      &if_isdictionary,
      &if_isfaststringwrapper,
      &if_isslowstringwrapper,
      if_not_found,
  };
  // clang-format on
  STATIC_ASSERT(arraysize(values) == arraysize(labels));
  Switch(elements_kind, if_bailout, values, labels, arraysize(values));

  Bind(&if_isobjectorsmi);
  {
    Node* elements = LoadElements(object);
    Node* length = LoadFixedArrayBaseLength(elements);

    GotoIf(Int32GreaterThanOrEqual(index, SmiToWord32(length)), if_not_found);

    Node* element = LoadFixedArrayElement(elements, index);
    Node* the_hole = TheHoleConstant();
    Branch(WordEqual(element, the_hole), if_not_found, if_found);
  }
  Bind(&if_isdouble);
  {
    Node* elements = LoadElements(object);
    Node* length = LoadFixedArrayBaseLength(elements);

    GotoIf(Int32GreaterThanOrEqual(index, SmiToWord32(length)), if_not_found);

    if (kPointerSize == kDoubleSize) {
      Node* element =
          LoadFixedDoubleArrayElement(elements, index, MachineType::Uint64());
      Node* the_hole = Int64Constant(kHoleNanInt64);
      Branch(Word64Equal(element, the_hole), if_not_found, if_found);
    } else {
      Node* element_upper =
          LoadFixedDoubleArrayElement(elements, index, MachineType::Uint32(),
                                      kIeeeDoubleExponentWordOffset);
      Branch(Word32Equal(element_upper, Int32Constant(kHoleNanUpper32)),
             if_not_found, if_found);
    }
  }
  Bind(&if_isdictionary);
  {
    Variable var_entry(this, MachineRepresentation::kWord32);
    Node* elements = LoadElements(object);
    NumberDictionaryLookup<SeededNumberDictionary>(elements, index, if_found,
                                                   &var_entry, if_not_found);
  }
  Bind(&if_isfaststringwrapper);
  {
    AssertInstanceType(object, JS_VALUE_TYPE);
    Node* string = LoadJSValueValue(object);
    Assert(Int32LessThan(LoadInstanceType(string),
                         Int32Constant(FIRST_NONSTRING_TYPE)));
    Node* length = LoadStringLength(string);
    GotoIf(Int32LessThan(index, SmiToWord32(length)), if_found);
    Goto(&if_isobjectorsmi);
  }
  Bind(&if_isslowstringwrapper);
  {
    AssertInstanceType(object, JS_VALUE_TYPE);
    Node* string = LoadJSValueValue(object);
    Assert(Int32LessThan(LoadInstanceType(string),
                         Int32Constant(FIRST_NONSTRING_TYPE)));
    Node* length = LoadStringLength(string);
    GotoIf(Int32LessThan(index, SmiToWord32(length)), if_found);
    Goto(&if_isdictionary);
  }
}

// Instantiate template methods to workaround GCC compilation issue.
template void CodeStubAssembler::NumberDictionaryLookup<SeededNumberDictionary>(
    Node*, Node*, Label*, Variable*, Label*);
template void CodeStubAssembler::NumberDictionaryLookup<
    UnseededNumberDictionary>(Node*, Node*, Label*, Variable*, Label*);

Node* CodeStubAssembler::OrdinaryHasInstance(Node* context, Node* callable,
                                             Node* object) {
  Variable var_result(this, MachineRepresentation::kTagged);
  Label return_false(this), return_true(this),
      return_runtime(this, Label::kDeferred), return_result(this);

  // Goto runtime if {object} is a Smi.
  GotoIf(WordIsSmi(object), &return_runtime);

  // Load map of {object}.
  Node* object_map = LoadMap(object);

  // Lookup the {callable} and {object} map in the global instanceof cache.
  // Note: This is safe because we clear the global instanceof cache whenever
  // we change the prototype of any object.
  Node* instanceof_cache_function =
      LoadRoot(Heap::kInstanceofCacheFunctionRootIndex);
  Node* instanceof_cache_map = LoadRoot(Heap::kInstanceofCacheMapRootIndex);
  {
    Label instanceof_cache_miss(this);
    GotoUnless(WordEqual(instanceof_cache_function, callable),
               &instanceof_cache_miss);
    GotoUnless(WordEqual(instanceof_cache_map, object_map),
               &instanceof_cache_miss);
    var_result.Bind(LoadRoot(Heap::kInstanceofCacheAnswerRootIndex));
    Goto(&return_result);
    Bind(&instanceof_cache_miss);
  }

  // Goto runtime if {callable} is a Smi.
  GotoIf(WordIsSmi(callable), &return_runtime);

  // Load map of {callable}.
  Node* callable_map = LoadMap(callable);

  // Goto runtime if {callable} is not a JSFunction.
  Node* callable_instance_type = LoadMapInstanceType(callable_map);
  GotoUnless(
      Word32Equal(callable_instance_type, Int32Constant(JS_FUNCTION_TYPE)),
      &return_runtime);

  // Goto runtime if {callable} is not a constructor or has
  // a non-instance "prototype".
  Node* callable_bitfield = LoadMapBitField(callable_map);
  GotoUnless(
      Word32Equal(Word32And(callable_bitfield,
                            Int32Constant((1 << Map::kHasNonInstancePrototype) |
                                          (1 << Map::kIsConstructor))),
                  Int32Constant(1 << Map::kIsConstructor)),
      &return_runtime);

  // Get the "prototype" (or initial map) of the {callable}.
  Node* callable_prototype =
      LoadObjectField(callable, JSFunction::kPrototypeOrInitialMapOffset);
  {
    Variable var_callable_prototype(this, MachineRepresentation::kTagged);
    Label callable_prototype_valid(this);
    var_callable_prototype.Bind(callable_prototype);

    // Resolve the "prototype" if the {callable} has an initial map.  Afterwards
    // the {callable_prototype} will be either the JSReceiver prototype object
    // or the hole value, which means that no instances of the {callable} were
    // created so far and hence we should return false.
    Node* callable_prototype_instance_type =
        LoadInstanceType(callable_prototype);
    GotoUnless(
        Word32Equal(callable_prototype_instance_type, Int32Constant(MAP_TYPE)),
        &callable_prototype_valid);
    var_callable_prototype.Bind(
        LoadObjectField(callable_prototype, Map::kPrototypeOffset));
    Goto(&callable_prototype_valid);
    Bind(&callable_prototype_valid);
    callable_prototype = var_callable_prototype.value();
  }

  // Update the global instanceof cache with the current {object} map and
  // {callable}.  The cached answer will be set when it is known below.
  StoreRoot(Heap::kInstanceofCacheFunctionRootIndex, callable);
  StoreRoot(Heap::kInstanceofCacheMapRootIndex, object_map);

  // Loop through the prototype chain looking for the {callable} prototype.
  Variable var_object_map(this, MachineRepresentation::kTagged);
  var_object_map.Bind(object_map);
  Label loop(this, &var_object_map);
  Goto(&loop);
  Bind(&loop);
  {
    Node* object_map = var_object_map.value();

    // Check if the current {object} needs to be access checked.
    Node* object_bitfield = LoadMapBitField(object_map);
    GotoUnless(
        Word32Equal(Word32And(object_bitfield,
                              Int32Constant(1 << Map::kIsAccessCheckNeeded)),
                    Int32Constant(0)),
        &return_runtime);

    // Check if the current {object} is a proxy.
    Node* object_instance_type = LoadMapInstanceType(object_map);
    GotoIf(Word32Equal(object_instance_type, Int32Constant(JS_PROXY_TYPE)),
           &return_runtime);

    // Check the current {object} prototype.
    Node* object_prototype = LoadMapPrototype(object_map);
    GotoIf(WordEqual(object_prototype, NullConstant()), &return_false);
    GotoIf(WordEqual(object_prototype, callable_prototype), &return_true);

    // Continue with the prototype.
    var_object_map.Bind(LoadMap(object_prototype));
    Goto(&loop);
  }

  Bind(&return_true);
  StoreRoot(Heap::kInstanceofCacheAnswerRootIndex, BooleanConstant(true));
  var_result.Bind(BooleanConstant(true));
  Goto(&return_result);

  Bind(&return_false);
  StoreRoot(Heap::kInstanceofCacheAnswerRootIndex, BooleanConstant(false));
  var_result.Bind(BooleanConstant(false));
  Goto(&return_result);

  Bind(&return_runtime);
  {
    // Invalidate the global instanceof cache.
    StoreRoot(Heap::kInstanceofCacheFunctionRootIndex, SmiConstant(0));
    // Fallback to the runtime implementation.
    var_result.Bind(
        CallRuntime(Runtime::kOrdinaryHasInstance, context, callable, object));
  }
  Goto(&return_result);

  Bind(&return_result);
  return var_result.value();
}

compiler::Node* CodeStubAssembler::ElementOffsetFromIndex(Node* index_node,
                                                          ElementsKind kind,
                                                          ParameterMode mode,
                                                          int base_size) {
  bool is_double = IsFastDoubleElementsKind(kind);
  int element_size_shift = is_double ? kDoubleSizeLog2 : kPointerSizeLog2;
  int element_size = 1 << element_size_shift;
  int const kSmiShiftBits = kSmiShiftSize + kSmiTagSize;
  int32_t index = 0;
  bool constant_index = false;
  if (mode == SMI_PARAMETERS) {
    element_size_shift -= kSmiShiftBits;
    intptr_t temp = 0;
    constant_index = ToIntPtrConstant(index_node, temp);
    index = temp >> kSmiShiftBits;
  } else {
    constant_index = ToInt32Constant(index_node, index);
  }
  if (constant_index) {
    return IntPtrConstant(base_size + element_size * index);
  }
  if (Is64() && mode == INTEGER_PARAMETERS) {
    index_node = ChangeInt32ToInt64(index_node);
  }
  if (base_size == 0) {
    return (element_size_shift >= 0)
               ? WordShl(index_node, IntPtrConstant(element_size_shift))
               : WordShr(index_node, IntPtrConstant(-element_size_shift));
  }
  return IntPtrAdd(
      IntPtrConstant(base_size),
      (element_size_shift >= 0)
          ? WordShl(index_node, IntPtrConstant(element_size_shift))
          : WordShr(index_node, IntPtrConstant(-element_size_shift)));
}

compiler::Node* CodeStubAssembler::LoadTypeFeedbackVectorForStub() {
  Node* function =
      LoadFromParentFrame(JavaScriptFrameConstants::kFunctionOffset);
  Node* literals = LoadObjectField(function, JSFunction::kLiteralsOffset);
  return LoadObjectField(literals, LiteralsArray::kFeedbackVectorOffset);
}

compiler::Node* CodeStubAssembler::LoadReceiverMap(compiler::Node* receiver) {
  Variable var_receiver_map(this, MachineRepresentation::kTagged);
  // TODO(ishell): defer blocks when it works.
  Label load_smi_map(this /*, Label::kDeferred*/), load_receiver_map(this),
      if_result(this);

  Branch(WordIsSmi(receiver), &load_smi_map, &load_receiver_map);
  Bind(&load_smi_map);
  {
    var_receiver_map.Bind(LoadRoot(Heap::kHeapNumberMapRootIndex));
    Goto(&if_result);
  }
  Bind(&load_receiver_map);
  {
    var_receiver_map.Bind(LoadMap(receiver));
    Goto(&if_result);
  }
  Bind(&if_result);
  return var_receiver_map.value();
}

compiler::Node* CodeStubAssembler::TryMonomorphicCase(
    const LoadICParameters* p, compiler::Node* receiver_map, Label* if_handler,
    Variable* var_handler, Label* if_miss) {
  DCHECK_EQ(MachineRepresentation::kTagged, var_handler->rep());

  // TODO(ishell): add helper class that hides offset computations for a series
  // of loads.
  int32_t header_size = FixedArray::kHeaderSize - kHeapObjectTag;
  Node* offset = ElementOffsetFromIndex(p->slot, FAST_HOLEY_ELEMENTS,
                                        SMI_PARAMETERS, header_size);
  Node* feedback = Load(MachineType::AnyTagged(), p->vector, offset);

  // Try to quickly handle the monomorphic case without knowing for sure
  // if we have a weak cell in feedback. We do know it's safe to look
  // at WeakCell::kValueOffset.
  GotoUnless(WordEqual(receiver_map, LoadWeakCellValue(feedback)), if_miss);

  Node* handler = Load(MachineType::AnyTagged(), p->vector,
                       IntPtrAdd(offset, IntPtrConstant(kPointerSize)));

  var_handler->Bind(handler);
  Goto(if_handler);
  return feedback;
}

void CodeStubAssembler::HandlePolymorphicCase(
    const LoadICParameters* p, compiler::Node* receiver_map,
    compiler::Node* feedback, Label* if_handler, Variable* var_handler,
    Label* if_miss, int unroll_count) {
  DCHECK_EQ(MachineRepresentation::kTagged, var_handler->rep());

  // Iterate {feedback} array.
  const int kEntrySize = 2;

  for (int i = 0; i < unroll_count; i++) {
    Label next_entry(this);
    Node* cached_map = LoadWeakCellValue(
        LoadFixedArrayElement(feedback, Int32Constant(i * kEntrySize)));
    GotoIf(WordNotEqual(receiver_map, cached_map), &next_entry);

    // Found, now call handler.
    Node* handler =
        LoadFixedArrayElement(feedback, Int32Constant(i * kEntrySize + 1));
    var_handler->Bind(handler);
    Goto(if_handler);

    Bind(&next_entry);
  }
  Node* length = SmiToWord32(LoadFixedArrayBaseLength(feedback));

  // Loop from {unroll_count}*kEntrySize to {length}.
  Variable var_index(this, MachineRepresentation::kWord32);
  Label loop(this, &var_index);
  var_index.Bind(Int32Constant(unroll_count * kEntrySize));
  Goto(&loop);
  Bind(&loop);
  {
    Node* index = var_index.value();
    GotoIf(Int32GreaterThanOrEqual(index, length), if_miss);

    Node* cached_map =
        LoadWeakCellValue(LoadFixedArrayElement(feedback, index));

    Label next_entry(this);
    GotoIf(WordNotEqual(receiver_map, cached_map), &next_entry);

    // Found, now call handler.
    Node* handler = LoadFixedArrayElement(feedback, index, kPointerSize);
    var_handler->Bind(handler);
    Goto(if_handler);

    Bind(&next_entry);
    var_index.Bind(Int32Add(index, Int32Constant(kEntrySize)));
    Goto(&loop);
  }
}

compiler::Node* CodeStubAssembler::StubCachePrimaryOffset(compiler::Node* name,
                                                          Code::Flags flags,
                                                          compiler::Node* map) {
  // See v8::internal::StubCache::PrimaryOffset().
  STATIC_ASSERT(StubCache::kCacheIndexShift == Name::kHashShift);
  // Compute the hash of the name (use entire hash field).
  Node* hash_field = LoadNameHashField(name);
  Assert(WordEqual(
      Word32And(hash_field, Int32Constant(Name::kHashNotComputedMask)),
      Int32Constant(0)));

  // Using only the low bits in 64-bit mode is unlikely to increase the
  // risk of collision even if the heap is spread over an area larger than
  // 4Gb (and not at all if it isn't).
  Node* hash = Int32Add(hash_field, map);
  // We always set the in_loop bit to zero when generating the lookup code
  // so do it here too so the hash codes match.
  uint32_t iflags =
      (static_cast<uint32_t>(flags) & ~Code::kFlagsNotUsedInLookup);
  // Base the offset on a simple combination of name, flags, and map.
  hash = Word32Xor(hash, Int32Constant(iflags));
  uint32_t mask = (StubCache::kPrimaryTableSize - 1)
                  << StubCache::kCacheIndexShift;
  return Word32And(hash, Int32Constant(mask));
}

compiler::Node* CodeStubAssembler::StubCacheSecondaryOffset(
    compiler::Node* name, Code::Flags flags, compiler::Node* seed) {
  // See v8::internal::StubCache::SecondaryOffset().

  // Use the seed from the primary cache in the secondary cache.
  Node* hash = Int32Sub(seed, name);
  // We always set the in_loop bit to zero when generating the lookup code
  // so do it here too so the hash codes match.
  uint32_t iflags =
      (static_cast<uint32_t>(flags) & ~Code::kFlagsNotUsedInLookup);
  hash = Int32Add(hash, Int32Constant(iflags));
  int32_t mask = (StubCache::kSecondaryTableSize - 1)
                 << StubCache::kCacheIndexShift;
  return Word32And(hash, Int32Constant(mask));
}

enum CodeStubAssembler::StubCacheTable : int {
  kPrimary = static_cast<int>(StubCache::kPrimary),
  kSecondary = static_cast<int>(StubCache::kSecondary)
};

void CodeStubAssembler::TryProbeStubCacheTable(
    StubCache* stub_cache, StubCacheTable table_id,
    compiler::Node* entry_offset, compiler::Node* name, Code::Flags flags,
    compiler::Node* map, Label* if_handler, Variable* var_handler,
    Label* if_miss) {
  StubCache::Table table = static_cast<StubCache::Table>(table_id);
#ifdef DEBUG
  if (FLAG_test_secondary_stub_cache && table == StubCache::kPrimary) {
    Goto(if_miss);
    return;
  } else if (FLAG_test_primary_stub_cache && table == StubCache::kSecondary) {
    Goto(if_miss);
    return;
  }
#endif
  // The {table_offset} holds the entry offset times four (due to masking
  // and shifting optimizations).
  const int kMultiplier = sizeof(StubCache::Entry) >> Name::kHashShift;
  entry_offset = Int32Mul(entry_offset, Int32Constant(kMultiplier));

  // Check that the key in the entry matches the name.
  Node* key_base =
      ExternalConstant(ExternalReference(stub_cache->key_reference(table)));
  Node* entry_key = Load(MachineType::Pointer(), key_base, entry_offset);
  GotoIf(WordNotEqual(name, entry_key), if_miss);

  // Get the map entry from the cache.
  DCHECK_EQ(kPointerSize * 2, stub_cache->map_reference(table).address() -
                                  stub_cache->key_reference(table).address());
  Node* entry_map =
      Load(MachineType::Pointer(), key_base,
           Int32Add(entry_offset, Int32Constant(kPointerSize * 2)));
  GotoIf(WordNotEqual(map, entry_map), if_miss);

  // Check that the flags match what we're looking for.
  DCHECK_EQ(kPointerSize, stub_cache->value_reference(table).address() -
                              stub_cache->key_reference(table).address());
  Node* code = Load(MachineType::Pointer(), key_base,
                    Int32Add(entry_offset, Int32Constant(kPointerSize)));

  Node* code_flags =
      LoadObjectField(code, Code::kFlagsOffset, MachineType::Uint32());
  GotoIf(Word32NotEqual(Int32Constant(flags),
                        Word32And(code_flags,
                                  Int32Constant(~Code::kFlagsNotUsedInLookup))),
         if_miss);

  // We found the handler.
  var_handler->Bind(code);
  Goto(if_handler);
}

void CodeStubAssembler::TryProbeStubCache(
    StubCache* stub_cache, Code::Flags flags, compiler::Node* receiver,
    compiler::Node* name, Label* if_handler, Variable* var_handler,
    Label* if_miss) {
  Label try_secondary(this), miss(this);

  Counters* counters = isolate()->counters();
  IncrementCounter(counters->megamorphic_stub_cache_probes(), 1);

  // Check that the {receiver} isn't a smi.
  GotoIf(WordIsSmi(receiver), &miss);

  Node* receiver_map = LoadMap(receiver);

  // Probe the primary table.
  Node* primary_offset = StubCachePrimaryOffset(name, flags, receiver_map);
  TryProbeStubCacheTable(stub_cache, kPrimary, primary_offset, name, flags,
                         receiver_map, if_handler, var_handler, &try_secondary);

  Bind(&try_secondary);
  {
    // Probe the secondary table.
    Node* secondary_offset =
        StubCacheSecondaryOffset(name, flags, primary_offset);
    TryProbeStubCacheTable(stub_cache, kSecondary, secondary_offset, name,
                           flags, receiver_map, if_handler, var_handler, &miss);
  }

  Bind(&miss);
  {
    IncrementCounter(counters->megamorphic_stub_cache_misses(), 1);
    Goto(if_miss);
  }
}

void CodeStubAssembler::LoadIC(const LoadICParameters* p) {
  Variable var_handler(this, MachineRepresentation::kTagged);
  // TODO(ishell): defer blocks when it works.
  Label if_handler(this, &var_handler), try_polymorphic(this),
      try_megamorphic(this /*, Label::kDeferred*/),
      miss(this /*, Label::kDeferred*/);

  Node* receiver_map = LoadReceiverMap(p->receiver);

  // Check monomorphic case.
  Node* feedback = TryMonomorphicCase(p, receiver_map, &if_handler,
                                      &var_handler, &try_polymorphic);
  Bind(&if_handler);
  {
    LoadWithVectorDescriptor descriptor(isolate());
    TailCallStub(descriptor, var_handler.value(), p->context, p->receiver,
                 p->name, p->slot, p->vector);
  }

  Bind(&try_polymorphic);
  {
    // Check polymorphic case.
    GotoUnless(
        WordEqual(LoadMap(feedback), LoadRoot(Heap::kFixedArrayMapRootIndex)),
        &try_megamorphic);
    HandlePolymorphicCase(p, receiver_map, feedback, &if_handler, &var_handler,
                          &miss, 2);
  }

  Bind(&try_megamorphic);
  {
    // Check megamorphic case.
    GotoUnless(
        WordEqual(feedback, LoadRoot(Heap::kmegamorphic_symbolRootIndex)),
        &miss);

    Code::Flags code_flags =
        Code::RemoveHolderFromFlags(Code::ComputeHandlerFlags(Code::LOAD_IC));

    TryProbeStubCache(isolate()->stub_cache(), code_flags, p->receiver, p->name,
                      &if_handler, &var_handler, &miss);
  }
  Bind(&miss);
  {
    TailCallRuntime(Runtime::kLoadIC_Miss, p->context, p->receiver, p->name,
                    p->slot, p->vector);
  }
}

void CodeStubAssembler::LoadGlobalIC(const LoadICParameters* p) {
  Label try_handler(this), miss(this);
  Node* weak_cell =
      LoadFixedArrayElement(p->vector, p->slot, 0, SMI_PARAMETERS);
  AssertInstanceType(weak_cell, WEAK_CELL_TYPE);

  // Load value or try handler case if the {weak_cell} is cleared.
  Node* property_cell = LoadWeakCellValue(weak_cell, &try_handler);
  AssertInstanceType(property_cell, PROPERTY_CELL_TYPE);

  Node* value = LoadObjectField(property_cell, PropertyCell::kValueOffset);
  GotoIf(WordEqual(value, TheHoleConstant()), &miss);
  Return(value);

  Bind(&try_handler);
  {
    Node* handler =
        LoadFixedArrayElement(p->vector, p->slot, kPointerSize, SMI_PARAMETERS);
    GotoIf(WordEqual(handler, LoadRoot(Heap::kuninitialized_symbolRootIndex)),
           &miss);

    // In this case {handler} must be a Code object.
    AssertInstanceType(handler, CODE_TYPE);
    LoadWithVectorDescriptor descriptor(isolate());
    Node* native_context = LoadNativeContext(p->context);
    Node* receiver = LoadFixedArrayElement(
        native_context, Int32Constant(Context::EXTENSION_INDEX));
    Node* fake_name = IntPtrConstant(0);
    TailCallStub(descriptor, handler, p->context, receiver, fake_name, p->slot,
                 p->vector);
  }
  Bind(&miss);
  {
    TailCallRuntime(Runtime::kLoadGlobalIC_Miss, p->context, p->slot,
                    p->vector);
  }
}

}  // namespace internal
}  // namespace v8
