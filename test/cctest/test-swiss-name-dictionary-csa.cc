// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/cctest/test-swiss-name-dictionary-infra.h"

namespace v8 {
namespace internal {
namespace test_swiss_hash_table {

// Executes tests by executing CSA/Torque versions of dictionary operations.
// See RuntimeTestRunner for description of public functions.
class CSATestRunner {
 public:
  CSATestRunner(Isolate* isolate, int initial_capacity, KeyCache& keys);

  void Add(Handle<Name> key, Handle<Object> value, PropertyDetails details);
  InternalIndex FindEntry(Handle<Name> key);
  void Put(InternalIndex entry, Handle<Object> new_value,
           PropertyDetails new_details);
  void Delete(InternalIndex entry);
  void RehashInplace();
  void Shrink();

  Handle<FixedArray> GetData(InternalIndex entry);
  void CheckCounts(base::Optional<int> capacity, base::Optional<int> elements,
                   base::Optional<int> deleted);
  void CheckEnumerationOrder(const std::vector<std::string>& expected_keys);
  void CheckCopy();
  void VerifyHeap();

  void PrintTable();

  Handle<SwissNameDictionary> table;

 private:
  void CheckAgainstReference();

  void Allocate(Handle<Smi> capacity);

  Isolate* isolate_;

  // Used to mirror all operations using C++ versions of all operations,
  // yielding a reference to compare against.
  Handle<SwissNameDictionary> reference_;

  // CSA functions execute the corresponding dictionary operation.
  compiler::FunctionTester find_entry_ft_;
  compiler::FunctionTester get_data_ft_;
  compiler::FunctionTester put_ft_;
  compiler::FunctionTester delete_ft_;
  compiler::FunctionTester add_ft_;
  compiler::FunctionTester allocate_ft_;

  // Used to create the FunctionTesters above.
  static Handle<Code> create_get_data(Isolate* isolate);
  static Handle<Code> create_find_entry(Isolate* isolate);
  static Handle<Code> create_put(Isolate* isolate);
  static Handle<Code> create_delete(Isolate* isolate);
  static Handle<Code> create_add(Isolate* isolate);
  static Handle<Code> create_allocate(Isolate* isolate);

  // Number of parameters of each of the tester functions above.
  static constexpr int kFindEntryParams = 2;  // (table, key)
  static constexpr int kGetDataParams = 2;    // (table, entry)
  static constexpr int kPutParams = 4;        // (table, entry, value,  details)
  static constexpr int kDeleteParams = 2;     // (table, entry)
  static constexpr int kAddParams = 4;        // (table, key, value, details)
  static constexpr int kAllocateParams = 1;   // (capacity)
};

// TODO(v8:11330): Currently, the CSATestRunner isn't doing much, except
// generating runtime calls. That will change once we have the CSA
// implementations ready.
CSATestRunner::CSATestRunner(Isolate* isolate, int initial_capacity,
                             KeyCache& keys)
    : isolate_{isolate},
      reference_{isolate_->factory()->NewSwissNameDictionaryWithCapacity(
          initial_capacity, AllocationType::kYoung)},
      find_entry_ft_(create_find_entry(isolate), kFindEntryParams),
      get_data_ft_(create_get_data(isolate), kGetDataParams),
      put_ft_{create_put(isolate), kPutParams},
      delete_ft_{create_delete(isolate), kDeleteParams},
      add_ft_{create_add(isolate), kAddParams},
      allocate_ft_{create_allocate(isolate), kAllocateParams} {
  int at_least_space_for =
      SwissNameDictionary::MaxUsableCapacity(initial_capacity);
  Allocate(handle(Smi::FromInt(at_least_space_for), isolate));
}

void CSATestRunner::Add(Handle<Name> key, Handle<Object> value,
                        PropertyDetails details) {
  reference_ =
      SwissNameDictionary::Add(isolate_, reference_, key, value, details);

  Handle<Smi> details_smi = handle(details.AsSmi(), isolate_);
  table =
      add_ft_.CallChecked<SwissNameDictionary>(table, key, value, details_smi);

  CheckAgainstReference();
}

void CSATestRunner::Allocate(Handle<Smi> capacity) {
  table = allocate_ft_.CallChecked<SwissNameDictionary>(capacity);

  CheckAgainstReference();
}

InternalIndex CSATestRunner::FindEntry(Handle<Name> key) {
  Handle<Smi> index = find_entry_ft_.CallChecked<Smi>(table, key);
  if (index->value() == SwissNameDictionary::kNotFoundSentinel) {
    return InternalIndex::NotFound();
  } else {
    return InternalIndex(index->value());
  }
}

Handle<FixedArray> CSATestRunner::GetData(InternalIndex entry) {
  DCHECK(entry.is_found());

  return get_data_ft_.CallChecked<FixedArray>(
      table, handle(Smi::FromInt(entry.as_int()), isolate_));
}

void CSATestRunner::CheckCounts(base::Optional<int> capacity,
                                base::Optional<int> elements,
                                base::Optional<int> deleted) {
  // TODO(v8:11330) Do actual check here once CSA/Torque version exists.
  CheckAgainstReference();
}

void CSATestRunner::CheckEnumerationOrder(
    const std::vector<std::string>& expected_keys) {
  // TODO(v8:11330) Do actual check here once CSA/Torque version exists.
  CheckAgainstReference();
}

void CSATestRunner::Put(InternalIndex entry, Handle<Object> new_value,
                        PropertyDetails new_details) {
  DCHECK(entry.is_found());
  reference_->ValueAtPut(entry, *new_value);
  reference_->DetailsAtPut(entry, new_details);

  Handle<Smi> entry_smi = handle(Smi::FromInt(entry.as_int()), isolate_);
  Handle<Smi> details_smi = handle(new_details.AsSmi(), isolate_);

  put_ft_.Call(table, entry_smi, new_value, details_smi);

  CheckAgainstReference();
}

void CSATestRunner::Delete(InternalIndex entry) {
  DCHECK(entry.is_found());
  reference_ = SwissNameDictionary::DeleteEntry(isolate_, reference_, entry);

  Handle<Smi> entry_smi = handle(Smi::FromInt(entry.as_int()), isolate_);
  table = delete_ft_.CallChecked<SwissNameDictionary>(table, entry_smi);
  CheckAgainstReference();
}

void CSATestRunner::RehashInplace() {
  // There's no CSA version of this. Use IsRuntimeTest to ensure that we only
  // run a test using this if it's a runtime test.
  UNREACHABLE();
}

void CSATestRunner::Shrink() {
  // There's no CSA version of this. Use IsRuntimeTest to ensure that we only
  // run a test using this if it's a runtime test.
  UNREACHABLE();
}

void CSATestRunner::CheckCopy() {
  // TODO(v8:11330) Do actual check here once CSA/Torque version exists.
}

void CSATestRunner::VerifyHeap() {
#if VERIFY_HEAP
  table->SwissNameDictionaryVerify(isolate_, true);
#endif
}

void CSATestRunner::PrintTable() {
#ifdef OBJECT_PRINT
  table->SwissNameDictionaryPrint(std::cout);
#endif
}

Handle<Code> CSATestRunner::create_find_entry(Isolate* isolate) {
  STATIC_ASSERT(kFindEntryParams == 2);  // (table, key)
  compiler::CodeAssemblerTester asm_tester(isolate, kFindEntryParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Name> key = m.Parameter<Name>(2);

    TNode<Smi> index = m.CallRuntime<Smi>(Runtime::kSwissTableFindEntry,
                                          m.NoContextConstant(), table, key);

    m.Return(index);
  }

  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_get_data(Isolate* isolate) {
  STATIC_ASSERT(kGetDataParams == 2);  // (table, entry)
  compiler::CodeAssemblerTester asm_tester(isolate, kGetDataParams + 1);
  CodeStubAssembler m(asm_tester.state());
  using Label = compiler::CodeAssemblerLabel;
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Smi> index = m.Parameter<Smi>(2);

    Label not_found(&m);

    m.GotoIf(m.SmiEqual(index,
                        m.SmiConstant(SwissNameDictionary::kNotFoundSentinel)),
             &not_found);

    TNode<FixedArray> data = m.AllocateZeroedFixedArray(m.IntPtrConstant(3));

    TNode<Object> key = m.CallRuntime(Runtime::kSwissTableKeyAt,
                                      m.NoContextConstant(), table, index);
    TNode<Object> value = m.CallRuntime(Runtime::kSwissTableValueAt,
                                        m.NoContextConstant(), table, index);
    TNode<Smi> details = m.UncheckedCast<Smi>(m.CallRuntime(
        Runtime::kSwissTableDetailsAt, m.NoContextConstant(), table, index));

    m.StoreFixedArrayElement(data, 0, key);
    m.StoreFixedArrayElement(data, 1, value);
    m.StoreFixedArrayElement(data, 2, details);

    m.Return(data);

    m.Bind(&not_found);

    m.Return(m.EmptyFixedArrayConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_put(Isolate* isolate) {
  STATIC_ASSERT(kPutParams == 4);  // (table, entry, value, details)
  compiler::CodeAssemblerTester asm_tester(isolate, kPutParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Smi> entry = m.Parameter<Smi>(2);
    TNode<Object> value = m.Parameter<Object>(3);
    TNode<Smi> details = m.Parameter<Smi>(4);

    m.CallRuntime(Runtime::kSwissTableUpdate, m.NoContextConstant(), table,
                  entry, value, details);
    m.Return(m.UndefinedConstant());
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_delete(Isolate* isolate) {
  STATIC_ASSERT(kDeleteParams == 2);  // (table, entry)
  compiler::CodeAssemblerTester asm_tester(isolate, kDeleteParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Smi> entry = m.Parameter<Smi>(2);

    TNode<SwissNameDictionary> new_table = m.CallRuntime<SwissNameDictionary>(
        Runtime::kSwissTableDelete, m.NoContextConstant(), table, entry);

    m.Return(new_table);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_add(Isolate* isolate) {
  STATIC_ASSERT(kAddParams == 4);  // (table, key, value, details)
  compiler::CodeAssemblerTester asm_tester(isolate, kAddParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<SwissNameDictionary> table = m.Parameter<SwissNameDictionary>(1);
    TNode<Name> key = m.Parameter<Name>(2);
    TNode<Object> value = m.Parameter<Object>(3);
    TNode<Smi> details = m.Parameter<Smi>(4);

    TNode<SwissNameDictionary> new_table = m.CallRuntime<SwissNameDictionary>(
        Runtime::kSwissTableAdd, m.NoContextConstant(), table, key, value,
        details);

    m.Return(new_table);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

Handle<Code> CSATestRunner::create_allocate(Isolate* isolate) {
  STATIC_ASSERT(kAllocateParams == 1);  // (capacity)
  compiler::CodeAssemblerTester asm_tester(isolate, kAllocateParams + 1);
  CodeStubAssembler m(asm_tester.state());
  {
    TNode<Smi> at_least_space_for = m.Parameter<Smi>(1);

    TNode<SwissNameDictionary> table = m.CallRuntime<SwissNameDictionary>(
        Runtime::kSwissTableAllocate, m.NoContextConstant(),
        at_least_space_for);

    m.Return(table);
  }
  return asm_tester.GenerateCodeCloseAndEscape();
}

void CSATestRunner::CheckAgainstReference() {
  CHECK(table->EqualsForTesting(*reference_));
}

}  // namespace test_swiss_hash_table
}  // namespace internal
}  // namespace v8
