// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXPRESSION_TYPE_COLLECTOR_MACROS_H_
#define V8_EXPRESSION_TYPE_COLLECTOR_MACROS_H_

#define CHECK_TYPES_BEGIN \
  {                       \
    size_t index = 0;     \
    int depth = 0;

#define CHECK_TYPES_END          \
  CHECK_EQ(index, types.size()); \
  }

#define DEFAULT_TYPE Bounds::Unbounded(handles.main_zone())
#define INT32_TYPE                            \
  Bounds(Type::Signed32(handles.main_zone()), \
         Type::Signed32(handles.main_zone()))

#define CHECK_EXPR(ekind, type)                     \
  CHECK_LT(index, types.size());                    \
  CHECK(strcmp(#ekind, types[index].kind) == 0);    \
  CHECK_EQ(depth, types[index].depth);              \
  CHECK(type.lower->Is(types[index].bounds.lower)); \
  CHECK(type.upper->Is(types[index].bounds.upper)); \
  for (int j = (++depth, ++index, 0); j < 1 ? 1 : (--depth, 0); ++j)

#define CHECK_VAR(vname, type)                                     \
  CHECK_EXPR(VariableProxy, type);                                 \
  CHECK_EQ(#vname, std::string(types[index - 1].name->raw_data(),  \
                               types[index - 1].name->raw_data() + \
                                   types[index - 1].name->byte_length()));

#endif  // V8_EXPRESSION_TYPE_COLLECTOR_MACROS_H_
