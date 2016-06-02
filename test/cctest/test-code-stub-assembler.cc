// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/utils/random-number-generator.h"
#include "src/interface-descriptors.h"
#include "src/isolate.h"
#include "test/cctest/compiler/function-tester.h"

namespace v8 {
namespace internal {

using compiler::FunctionTester;
using compiler::Node;

class ZoneHolder {
 public:
  explicit ZoneHolder(Isolate* isolate) : zone_(isolate->allocator()) {}
  Zone* zone() { return &zone_; }

 private:
  Zone zone_;
};

// Inherit from ZoneHolder in order to create a zone that can be passed to
// CodeStubAssembler base class constructor.
class CodeStubAssemblerTester : private ZoneHolder, public CodeStubAssembler {
 public:
  // Test generating code for a stub.
  CodeStubAssemblerTester(Isolate* isolate,
                          const CallInterfaceDescriptor& descriptor)
      : ZoneHolder(isolate),
        CodeStubAssembler(isolate, ZoneHolder::zone(), descriptor,
                          Code::ComputeFlags(Code::STUB), "test"),
        scope_(isolate) {}

  // Test generating code for a JS function (e.g. builtins).
  CodeStubAssemblerTester(Isolate* isolate, int parameter_count)
      : ZoneHolder(isolate),
        CodeStubAssembler(isolate, ZoneHolder::zone(), parameter_count,
                          Code::ComputeFlags(Code::FUNCTION), "test"),
        scope_(isolate) {}

 private:
  HandleScope scope_;
  LocalContext context_;
};

TEST(SimpleSmiReturn) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  m.Return(m.SmiTag(m.Int32Constant(37)));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(37, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(SimpleIntPtrReturn) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  int test;
  m.Return(m.IntPtrConstant(reinterpret_cast<intptr_t>(&test)));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(reinterpret_cast<intptr_t>(&test),
           reinterpret_cast<intptr_t>(*result.ToHandleChecked()));
}

TEST(SimpleDoubleReturn) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  m.Return(m.NumberConstant(0.5));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(0.5, Handle<HeapNumber>::cast(result.ToHandleChecked())->value());
}

TEST(SimpleCallRuntime1Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Node* context = m.HeapConstant(Handle<Context>(isolate->native_context()));
  Node* b = m.SmiTag(m.Int32Constant(0));
  m.Return(m.CallRuntime(Runtime::kNumberToSmi, context, b));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(0, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(SimpleTailCallRuntime1Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Node* context = m.HeapConstant(Handle<Context>(isolate->native_context()));
  Node* b = m.SmiTag(m.Int32Constant(0));
  m.TailCallRuntime(Runtime::kNumberToSmi, context, b);
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(0, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(SimpleCallRuntime2Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Node* context = m.HeapConstant(Handle<Context>(isolate->native_context()));
  Node* a = m.SmiTag(m.Int32Constant(2));
  Node* b = m.SmiTag(m.Int32Constant(4));
  m.Return(m.CallRuntime(Runtime::kMathPow, context, a, b));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(16, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(SimpleTailCallRuntime2Arg) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Node* context = m.HeapConstant(Handle<Context>(isolate->native_context()));
  Node* a = m.SmiTag(m.Int32Constant(2));
  Node* b = m.SmiTag(m.Int32Constant(4));
  m.TailCallRuntime(Runtime::kMathPow, context, a, b);
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(16, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(VariableMerge1) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Variable var1(&m, MachineRepresentation::kTagged);
  CodeStubAssembler::Label l1(&m), l2(&m), merge(&m);
  Node* temp = m.Int32Constant(0);
  var1.Bind(temp);
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&l2);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK_EQ(var1.value(), temp);
}

TEST(VariableMerge2) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Variable var1(&m, MachineRepresentation::kTagged);
  CodeStubAssembler::Label l1(&m), l2(&m), merge(&m);
  Node* temp = m.Int32Constant(0);
  var1.Bind(temp);
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&l2);
  Node* temp2 = m.Int32Constant(2);
  var1.Bind(temp2);
  CHECK_EQ(var1.value(), temp2);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK_NE(var1.value(), temp);
}

TEST(VariableMerge3) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Variable var1(&m, MachineRepresentation::kTagged);
  CodeStubAssembler::Variable var2(&m, MachineRepresentation::kTagged);
  CodeStubAssembler::Label l1(&m), l2(&m), merge(&m);
  Node* temp = m.Int32Constant(0);
  var1.Bind(temp);
  var2.Bind(temp);
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&l2);
  Node* temp2 = m.Int32Constant(2);
  var1.Bind(temp2);
  CHECK_EQ(var1.value(), temp2);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK_NE(var1.value(), temp);
  CHECK_NE(var1.value(), temp2);
  CHECK_EQ(var2.value(), temp);
}

TEST(VariableMergeBindFirst) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Variable var1(&m, MachineRepresentation::kTagged);
  CodeStubAssembler::Label l1(&m), l2(&m), merge(&m, &var1), end(&m);
  Node* temp = m.Int32Constant(0);
  var1.Bind(temp);
  m.Branch(m.Int32Constant(1), &l1, &l2);
  m.Bind(&l1);
  CHECK_EQ(var1.value(), temp);
  m.Goto(&merge);
  m.Bind(&merge);
  CHECK(var1.value() != temp);
  CHECK(var1.value() != nullptr);
  m.Goto(&end);
  m.Bind(&l2);
  Node* temp2 = m.Int32Constant(2);
  var1.Bind(temp2);
  CHECK_EQ(var1.value(), temp2);
  m.Goto(&merge);
  m.Bind(&end);
  CHECK(var1.value() != temp);
  CHECK(var1.value() != nullptr);
}

TEST(VariableMergeSwitch) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Variable var1(&m, MachineRepresentation::kTagged);
  CodeStubAssembler::Label l1(&m), l2(&m), default_label(&m);
  CodeStubAssembler::Label* labels[] = {&l1, &l2};
  int32_t values[] = {1, 2};
  Node* temp = m.Int32Constant(0);
  var1.Bind(temp);
  m.Switch(m.Int32Constant(2), &default_label, values, labels, 2);
  m.Bind(&l1);
  DCHECK_EQ(temp, var1.value());
  m.Return(temp);
  m.Bind(&l2);
  DCHECK_EQ(temp, var1.value());
  m.Return(temp);
  m.Bind(&default_label);
  DCHECK_EQ(temp, var1.value());
  m.Return(temp);
}

TEST(FixedArrayAccessSmiIndex) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Handle<FixedArray> array = isolate->factory()->NewFixedArray(5);
  array->set(4, Smi::FromInt(733));
  m.Return(m.LoadFixedArrayElement(m.HeapConstant(array),
                                   m.SmiTag(m.Int32Constant(4)), 0,
                                   CodeStubAssembler::SMI_PARAMETERS));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(733, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(LoadHeapNumberValue) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Handle<HeapNumber> number = isolate->factory()->NewHeapNumber(1234);
  m.Return(m.SmiTag(
      m.ChangeFloat64ToUint32(m.LoadHeapNumberValue(m.HeapConstant(number)))));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(1234, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(LoadInstanceType) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Handle<HeapObject> undefined = isolate->factory()->undefined_value();
  m.Return(m.SmiTag(m.LoadInstanceType(m.HeapConstant(undefined))));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  CHECK_EQ(InstanceType::ODDBALL_TYPE,
           Handle<Smi>::cast(result.ToHandleChecked())->value());
}

namespace {

class TestBitField : public BitField<unsigned, 3, 3> {};

}  // namespace

TEST(BitFieldDecode) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  m.Return(m.SmiTag(m.BitFieldDecode<TestBitField>(m.Int32Constant(0x2f))));
  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(descriptor, code);
  MaybeHandle<Object> result = ft.Call();
  // value  = 00101111
  // mask   = 00111000
  // result = 101
  CHECK_EQ(5, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

namespace {

Handle<JSFunction> CreateFunctionFromCode(int parameter_count_with_receiver,
                                          Handle<Code> code) {
  Isolate* isolate = code->GetIsolate();
  Handle<String> name = isolate->factory()->InternalizeUtf8String("test");
  Handle<JSFunction> function =
      isolate->factory()->NewFunctionWithoutPrototype(name, code);
  function->shared()->set_internal_formal_parameter_count(
      parameter_count_with_receiver - 1);  // Implicit undefined receiver.
  return function;
}

}  // namespace

TEST(JSFunction) {
  const int kNumParams = 3;  // Receiver, left, right.
  Isolate* isolate(CcTest::InitIsolateOnce());
  CodeStubAssemblerTester m(isolate, kNumParams);
  m.Return(m.SmiTag(m.Int32Add(m.SmiToWord32(m.Parameter(1)),
                               m.SmiToWord32(m.Parameter(2)))));
  Handle<Code> code = m.GenerateCode();
  Handle<JSFunction> function = CreateFunctionFromCode(kNumParams, code);
  Handle<Object> args[] = {Handle<Smi>(Smi::FromInt(23), isolate),
                           Handle<Smi>(Smi::FromInt(34), isolate)};
  MaybeHandle<Object> result =
      Execution::Call(isolate, function, isolate->factory()->undefined_value(),
                      arraysize(args), args);
  CHECK_EQ(57, Handle<Smi>::cast(result.ToHandleChecked())->value());
}

TEST(SplitEdgeBranchMerge) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Label l1(&m), merge(&m);
  m.Branch(m.Int32Constant(1), &l1, &merge);
  m.Bind(&l1);
  m.Goto(&merge);
  m.Bind(&merge);
  USE(m.GenerateCode());
}

TEST(SplitEdgeSwitchMerge) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  CodeStubAssembler::Label l1(&m), l2(&m), l3(&m), default_label(&m);
  CodeStubAssembler::Label* labels[] = {&l1, &l2};
  int32_t values[] = {1, 2};
  m.Branch(m.Int32Constant(1), &l3, &l1);
  m.Bind(&l3);
  m.Switch(m.Int32Constant(2), &default_label, values, labels, 2);
  m.Bind(&l1);
  m.Goto(&l2);
  m.Bind(&l2);
  m.Goto(&default_label);
  m.Bind(&default_label);
  USE(m.GenerateCode());
}

TEST(TestToConstant) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  int32_t value32;
  int64_t value64;
  Node* a = m.Int32Constant(5);
  CHECK(m.ToInt32Constant(a, value32));
  CHECK(m.ToInt64Constant(a, value64));

  a = m.Int64Constant(static_cast<int64_t>(1) << 32);
  CHECK(!m.ToInt32Constant(a, value32));
  CHECK(m.ToInt64Constant(a, value64));

  a = m.Int64Constant(13);
  CHECK(m.ToInt32Constant(a, value32));
  CHECK(m.ToInt64Constant(a, value64));

  a = m.UndefinedConstant();
  CHECK(!m.ToInt32Constant(a, value32));
  CHECK(!m.ToInt64Constant(a, value64));

  a = m.UndefinedConstant();
  CHECK(!m.ToInt32Constant(a, value32));
  CHECK(!m.ToInt64Constant(a, value64));
}

TEST(ComputeIntegerHash) {
  Isolate* isolate(CcTest::InitIsolateOnce());
  const int param_count = 2;
  CodeStubAssemblerTester m(isolate, param_count);
  m.Return(m.SmiFromWord32(m.ComputeIntegerHash(
      m.SmiToWord32(m.Parameter(0)), m.SmiToWord32(m.Parameter(1)))));

  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(code, param_count);

  Handle<Smi> hash_seed = isolate->factory()->hash_seed();

  base::RandomNumberGenerator rand_gen(FLAG_random_seed);

  for (int i = 0; i < 1024; i++) {
    int k = rand_gen.NextInt(Smi::kMaxValue);

    Handle<Smi> key(Smi::FromInt(k), isolate);
    Handle<Object> result = ft.Call(key, hash_seed).ToHandleChecked();

    uint32_t hash = ComputeIntegerHash(k, hash_seed->value());
    Smi* expected = Smi::FromInt(hash & Smi::kMaxValue);
    CHECK_EQ(expected, Smi::cast(*result));
  }
}

TEST(TryToName) {
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;
  Isolate* isolate(CcTest::InitIsolateOnce());

  const int param_count = 3;
  CodeStubAssemblerTester m(isolate, param_count);

  enum Result { kKeyIsIndex, kKeyIsUnique, kBailout };
  {
    Node* key = m.Parameter(0);
    Node* expected_result = m.Parameter(1);
    Node* expected_arg = m.Parameter(2);

    Label passed(&m), failed(&m);
    Label if_keyisindex(&m), if_keyisunique(&m), if_bailout(&m);
    Variable var_index(&m, MachineRepresentation::kWord32);

    m.TryToName(key, &if_keyisindex, &var_index, &if_keyisunique, &if_bailout);

    m.Bind(&if_keyisindex);
    m.GotoUnless(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kKeyIsIndex))),
        &failed);
    m.Branch(m.Word32Equal(m.SmiToWord32(expected_arg), var_index.value()),
             &passed, &failed);

    m.Bind(&if_keyisunique);
    m.GotoUnless(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kKeyIsUnique))),
        &failed);
    m.Branch(m.WordEqual(expected_arg, key), &passed, &failed);

    m.Bind(&if_bailout);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kBailout))),
        &passed, &failed);

    m.Bind(&passed);
    m.Return(m.BooleanConstant(true));

    m.Bind(&failed);
    m.Return(m.BooleanConstant(false));
  }

  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(code, param_count);

  Handle<Object> expect_index(Smi::FromInt(kKeyIsIndex), isolate);
  Handle<Object> expect_unique(Smi::FromInt(kKeyIsUnique), isolate);
  Handle<Object> expect_bailout(Smi::FromInt(kBailout), isolate);

  {
    // TryToName(<zero smi>) => if_keyisindex: smi value.
    Handle<Object> key(Smi::FromInt(0), isolate);
    ft.CheckTrue(key, expect_index, key);
  }

  {
    // TryToName(<positive smi>) => if_keyisindex: smi value.
    Handle<Object> key(Smi::FromInt(153), isolate);
    ft.CheckTrue(key, expect_index, key);
  }

  {
    // TryToName(<negative smi>) => bailout.
    Handle<Object> key(Smi::FromInt(-1), isolate);
    ft.CheckTrue(key, expect_bailout);
  }

  {
    // TryToName(<symbol>) => if_keyisunique: <symbol>.
    Handle<Object> key = isolate->factory()->NewSymbol();
    ft.CheckTrue(key, expect_unique, key);
  }

  {
    // TryToName(<internalized string>) => if_keyisunique: <internalized string>
    Handle<Object> key = isolate->factory()->InternalizeUtf8String("test");
    ft.CheckTrue(key, expect_unique, key);
  }

  {
    // TryToName(<internalized number string>) => if_keyisindex: number.
    Handle<Object> key = isolate->factory()->InternalizeUtf8String("153");
    Handle<Object> index(Smi::FromInt(153), isolate);
    ft.CheckTrue(key, expect_index, index);
  }

  {
    // TryToName(<non-internalized string>) => bailout.
    Handle<Object> key = isolate->factory()->NewStringFromAsciiChecked("test");
    ft.CheckTrue(key, expect_bailout);
  }
}

namespace {

template <typename Dictionary>
void TestNameDictionaryLookup() {
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;
  Isolate* isolate(CcTest::InitIsolateOnce());

  const int param_count = 4;
  CodeStubAssemblerTester m(isolate, param_count);

  enum Result { kFound, kNotFound };
  {
    Node* dictionary = m.Parameter(0);
    Node* unique_name = m.Parameter(1);
    Node* expected_result = m.Parameter(2);
    Node* expected_arg = m.Parameter(3);

    Label passed(&m), failed(&m);
    Label if_found(&m), if_not_found(&m);
    Variable var_entry(&m, MachineRepresentation::kWord32);

    m.NameDictionaryLookup<Dictionary>(dictionary, unique_name, &if_found,
                                       &var_entry, &if_not_found);
    m.Bind(&if_found);
    m.GotoUnless(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kFound))),
        &failed);
    m.Branch(m.Word32Equal(m.SmiToWord32(expected_arg), var_entry.value()),
             &passed, &failed);

    m.Bind(&if_not_found);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kNotFound))),
        &passed, &failed);

    m.Bind(&passed);
    m.Return(m.BooleanConstant(true));

    m.Bind(&failed);
    m.Return(m.BooleanConstant(false));
  }

  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(code, param_count);

  Handle<Object> expect_found(Smi::FromInt(kFound), isolate);
  Handle<Object> expect_not_found(Smi::FromInt(kNotFound), isolate);

  Handle<Dictionary> dictionary = Dictionary::New(isolate, 40);
  PropertyDetails fake_details = PropertyDetails::Empty();

  Factory* factory = isolate->factory();
  Handle<Name> keys[] = {
      factory->InternalizeUtf8String("0"),
      factory->InternalizeUtf8String("42"),
      factory->InternalizeUtf8String("-153"),
      factory->InternalizeUtf8String("0.0"),
      factory->InternalizeUtf8String("4.2"),
      factory->InternalizeUtf8String(""),
      factory->InternalizeUtf8String("name"),
      factory->NewSymbol(),
      factory->NewPrivateSymbol(),
  };

  for (size_t i = 0; i < arraysize(keys); i++) {
    Handle<Object> value = factory->NewPropertyCell();
    dictionary = Dictionary::Add(dictionary, keys[i], value, fake_details);
  }

  for (size_t i = 0; i < arraysize(keys); i++) {
    int entry = dictionary->FindEntry(keys[i]);
    CHECK_NE(Dictionary::kNotFound, entry);

    Handle<Object> expected_entry(Smi::FromInt(entry), isolate);
    ft.CheckTrue(dictionary, keys[i], expect_found, expected_entry);
  }

  Handle<Name> non_existing_keys[] = {
      factory->InternalizeUtf8String("1"),
      factory->InternalizeUtf8String("-42"),
      factory->InternalizeUtf8String("153"),
      factory->InternalizeUtf8String("-1.0"),
      factory->InternalizeUtf8String("1.3"),
      factory->InternalizeUtf8String("a"),
      factory->InternalizeUtf8String("boom"),
      factory->NewSymbol(),
      factory->NewPrivateSymbol(),
  };

  for (size_t i = 0; i < arraysize(non_existing_keys); i++) {
    int entry = dictionary->FindEntry(non_existing_keys[i]);
    CHECK_EQ(Dictionary::kNotFound, entry);

    ft.CheckTrue(dictionary, non_existing_keys[i], expect_not_found);
  }
}

}  // namespace

TEST(NameDictionaryLookup) { TestNameDictionaryLookup<NameDictionary>(); }

TEST(GlobalDictionaryLookup) { TestNameDictionaryLookup<GlobalDictionary>(); }

namespace {

template <typename Dictionary>
void TestNumberDictionaryLookup() {
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;
  Isolate* isolate(CcTest::InitIsolateOnce());

  const int param_count = 4;
  CodeStubAssemblerTester m(isolate, param_count);

  enum Result { kFound, kNotFound };
  {
    Node* dictionary = m.Parameter(0);
    Node* key = m.SmiToWord32(m.Parameter(1));
    Node* expected_result = m.Parameter(2);
    Node* expected_arg = m.Parameter(3);

    Label passed(&m), failed(&m);
    Label if_found(&m), if_not_found(&m);
    Variable var_entry(&m, MachineRepresentation::kWord32);

    m.NumberDictionaryLookup<Dictionary>(dictionary, key, &if_found, &var_entry,
                                         &if_not_found);
    m.Bind(&if_found);
    m.GotoUnless(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kFound))),
        &failed);
    m.Branch(m.Word32Equal(m.SmiToWord32(expected_arg), var_entry.value()),
             &passed, &failed);

    m.Bind(&if_not_found);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kNotFound))),
        &passed, &failed);

    m.Bind(&passed);
    m.Return(m.BooleanConstant(true));

    m.Bind(&failed);
    m.Return(m.BooleanConstant(false));
  }

  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(code, param_count);

  Handle<Object> expect_found(Smi::FromInt(kFound), isolate);
  Handle<Object> expect_not_found(Smi::FromInt(kNotFound), isolate);

  const int kKeysCount = 1000;
  Handle<Dictionary> dictionary = Dictionary::New(isolate, kKeysCount);
  uint32_t keys[kKeysCount];

  Handle<Object> fake_value(Smi::FromInt(42), isolate);
  PropertyDetails fake_details = PropertyDetails::Empty();

  base::RandomNumberGenerator rand_gen(FLAG_random_seed);

  for (int i = 0; i < kKeysCount; i++) {
    int random_key = rand_gen.NextInt(Smi::kMaxValue);
    keys[i] = static_cast<uint32_t>(random_key);

    dictionary = Dictionary::Add(dictionary, keys[i], fake_value, fake_details);
  }

  // Now try querying existing keys.
  for (int i = 0; i < kKeysCount; i++) {
    int entry = dictionary->FindEntry(keys[i]);
    CHECK_NE(Dictionary::kNotFound, entry);

    Handle<Object> key(Smi::FromInt(keys[i]), isolate);
    Handle<Object> expected_entry(Smi::FromInt(entry), isolate);
    ft.CheckTrue(dictionary, key, expect_found, expected_entry);
  }

  // Now try querying random keys which do not exist in the dictionary.
  for (int i = 0; i < kKeysCount;) {
    int random_key = rand_gen.NextInt(Smi::kMaxValue);
    int entry = dictionary->FindEntry(random_key);
    if (entry != Dictionary::kNotFound) continue;
    i++;

    Handle<Object> key(Smi::FromInt(random_key), isolate);
    ft.CheckTrue(dictionary, key, expect_not_found);
  }
}

}  // namespace

TEST(SeededNumberDictionaryLookup) {
  TestNumberDictionaryLookup<SeededNumberDictionary>();
}

TEST(UnseededNumberDictionaryLookup) {
  TestNumberDictionaryLookup<UnseededNumberDictionary>();
}

namespace {

void AddProperties(Handle<JSObject> object, Handle<Name> names[],
                   size_t count) {
  Handle<Object> value(Smi::FromInt(42), object->GetIsolate());
  for (size_t i = 0; i < count; i++) {
    JSObject::AddProperty(object, names[i], value, NONE);
  }
}

}  // namespace

TEST(TryLookupProperty) {
  typedef CodeStubAssembler::Label Label;
  Isolate* isolate(CcTest::InitIsolateOnce());

  const int param_count = 4;
  CodeStubAssemblerTester m(isolate, param_count);

  enum Result { kFound, kNotFound, kBailout };
  {
    Node* object = m.Parameter(0);
    Node* unique_name = m.Parameter(1);
    Node* expected_result = m.Parameter(2);

    Label passed(&m), failed(&m);
    Label if_found(&m), if_not_found(&m), if_bailout(&m);

    Node* map = m.LoadMap(object);
    Node* instance_type = m.LoadMapInstanceType(map);

    m.TryLookupProperty(object, map, instance_type, unique_name, &if_found,
                        &if_not_found, &if_bailout);

    m.Bind(&if_found);
    m.Branch(m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kFound))),
             &passed, &failed);

    m.Bind(&if_not_found);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kNotFound))),
        &passed, &failed);

    m.Bind(&if_bailout);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kBailout))),
        &passed, &failed);

    m.Bind(&passed);
    m.Return(m.BooleanConstant(true));

    m.Bind(&failed);
    m.Return(m.BooleanConstant(false));
  }

  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(code, param_count);

  Handle<Object> expect_found(Smi::FromInt(kFound), isolate);
  Handle<Object> expect_not_found(Smi::FromInt(kNotFound), isolate);
  Handle<Object> expect_bailout(Smi::FromInt(kBailout), isolate);

  Factory* factory = isolate->factory();
  Handle<Name> names[] = {
      factory->InternalizeUtf8String("a"),
      factory->InternalizeUtf8String("bb"),
      factory->InternalizeUtf8String("ccc"),
      factory->InternalizeUtf8String("dddd"),
      factory->InternalizeUtf8String("eeeee"),
      factory->InternalizeUtf8String(""),
      factory->InternalizeUtf8String("name"),
      factory->NewSymbol(),
      factory->NewPrivateSymbol(),
  };

  std::vector<Handle<JSObject>> objects;

  {
    Handle<JSFunction> function = factory->NewFunction(factory->empty_string());
    Handle<JSObject> object = factory->NewJSObject(function);
    AddProperties(object, names, arraysize(names));
    CHECK_EQ(JS_OBJECT_TYPE, object->map()->instance_type());
    CHECK(!object->map()->is_dictionary_map());
    objects.push_back(object);
  }

  {
    Handle<JSFunction> function = factory->NewFunction(factory->empty_string());
    Handle<JSObject> object = factory->NewJSObject(function);
    AddProperties(object, names, arraysize(names));
    JSObject::NormalizeProperties(object, CLEAR_INOBJECT_PROPERTIES, 0, "test");
    CHECK_EQ(JS_OBJECT_TYPE, object->map()->instance_type());
    CHECK(object->map()->is_dictionary_map());
    objects.push_back(object);
  }

  {
    Handle<JSFunction> function = factory->NewFunction(factory->empty_string());
    JSFunction::EnsureHasInitialMap(function);
    function->initial_map()->set_instance_type(JS_GLOBAL_OBJECT_TYPE);
    function->initial_map()->set_is_prototype_map(true);
    function->initial_map()->set_dictionary_map(true);
    Handle<JSObject> object = factory->NewJSGlobalObject(function);
    AddProperties(object, names, arraysize(names));
    CHECK_EQ(JS_GLOBAL_OBJECT_TYPE, object->map()->instance_type());
    CHECK(object->map()->is_dictionary_map());
    objects.push_back(object);
  }

  {
    for (Handle<JSObject> object : objects) {
      for (size_t name_index = 0; name_index < arraysize(names); name_index++) {
        Handle<Name> name = names[name_index];
        CHECK(JSReceiver::HasProperty(object, name).FromJust());
        ft.CheckTrue(object, name, expect_found);
      }
    }
  }

  {
    Handle<Name> non_existing_names[] = {
        factory->InternalizeUtf8String("ne_a"),
        factory->InternalizeUtf8String("ne_bb"),
        factory->InternalizeUtf8String("ne_ccc"),
        factory->InternalizeUtf8String("ne_dddd"),
    };
    for (Handle<JSObject> object : objects) {
      for (size_t key_index = 0; key_index < arraysize(non_existing_names);
           key_index++) {
        Handle<Name> key = non_existing_names[key_index];
        CHECK(!JSReceiver::HasProperty(object, key).FromJust());
        ft.CheckTrue(object, key, expect_not_found);
      }
    }
  }

  {
    Handle<JSFunction> function = factory->NewFunction(factory->empty_string());
    Handle<JSProxy> object = factory->NewJSProxy(function, objects[0]);
    CHECK_EQ(JS_PROXY_TYPE, object->map()->instance_type());
    ft.CheckTrue(object, names[0], expect_bailout);
  }

  {
    Handle<JSObject> object = isolate->global_proxy();
    CHECK_EQ(JS_GLOBAL_PROXY_TYPE, object->map()->instance_type());
    ft.CheckTrue(object, names[0], expect_bailout);
  }
}

namespace {

void AddElement(Handle<JSObject> object, uint32_t index, Handle<Object> value,
                PropertyAttributes attributes = NONE) {
  JSObject::AddDataElement(object, index, value, attributes).ToHandleChecked();
}

}  // namespace

TEST(TryLookupElement) {
  typedef CodeStubAssembler::Label Label;
  Isolate* isolate(CcTest::InitIsolateOnce());

  const int param_count = 4;
  CodeStubAssemblerTester m(isolate, param_count);

  enum Result { kFound, kNotFound, kBailout };
  {
    Node* object = m.Parameter(0);
    Node* index = m.SmiToWord32(m.Parameter(1));
    Node* expected_result = m.Parameter(2);

    Label passed(&m), failed(&m);
    Label if_found(&m), if_not_found(&m), if_bailout(&m);

    Node* map = m.LoadMap(object);
    Node* instance_type = m.LoadMapInstanceType(map);

    m.TryLookupElement(object, map, instance_type, index, &if_found,
                       &if_not_found, &if_bailout);

    m.Bind(&if_found);
    m.Branch(m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kFound))),
             &passed, &failed);

    m.Bind(&if_not_found);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kNotFound))),
        &passed, &failed);

    m.Bind(&if_bailout);
    m.Branch(
        m.WordEqual(expected_result, m.SmiConstant(Smi::FromInt(kBailout))),
        &passed, &failed);

    m.Bind(&passed);
    m.Return(m.BooleanConstant(true));

    m.Bind(&failed);
    m.Return(m.BooleanConstant(false));
  }

  Handle<Code> code = m.GenerateCode();
  FunctionTester ft(code, param_count);

  Factory* factory = isolate->factory();
  Handle<Object> smi0(Smi::FromInt(0), isolate);
  Handle<Object> smi1(Smi::FromInt(1), isolate);
  Handle<Object> smi7(Smi::FromInt(7), isolate);
  Handle<Object> smi13(Smi::FromInt(13), isolate);
  Handle<Object> smi42(Smi::FromInt(42), isolate);

  Handle<Object> expect_found(Smi::FromInt(kFound), isolate);
  Handle<Object> expect_not_found(Smi::FromInt(kNotFound), isolate);
  Handle<Object> expect_bailout(Smi::FromInt(kBailout), isolate);

#define CHECK_FOUND(object, index)                         \
  CHECK(JSReceiver::HasElement(object, index).FromJust()); \
  ft.CheckTrue(object, smi##index, expect_found);

#define CHECK_NOT_FOUND(object, index)                      \
  CHECK(!JSReceiver::HasElement(object, index).FromJust()); \
  ft.CheckTrue(object, smi##index, expect_not_found);

  {
    Handle<JSArray> object = factory->NewJSArray(0, FAST_SMI_ELEMENTS);
    AddElement(object, 0, smi0);
    AddElement(object, 1, smi0);
    CHECK_EQ(FAST_SMI_ELEMENTS, object->map()->elements_kind());

    CHECK_FOUND(object, 0);
    CHECK_FOUND(object, 1);
    CHECK_NOT_FOUND(object, 7);
    CHECK_NOT_FOUND(object, 13);
    CHECK_NOT_FOUND(object, 42);
  }

  {
    Handle<JSArray> object = factory->NewJSArray(0, FAST_HOLEY_SMI_ELEMENTS);
    AddElement(object, 0, smi0);
    AddElement(object, 13, smi0);
    CHECK_EQ(FAST_HOLEY_SMI_ELEMENTS, object->map()->elements_kind());

    CHECK_FOUND(object, 0);
    CHECK_NOT_FOUND(object, 1);
    CHECK_NOT_FOUND(object, 7);
    CHECK_FOUND(object, 13);
    CHECK_NOT_FOUND(object, 42);
  }

  {
    Handle<JSArray> object = factory->NewJSArray(0, FAST_ELEMENTS);
    AddElement(object, 0, smi0);
    AddElement(object, 1, smi0);
    CHECK_EQ(FAST_ELEMENTS, object->map()->elements_kind());

    CHECK_FOUND(object, 0);
    CHECK_FOUND(object, 1);
    CHECK_NOT_FOUND(object, 7);
    CHECK_NOT_FOUND(object, 13);
    CHECK_NOT_FOUND(object, 42);
  }

  {
    Handle<JSArray> object = factory->NewJSArray(0, FAST_HOLEY_ELEMENTS);
    AddElement(object, 0, smi0);
    AddElement(object, 13, smi0);
    CHECK_EQ(FAST_HOLEY_ELEMENTS, object->map()->elements_kind());

    CHECK_FOUND(object, 0);
    CHECK_NOT_FOUND(object, 1);
    CHECK_NOT_FOUND(object, 7);
    CHECK_FOUND(object, 13);
    CHECK_NOT_FOUND(object, 42);
  }

  {
    Handle<JSFunction> constructor = isolate->string_function();
    Handle<JSObject> object = factory->NewJSObject(constructor);
    Handle<String> str = factory->InternalizeUtf8String("ab");
    Handle<JSValue>::cast(object)->set_value(*str);
    AddElement(object, 13, smi0);
    CHECK_EQ(FAST_STRING_WRAPPER_ELEMENTS, object->map()->elements_kind());

    CHECK_FOUND(object, 0);
    CHECK_FOUND(object, 1);
    CHECK_NOT_FOUND(object, 7);
    CHECK_FOUND(object, 13);
    CHECK_NOT_FOUND(object, 42);
  }

  {
    Handle<JSFunction> constructor = isolate->string_function();
    Handle<JSObject> object = factory->NewJSObject(constructor);
    Handle<String> str = factory->InternalizeUtf8String("ab");
    Handle<JSValue>::cast(object)->set_value(*str);
    AddElement(object, 13, smi0);
    JSObject::NormalizeElements(object);
    CHECK_EQ(SLOW_STRING_WRAPPER_ELEMENTS, object->map()->elements_kind());

    CHECK_FOUND(object, 0);
    CHECK_FOUND(object, 1);
    CHECK_NOT_FOUND(object, 7);
    CHECK_FOUND(object, 13);
    CHECK_NOT_FOUND(object, 42);
  }

// TODO(ishell): uncomment once NO_ELEMENTS kind is supported.
//  {
//    Handle<Map> map = Map::Create(isolate, 0);
//    map->set_elements_kind(NO_ELEMENTS);
//    Handle<JSObject> object = factory->NewJSObjectFromMap(map);
//    CHECK_EQ(NO_ELEMENTS, object->map()->elements_kind());
//
//    CHECK_NOT_FOUND(object, 0);
//    CHECK_NOT_FOUND(object, 1);
//    CHECK_NOT_FOUND(object, 7);
//    CHECK_NOT_FOUND(object, 13);
//    CHECK_NOT_FOUND(object, 42);
//  }

#undef CHECK_FOUND
#undef CHECK_NOT_FOUND

  {
    Handle<JSArray> handler = factory->NewJSArray(0);
    Handle<JSFunction> function = factory->NewFunction(factory->empty_string());
    Handle<JSProxy> object = factory->NewJSProxy(function, handler);
    CHECK_EQ(JS_PROXY_TYPE, object->map()->instance_type());
    ft.CheckTrue(object, smi0, expect_bailout);
  }

  {
    Handle<JSObject> object = isolate->global_object();
    CHECK_EQ(JS_GLOBAL_OBJECT_TYPE, object->map()->instance_type());
    ft.CheckTrue(object, smi0, expect_bailout);
  }

  {
    Handle<JSObject> object = isolate->global_proxy();
    CHECK_EQ(JS_GLOBAL_PROXY_TYPE, object->map()->instance_type());
    ft.CheckTrue(object, smi0, expect_bailout);
  }
}

TEST(DeferredCodePhiHints) {
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;
  Isolate* isolate(CcTest::InitIsolateOnce());
  VoidDescriptor descriptor(isolate);
  CodeStubAssemblerTester m(isolate, descriptor);
  Label block1(&m, Label::kDeferred);
  m.Goto(&block1);
  m.Bind(&block1);
  {
    Variable var_object(&m, MachineRepresentation::kTagged);
    Label loop(&m, &var_object);
    var_object.Bind(m.IntPtrConstant(0));
    m.Goto(&loop);
    m.Bind(&loop);
    {
      Node* map = m.LoadMap(var_object.value());
      var_object.Bind(map);
      m.Goto(&loop);
    }
  }
  CHECK(!m.GenerateCode().is_null());
}

}  // namespace internal
}  // namespace v8
