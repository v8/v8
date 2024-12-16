// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-json.h"
#include "src/utils/ostreams.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module-builder.h"
#include "test/unittests/fuzztest.h"
#include "test/unittests/test-utils.h"

namespace v8::internal::wasm {

// Introduce a separate representation for recursion groups to be used by this
// fuzz test.
namespace test {

// Provide operator<<(std::ostream&, T> for types with T.Print(std::ostream*).
template <typename T>
  requires requires(const T& t, std::ostream& os) { t.Print(os); }
std::ostream& operator<<(std::ostream& os, const T& t) {
  t.Print(os);
  return os;
}

struct FieldType {
  ValueType value_type;
  bool mutability;

  void Print(std::ostream& os) const {
    os << (mutability ? "mut " : "") << value_type;
  }
};

struct StructType {
  std::vector<FieldType> field_types;

  void BuildType(Zone* zone, WasmModuleBuilder* builder) const {
    // TODO(381687256): Populate final and supertype.
    constexpr bool kNotFinal = false;
    constexpr ModuleTypeIndex kNoSupertype = ModuleTypeIndex::Invalid();
    DCHECK_GE(kMaxUInt32, field_types.size());
    uint32_t field_count = static_cast<uint32_t>(field_types.size());
    // Offsets are not used and never accessed, hence we can pass nullptr.
    constexpr uint32_t* kNoOffsets = nullptr;
    ValueType* reps = zone->AllocateArray<ValueType>(field_count);
    bool* mutabilities = zone->AllocateArray<bool>(field_count);
    for (uint32_t i = 0; i < field_count; ++i) {
      reps[i] = field_types[i].value_type;
      mutabilities[i] = field_types[i].mutability;
    }
    builder->AddStructType(zone->New<wasm::StructType>(field_count, kNoOffsets,
                                                       reps, mutabilities),
                           kNotFinal, kNoSupertype);
  }

  void Print(std::ostream& os) const {
    os << "struct(" << PrintCollection(field_types).WithoutBrackets() << ")";
  }
};

struct ArrayType {
  FieldType field_type;

  void BuildType(Zone* zone, WasmModuleBuilder* builder) const {
    // TODO(381687256): Populate final and supertype.
    constexpr bool kNotFinal = false;
    constexpr ModuleTypeIndex kNoSupertype = ModuleTypeIndex::Invalid();
    builder->AddArrayType(zone->New<wasm::ArrayType>(field_type.value_type,
                                                     field_type.mutability),
                          kNotFinal, kNoSupertype);
  }

  void Print(std::ostream& os) const { os << "array(" << field_type << ")"; }
};

struct FunctionType {
  std::vector<ValueType> params;
  std::vector<ValueType> returns;

  void BuildType(Zone* zone, WasmModuleBuilder* builder) const {
    // TODO(381687256): Populate final and supertype.
    constexpr bool kNotFinal = false;
    constexpr ModuleTypeIndex kNoSupertype = ModuleTypeIndex::Invalid();
    FunctionSig::Builder sig_builder(zone, returns.size(), params.size());
    for (ValueType param : params) sig_builder.AddParam(param);
    for (ValueType ret : returns) sig_builder.AddReturn(ret);
    FunctionSig* sig = sig_builder.Get();
    builder->ForceAddSignature(sig, kNotFinal, kNoSupertype);
  }

  void Print(std::ostream& os) const {
    os << "func params (" << PrintCollection(params).WithoutBrackets()
       << ") returns (" << PrintCollection(returns).WithoutBrackets() << ")";
  }
};

using Type = std::variant<StructType, ArrayType, FunctionType>;

std::ostream& operator<<(std::ostream& os, const Type& type) {
  // Call operator<< on the contained type.
  std::visit([&os](auto& t) { os << t; }, type);
  return os;
}

// A module with a number of types.
struct Module {
  // TODO(381687256): Add recursion groups.
  std::vector<Type> types;

  void BuildTypes(Zone* zone, WasmModuleBuilder* builder) const {
    for (const Type& type : types) {
      std::visit([zone, builder](const auto& t) { t.BuildType(zone, builder); },
                 type);
    }
  }
};

}  // namespace test

class TypeCanonicalizerTest
    : public fuzztest::PerFuzzTestFixtureAdapter<TestWithPlatform> {
 public:
  TypeCanonicalizerTest() : zone_(&allocator_, "TypeCanonicalizerTest") {}
  ~TypeCanonicalizerTest() override = default;

  void TestCanonicalization(const std::vector<test::Module>&);

 private:
  void Reset() {
    wasm::GetTypeCanonicalizer()->EmptyStorageForTesting();
    zone_.Reset();
  }

  v8::internal::AccountingAllocator allocator_;
  Zone zone_;
  const WasmEnabledFeatures enabled_features_ =
      WasmEnabledFeatures::FromFlags();
};

// FuzzTest domain construction.

static fuzztest::Domain<test::Module> ArbitraryModule() {
  auto storage_type_domain = fuzztest::ElementOf(
      {kWasmI8, kWasmI16, kWasmI32, kWasmI64, kWasmF32, kWasmF64
       /* TODO(381687256: Add kS128 on SIMD-enabled hosts */});
  auto value_type_domain = fuzztest::ElementOf(
      {kWasmI32, kWasmI64, kWasmF32, kWasmF64
       /* TODO(381687256: Add kS128 on SIMD-enabled hosts */});

  auto field_type_domain = fuzztest::StructOf<test::FieldType>(
      storage_type_domain, fuzztest::Arbitrary<bool>());
  auto struct_type_domain = fuzztest::StructOf<test::StructType>(
      fuzztest::VectorOf(field_type_domain));

  auto array_type_domain =
      fuzztest::StructOf<test::ArrayType>(field_type_domain);

  auto function_type_domain = fuzztest::StructOf<test::FunctionType>(
      fuzztest::VectorOf(value_type_domain),
      fuzztest::VectorOf(value_type_domain));

  auto type_domain = fuzztest::VariantOf<test::Type>(
      struct_type_domain, array_type_domain, function_type_domain);

  auto module_domain =
      fuzztest::StructOf<test::Module>(fuzztest::VectorOf(type_domain));
  return module_domain;
}

// Fuzz tests.

void TypeCanonicalizerTest::TestCanonicalization(
    const std::vector<test::Module>& test_modules) {
  // For each test, reset the type canonicalizer such that individual inputs are
  // independent of each other.
  Reset();

  // Keep a map of all types in all modules to check that canonicalization works
  // as expected. The key is a text representation of the respective type; we
  // expect same text to mean identical type.
  std::map<std::string, CanonicalTypeIndex> canonical_types;

  for (const test::Module& test_module : test_modules) {
    WasmModuleBuilder builder(&zone_);
    test_module.BuildTypes(&zone_, &builder);
    ZoneBuffer buffer{&zone_};
    builder.WriteTo(&buffer);

    WasmDetectedFeatures detected_features;
    bool kValidateModule = true;
    ModuleResult result =
        DecodeWasmModule(enabled_features_, base::VectorOf(buffer),
                         kValidateModule, kWasmOrigin, &detected_features);

    ASSERT_TRUE(result.ok());
    std::shared_ptr<WasmModule> module = std::move(result).value();
    ASSERT_EQ(module->types.size(), test_module.types.size());

    for (size_t type_id = 0; type_id < test_module.types.size(); ++type_id) {
      const test::Type& type = test_module.types[type_id];
      CanonicalTypeIndex canonical_id = module->canonical_type_id(
          ModuleTypeIndex{static_cast<uint32_t>(type_id)});
      std::string type_str = (std::ostringstream{} << type).str();
      auto [it, added] =
          canonical_types.insert(std::make_pair(type_str, canonical_id));
      // Check that the entry holds canonical_id; either it was added here, or
      // it existed and we check against the existing entry.
      ASSERT_EQ(it->second, canonical_id) << "New type:\n"
                                          << type_str << "\nOld type:\n"
                                          << it->first;
    }
  }
}

V8_FUZZ_TEST_F(TypeCanonicalizerTest, TestCanonicalization)
    .WithDomains(fuzztest::VectorOf(ArbitraryModule()));

}  // namespace v8::internal::wasm
