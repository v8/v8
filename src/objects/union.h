// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_UNION_H_
#define V8_OBJECTS_UNION_H_

#include "src/base/template-utils.h"
#include "src/common/globals.h"

namespace v8::internal {

class Smi;
class Hole;

// Union<Ts...> represents a union of multiple V8 types.
//
// Unions are required to be non-nested (i.e. no unions of unions), and to
// have each type only once. The UnionOf<Ts...> helper can be used to flatten
// nested unions and remove duplicates.
//
// Inheritance from Unions is forbidden because it messes with `is_subtype`
// checking.
template <typename... Ts>
class Union;

// is_union<T> is a type trait that returns true if T is a union.
template <typename... Ts>
struct is_union : public std::false_type {};
template <typename... Ts>
struct is_union<Union<Ts...>> : public std::true_type {};
template <typename... Ts>
static constexpr bool is_union_v = is_union<Ts...>::value;

namespace detail {

template <typename Accumulator, typename TWithout, typename... InputTypes>
struct UnionWithoutHelper;

// Base case: No input types, return the accumulated types.
template <typename... OutputTs, typename TWithout>
struct UnionWithoutHelper<Union<OutputTs...>, TWithout> {
  using type = Union<OutputTs...>;
};

// Recursive case: Found Head matching TWithout, drop it and accumulate the
// remainder.
template <typename... OutputTs, typename TWithout, typename... Ts>
struct UnionWithoutHelper<Union<OutputTs...>, TWithout, TWithout, Ts...> {
  using type = Union<OutputTs..., Ts...>;
};

// Recursive case: Non-matching input, accumulate and continue.
template <typename... OutputTs, typename TWithout, typename Head,
          typename... Ts>
struct UnionWithoutHelper<Union<OutputTs...>, TWithout, Head, Ts...> {
  // Don't accumulate duplicate types.
  using type = typename UnionWithoutHelper<Union<OutputTs..., Head>, TWithout,
                                           Ts...>::type;
};

}  // namespace detail

template <typename... Ts>
class Union final : public AllStatic {
 public:
  static_assert(((!is_union_v<Ts>) && ...),
                "Cannot have a union of unions -- use the UnionOf<T...> helper "
                "to flatten nested unions");
  static_assert(
      (base::has_type_v<Ts, Ts...> && ...),
      "Unions should have each type only once -- use the UnionOf<T...> "
      "helper to deduplicate unions");

  template <typename U>
  using Without = typename detail::UnionWithoutHelper<Union<>, U, Ts...>::type;
};

namespace detail {

#if !V8_HAS_BUILTIN_DEDUP_PACK
template <typename Result, typename... Ts>
struct Deduplicate;

template <typename... ResultTs>
struct Deduplicate<Union<ResultTs...>> {
  using type = Union<ResultTs...>;
};

template <typename... ResultTs, typename Head, typename... Tail>
struct Deduplicate<Union<ResultTs...>, Head, Tail...> {
  using type = std::conditional_t<
      base::has_type_v<Head, ResultTs...>,
      typename Deduplicate<Union<ResultTs...>, Tail...>::type,
      typename Deduplicate<Union<ResultTs..., Head>, Tail...>::type>;
};
#endif

template <typename T>
struct FinalizeUnion {
  using type = T;
};
template <>
struct FinalizeUnion<Union<>> {
  using type = void;
};
template <typename T>
struct FinalizeUnion<Union<T>> {
  using type = T;
};
template <typename... Ts>
struct FinalizeUnion<Union<Ts...>> {
  using type = Union<Ts...>;
};

template <typename Accumulator, typename... InputTypes>
struct FlattenUnionHelper;

// Base case: No input types, deduplicate and return the accumulated types.
template <typename... OutputTs>
struct FlattenUnionHelper<Union<OutputTs...>> {
#if V8_HAS_BUILTIN_DEDUP_PACK
  using deduped = Union<__builtin_dedup_pack<OutputTs...>...>;
#else
  using deduped = typename Deduplicate<Union<>, OutputTs...>::type;
#endif
  using type = typename FinalizeUnion<deduped>::type;
};

// Recursive case: Non-union input, accumulate and continue.
template <typename... OutputTs, typename Head, typename... Ts>
struct FlattenUnionHelper<Union<OutputTs...>, Head, Ts...>
    : FlattenUnionHelper<Union<OutputTs..., Head>, Ts...> {};

// Recursive case: Smi input, normalize to always be the first element.
template <typename... OutputTs, typename... Ts>
struct FlattenUnionHelper<Union<OutputTs...>, Smi, Ts...>
    : FlattenUnionHelper<Union<Smi, OutputTs...>, Ts...> {};

// Recursive case: Union input, flatten and continue.
template <typename... OutputTs, typename... HeadTs, typename... Ts>
struct FlattenUnionHelper<Union<OutputTs...>, Union<HeadTs...>, Ts...>
    : FlattenUnionHelper<Union<OutputTs...>, HeadTs..., Ts...> {};

}  // namespace detail

// UnionOf<Ts...> is a helper that returns a union of multiple V8 types,
// flattening any nested unions and removing duplicate types.
template <typename... Ts>
using UnionOf = typename detail::FlattenUnionHelper<Union<>, Ts...>::type;

// Unions of unions are flattened.
static_assert(std::is_same_v<Union<Smi, HeapObject>,
                             UnionOf<UnionOf<Smi>, UnionOf<HeapObject>>>);
// Unions with duplicates are deduplicated.
static_assert(std::is_same_v<Union<Smi, HeapObject>,
                             UnionOf<HeapObject, Smi, Smi, HeapObject>>);
// Unions with Smis are normalized to have the Smi be the first element.
static_assert(std::is_same_v<Union<Smi, HeapObject>, UnionOf<HeapObject, Smi>>);

// Union::Without matches expectations.
static_assert(
    std::is_same_v<Union<Smi, HeapObject>::Without<Smi>, Union<HeapObject>>);
static_assert(std::is_same_v<JSAny::Without<Smi>, JSAnyNotSmi>);
static_assert(
    std::is_same_v<JSAny::Without<Smi>::Without<HeapNumber>, JSAnyNotNumber>);

// Union::Without that doesn't have a match is a no-op
static_assert(std::is_same_v<Union<Smi, HeapObject>::Without<HeapNumber>,
                             Union<Smi, HeapObject>>);

}  // namespace v8::internal

#endif  // V8_OBJECTS_UNION_H_
