// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/asm-types.h"

#include <unordered_map>
#include <unordered_set>

#include "src/base/macros.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace {

using ::testing::StrEq;

class AsmTypeTest : public TestWithZone {
 public:
  using Type = AsmType;

  AsmTypeTest()
      : parents_({
            {Type::Uint8Array(), {Type::Heap()}},
            {Type::Int8Array(), {Type::Heap()}},
            {Type::Uint16Array(), {Type::Heap()}},
            {Type::Int16Array(), {Type::Heap()}},
            {Type::Uint32Array(), {Type::Heap()}},
            {Type::Int32Array(), {Type::Heap()}},
            {Type::Float32Array(), {Type::Heap()}},
            {Type::Float64Array(), {Type::Heap()}},
            {Type::FloatishDoubleQ(), {Type::Floatish(), Type::DoubleQ()}},
            {Type::FloatQDoubleQ(),
             {Type::FloatQ(), Type::Floatish(), Type::DoubleQ()}},
            {Type::Float(), {Type::FloatQ(), Type::Floatish()}},
            {Type::FloatQ(), {Type::Floatish()}},
            {Type::FixNum(),
             {Type::Signed(), Type::Extern(), Type::Unsigned(), Type::Int(),
              Type::Intish()}},
            {Type::Unsigned(), {Type::Int(), Type::Intish()}},
            {Type::Signed(), {Type::Extern(), Type::Int(), Type::Intish()}},
            {Type::Int(), {Type::Intish()}},
            {Type::Double(), {Type::DoubleQ(), Type::Extern()}},
        }) {}

 protected:
  std::unordered_set<Type*> ParentsOf(Type* derived) const {
    const auto parents_iter = parents_.find(derived);
    if (parents_iter == parents_.end()) {
      return std::unordered_set<Type*>();
    }
    return parents_iter->second;
  }

  class FunctionTypeBuilder {
   public:
    FunctionTypeBuilder(FunctionTypeBuilder&& b)
        : function_type_(b.function_type_) {
      b.function_type_ = nullptr;
    }

    FunctionTypeBuilder& operator=(FunctionTypeBuilder&& b) {
      if (this != &b) {
        function_type_ = b.function_type_;
        b.function_type_ = nullptr;
      }
      return *this;
    }

    FunctionTypeBuilder(Zone* zone, Type* return_type)
        : function_type_(Type::Function(zone, return_type)) {}

   private:
    static void AddAllArguments(AsmFunctionType*) {}

    template <typename Arg, typename... Others>
    static void AddAllArguments(AsmFunctionType* function_type, Arg* arg,
                                Others... others) {
      CHECK(function_type != nullptr);
      function_type->AddArgument((*arg)());
      AddAllArguments(function_type, others...);
    }

   public:
    template <typename... Args>
    Type* operator()(Args... args) {
      Type* ret = function_type_;
      function_type_ = nullptr;
      AddAllArguments(ret->AsFunctionType(), args...);
      return ret;
    }

   private:
    Type* function_type_;
  };

  FunctionTypeBuilder Function(Type* (*return_type)()) {
    return FunctionTypeBuilder(zone(), (*return_type)());
  }

  template <typename... Overloads>
  Type* Overload(Overloads... overloads) {
    auto* ret = Type::OverloadedFunction(zone());
    AddAllOverloads(ret->AsOverloadedFunctionType(), overloads...);
    return ret;
  }

 private:
  static void AddAllOverloads(AsmOverloadedFunctionType*) {}

  template <typename Overload, typename... Others>
  static void AddAllOverloads(AsmOverloadedFunctionType* function,
                              Overload* overload, Others... others) {
    CHECK(function != nullptr);
    function->AddOverload(overload);
    AddAllOverloads(function, others...);
  }

  const std::unordered_map<Type*, std::unordered_set<Type*>> parents_;
};

// AsmValueTypeParents expose the bitmasks for the parents for each value type
// in asm's type system. It inherits from AsmValueType so that the kAsm<Foo>
// members are available when expanding the FOR_EACH_ASM_VALUE_TYPE_LIST macro.
class AsmValueTypeParents : private AsmValueType {
 public:
  enum : uint32_t {
#define V(CamelName, string_name, number, parent_types) \
  CamelName = parent_types,
    FOR_EACH_ASM_VALUE_TYPE_LIST(V)
#undef V
  };

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(AsmValueTypeParents);
};

TEST_F(AsmTypeTest, ValidateBits) {
  // Generic validation tests for the bits in the type system's type
  // definitions.

  std::unordered_set<Type*> seen_types;
  std::unordered_set<uint32_t> seen_numbers;
  uint32_t total_types = 0;
#define V(CamelName, string_name, number, parent_types)                      \
  do {                                                                       \
    ++total_types;                                                           \
    seen_types.insert(Type::CamelName());                                    \
    seen_numbers.insert(number);                                             \
    /* Every ASM type must have a valid number. */                           \
    EXPECT_NE(0, number) << Type::CamelName()->Name();                       \
    /* Inheritance cycles - unlikely, but we're paranoid and check for it */ \
    /* anyways.*/                                                            \
    EXPECT_EQ(0, (1 << (number)) & AsmValueTypeParents::CamelName);          \
  } while (0);
  FOR_EACH_ASM_VALUE_TYPE_LIST(V)
#undef V

  // At least one type was expanded.
  EXPECT_GT(total_types, 0u);

  // Each value type is unique.
  EXPECT_EQ(total_types, seen_types.size());

  // Each number is unique.
  EXPECT_EQ(total_types, seen_numbers.size());
}

TEST_F(AsmTypeTest, SaneParentsMap) {
  // This test ensures our parents map contains all the parents types that are
  // specified in the types' declaration. It does not report bogus inheritance.

  // Handy-dandy lambda for counting bits. Code borrowed from stack overflow.
  auto NumberOfSetBits = [](uintptr_t parent_mask) -> uint32_t {
    uint32_t parent_mask32 = static_cast<uint32_t>(parent_mask);
    CHECK_EQ(parent_mask, parent_mask32);
    parent_mask32 = parent_mask32 - ((parent_mask32 >> 1) & 0x55555555);
    parent_mask32 =
        (parent_mask32 & 0x33333333) + ((parent_mask32 >> 2) & 0x33333333);
    return (((parent_mask32 + (parent_mask32 >> 4)) & 0x0F0F0F0F) *
            0x01010101) >>
           24;
  };

#define V(CamelName, string_name, number, parent_types)                    \
  do {                                                                     \
    const uintptr_t parents =                                              \
        reinterpret_cast<uintptr_t>(Type::CamelName()) & ~(1 << (number)); \
    EXPECT_EQ(NumberOfSetBits(parents),                                    \
              1 + ParentsOf(Type::CamelName()).size())                     \
        << Type::CamelName()->Name() << ", parents "                       \
        << reinterpret_cast<void*>(parents) << ", type "                   \
        << static_cast<void*>(Type::CamelName());                          \
  } while (0);
  FOR_EACH_ASM_VALUE_TYPE_LIST(V)
#undef V
}

TEST_F(AsmTypeTest, Names) {
#define V(CamelName, string_name, number, parent_types)         \
  do {                                                          \
    EXPECT_THAT(Type::CamelName()->Name(), StrEq(string_name)); \
  } while (0);
  FOR_EACH_ASM_VALUE_TYPE_LIST(V)
#undef V

  EXPECT_THAT(Function(Type::Int)(Type::Double, Type::Float)->Name(),
              StrEq("(double, float) -> int"));

  EXPECT_THAT(Overload(Function(Type::Int)(Type::Double, Type::Float),
                       Function(Type::Int)(Type::Int))
                  ->Name(),
              StrEq("(double, float) -> int /\\ (int) -> int"));

  EXPECT_THAT(Type::FroundType(zone(), Type::Int())->Name(),
              StrEq("(int) -> float"));
  EXPECT_THAT(Type::FroundType(zone(), Type::Floatish())->Name(),
              StrEq("(floatish) -> float"));
  EXPECT_THAT(Type::FroundType(zone(), Type::DoubleQ())->Name(),
              StrEq("(double?) -> float"));

  EXPECT_THAT(Type::MinMaxType(zone(), Type::Int())->Name(),
              StrEq("(int, int...) -> int"));
  EXPECT_THAT(Type::MinMaxType(zone(), Type::Floatish())->Name(),
              StrEq("(floatish, floatish...) -> floatish"));
  EXPECT_THAT(Type::MinMaxType(zone(), Type::DoubleQ())->Name(),
              StrEq("(double?, double?...) -> double?"));
}

TEST_F(AsmTypeTest, IsExactly) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    for (size_t jj = 0; jj < arraysize(test_types); ++jj) {
      EXPECT_EQ(ii == jj, test_types[ii]->IsExactly(test_types[jj]))
          << test_types[ii]->Name()
          << ((ii == jj) ? " is not exactly " : " is exactly ")
          << test_types[jj]->Name();
    }
  }
}

TEST_F(AsmTypeTest, IsA) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    for (size_t jj = 0; jj < arraysize(test_types); ++jj) {
      const bool Expected =
          (ii == jj) || ParentsOf(test_types[ii]).count(test_types[jj]) != 0;
      EXPECT_EQ(Expected, test_types[ii]->IsA(test_types[jj]))
          << test_types[ii]->Name() << (Expected ? " is not a " : " is a ")
          << test_types[jj]->Name();
    }
  }
}

TEST_F(AsmTypeTest, ValidateCall) {
  auto* min_max_int = Type::MinMaxType(zone(), Type::Int());
  auto* i2i = Function(Type::Int)(Type::Int);
  auto* ii2i = Function(Type::Int)(Type::Int, Type::Int);
  auto* iii2i = Function(Type::Int)(Type::Int, Type::Int, Type::Int);
  auto* iiii2i =
      Function(Type::Int)(Type::Int, Type::Int, Type::Int, Type::Int);

  EXPECT_EQ(Type::Int(),
            min_max_int->AsCallableType()->ValidateCall(min_max_int));
  EXPECT_EQ(Type::Int(), min_max_int->AsCallableType()->ValidateCall(ii2i));
  EXPECT_EQ(Type::Int(), min_max_int->AsCallableType()->ValidateCall(iii2i));
  EXPECT_EQ(Type::Int(), min_max_int->AsCallableType()->ValidateCall(iiii2i));
  EXPECT_EQ(Type::None(), min_max_int->AsCallableType()->ValidateCall(i2i));

  auto* min_max_double = Type::MinMaxType(zone(), Type::Double());
  auto* d2d = Function(Type::Double)(Type::Double);
  auto* dd2d = Function(Type::Double)(Type::Double, Type::Double);
  auto* ddd2d =
      Function(Type::Double)(Type::Double, Type::Double, Type::Double);
  auto* dddd2d = Function(Type::Double)(Type::Double, Type::Double,
                                        Type::Double, Type::Double);
  EXPECT_EQ(Type::Double(),
            min_max_double->AsCallableType()->ValidateCall(min_max_double));
  EXPECT_EQ(Type::Double(),
            min_max_double->AsCallableType()->ValidateCall(dd2d));
  EXPECT_EQ(Type::Double(),
            min_max_double->AsCallableType()->ValidateCall(ddd2d));
  EXPECT_EQ(Type::Double(),
            min_max_double->AsCallableType()->ValidateCall(dddd2d));
  EXPECT_EQ(Type::None(), min_max_double->AsCallableType()->ValidateCall(d2d));

  auto* min_max = Overload(min_max_int, min_max_double);
  EXPECT_EQ(Type::None(), min_max->AsCallableType()->ValidateCall(min_max));
  EXPECT_EQ(Type::None(), min_max->AsCallableType()->ValidateCall(i2i));
  EXPECT_EQ(Type::None(), min_max->AsCallableType()->ValidateCall(d2d));
  EXPECT_EQ(Type::Int(), min_max->AsCallableType()->ValidateCall(min_max_int));
  EXPECT_EQ(Type::Int(), min_max->AsCallableType()->ValidateCall(ii2i));
  EXPECT_EQ(Type::Int(), min_max->AsCallableType()->ValidateCall(iii2i));
  EXPECT_EQ(Type::Int(), min_max->AsCallableType()->ValidateCall(iiii2i));
  EXPECT_EQ(Type::Double(),
            min_max->AsCallableType()->ValidateCall(min_max_double));
  EXPECT_EQ(Type::Double(), min_max->AsCallableType()->ValidateCall(dd2d));
  EXPECT_EQ(Type::Double(), min_max->AsCallableType()->ValidateCall(ddd2d));
  EXPECT_EQ(Type::Double(), min_max->AsCallableType()->ValidateCall(dddd2d));

  auto* fround_floatish = Type::FroundType(zone(), Type::Floatish());
  auto* fround_floatq = Type::FroundType(zone(), Type::FloatQ());
  auto* fround_float = Type::FroundType(zone(), Type::Float());
  auto* fround_doubleq = Type::FroundType(zone(), Type::DoubleQ());
  auto* fround_double = Type::FroundType(zone(), Type::Double());
  auto* fround_signed = Type::FroundType(zone(), Type::Signed());
  auto* fround_unsigned = Type::FroundType(zone(), Type::Unsigned());
  auto* fround_fixnum = Type::FroundType(zone(), Type::FixNum());
  auto* fround =
      Overload(fround_floatish, fround_floatq, fround_float, fround_doubleq,
               fround_double, fround_signed, fround_unsigned, fround_fixnum);

  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::Floatish)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::FloatQ)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::Float)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::DoubleQ)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::Double)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::Signed)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::Unsigned)));
  EXPECT_EQ(Type::Float(), fround->AsCallableType()->ValidateCall(
                               Function(Type::Float)(Type::FixNum)));

  auto* idf2v = Function(Type::Void)(Type::Int, Type::Double, Type::Float);
  auto* i2d = Function(Type::Double)(Type::Int);
  auto* i2f = Function(Type::Float)(Type::Int);
  auto* fi2d = Function(Type::Double)(Type::Float, Type::Int);
  auto* idif2i =
      Function(Type::Int)(Type::Int, Type::Double, Type::Int, Type::Float);
  auto* overload = Overload(idf2v, i2f, /*i2d missing, */ fi2d, idif2i);
  EXPECT_EQ(Type::Void(), overload->AsCallableType()->ValidateCall(idf2v));
  EXPECT_EQ(Type::Float(), overload->AsCallableType()->ValidateCall(i2f));
  EXPECT_EQ(Type::Double(), overload->AsCallableType()->ValidateCall(fi2d));
  EXPECT_EQ(Type::Int(), overload->AsCallableType()->ValidateCall(idif2i));
  EXPECT_EQ(Type::None(), overload->AsCallableType()->ValidateCall(i2d));
  EXPECT_EQ(Type::None(), i2f->AsCallableType()->ValidateCall(i2d));
}

TEST_F(AsmTypeTest, IsReturnType) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  std::unordered_set<Type*> return_types{
      Type::Double(), Type::Signed(), Type::Float(), Type::Void(),
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    const bool IsReturnType = return_types.count(test_types[ii]);
    EXPECT_EQ(IsReturnType, test_types[ii]->IsReturnType())
        << test_types[ii]->Name()
        << (IsReturnType ? " is not a return type" : " is a return type");
  }
}

TEST_F(AsmTypeTest, IsParameterType) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  std::unordered_set<Type*> parameter_types{
      Type::Double(), Type::Int(), Type::Float(),
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    const bool IsParameterType = parameter_types.count(test_types[ii]);
    EXPECT_EQ(IsParameterType, test_types[ii]->IsParameterType())
        << test_types[ii]->Name()
        << (IsParameterType ? " is not a parameter type"
                            : " is a parameter type");
  }
}

TEST_F(AsmTypeTest, IsComparableType) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  std::unordered_set<Type*> comparable_types{
      Type::Double(), Type::Signed(), Type::Unsigned(), Type::Float(),
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    const bool IsComparableType = comparable_types.count(test_types[ii]);
    EXPECT_EQ(IsComparableType, test_types[ii]->IsComparableType())
        << test_types[ii]->Name()
        << (IsComparableType ? " is not a comparable type"
                             : " is a comparable type");
  }
}

TEST_F(AsmTypeTest, ElementSizeInBytes) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  auto ElementSizeInBytesForType = [](Type* type) -> int32_t {
    if (type == Type::Int8Array() || type == Type::Uint8Array()) {
      return 1;
    }
    if (type == Type::Int16Array() || type == Type::Uint16Array()) {
      return 2;
    }
    if (type == Type::Int32Array() || type == Type::Uint32Array() ||
        type == Type::Float32Array()) {
      return 4;
    }
    if (type == Type::Float64Array()) {
      return 8;
    }
    return -1;
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    EXPECT_EQ(ElementSizeInBytesForType(test_types[ii]),
              test_types[ii]->ElementSizeInBytes());
  }
}

TEST_F(AsmTypeTest, LoadType) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  auto LoadTypeForType = [](Type* type) -> Type* {
    if (type == Type::Int8Array() || type == Type::Uint8Array() ||
        type == Type::Int16Array() || type == Type::Uint16Array() ||
        type == Type::Int32Array() || type == Type::Uint32Array()) {
      return Type::Intish();
    }

    if (type == Type::Float32Array()) {
      return Type::FloatQ();
    }

    if (type == Type::Float64Array()) {
      return Type::DoubleQ();
    }

    return Type::None();
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    EXPECT_EQ(LoadTypeForType(test_types[ii]), test_types[ii]->LoadType());
  }
}

TEST_F(AsmTypeTest, StoreType) {
  Type* test_types[] = {
#define CREATE(CamelName, string_name, number, parent_types) Type::CamelName(),
      FOR_EACH_ASM_VALUE_TYPE_LIST(CREATE)
#undef CREATE
          Function(Type::Int)(Type::Double),
      Function(Type::Int)(Type::DoubleQ),
      Overload(Function(Type::Int)(Type::Double)),
      Function(Type::Int)(Type::Int, Type::Int),
      Type::MinMaxType(zone(), Type::Int()), Function(Type::Int)(Type::Float),
      Type::FroundType(zone(), Type::Int()),
  };

  auto StoreTypeForType = [](Type* type) -> Type* {
    if (type == Type::Int8Array() || type == Type::Uint8Array() ||
        type == Type::Int16Array() || type == Type::Uint16Array() ||
        type == Type::Int32Array() || type == Type::Uint32Array()) {
      return Type::Intish();
    }

    if (type == Type::Float32Array()) {
      return Type::FloatishDoubleQ();
    }

    if (type == Type::Float64Array()) {
      return Type::FloatQDoubleQ();
    }

    return Type::None();
  };

  for (size_t ii = 0; ii < arraysize(test_types); ++ii) {
    EXPECT_EQ(StoreTypeForType(test_types[ii]), test_types[ii]->StoreType())
        << test_types[ii]->Name();
  }
}

}  // namespace
}  // namespace wasm
}  // namespace internal
}  // namespace v8
