// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/v8.h"

#include "src/ast.h"
#include "src/ast-expression-visitor.h"
#include "src/parser.h"
#include "src/rewriter.h"
#include "src/scopes.h"
#include "test/cctest/cctest.h"
#include "test/cctest/expression-type-collector.h"
#include "test/cctest/expression-type-collector-macros.h"

using namespace v8::internal;

namespace {

static void CollectTypes(HandleAndZoneScope* handles, const char* source,
                         ZoneVector<ExpressionTypeEntry>* dst) {
  i::Isolate* isolate = CcTest::i_isolate();
  i::Factory* factory = isolate->factory();

  i::Handle<i::String> source_code =
      factory->NewStringFromUtf8(i::CStrVector(source)).ToHandleChecked();

  i::Handle<i::Script> script = factory->NewScript(source_code);

  i::ParseInfo info(handles->main_zone(), script);
  i::Parser parser(&info);
  parser.set_allow_harmony_arrow_functions(true);
  parser.set_allow_harmony_sloppy(true);
  info.set_global();
  info.set_lazy(false);
  info.set_allow_lazy_parsing(false);
  info.set_toplevel(true);

  i::CompilationInfo compilation_info(&info);
  CHECK(i::Compiler::ParseAndAnalyze(&info));
  info.set_literal(
      info.scope()->declarations()->at(0)->AsFunctionDeclaration()->fun());

  ExpressionTypeCollector(&compilation_info, dst).Run();
}
}


TEST(VisitExpressions) {
  v8::V8::Initialize();
  HandleAndZoneScope handles;
  ZoneVector<ExpressionTypeEntry> types(handles.main_zone());
  const char test_function[] =
      "function GeometricMean(stdlib, foreign, buffer) {\n"
      "  \"use asm\";\n"
      "\n"
      "  var exp = stdlib.Math.exp;\n"
      "  var log = stdlib.Math.log;\n"
      "  var values = new stdlib.Float64Array(buffer);\n"
      "\n"
      "  function logSum(start, end) {\n"
      "    start = start|0;\n"
      "    end = end|0;\n"
      "\n"
      "    var sum = 0.0, p = 0, q = 0;\n"
      "\n"
      "    // asm.js forces byte addressing of the heap by requiring shifting "
      "by 3\n"
      "    for (p = start << 3, q = end << 3; (p|0) < (q|0); p = (p + 8)|0) {\n"
      "      sum = sum + +log(values[p>>3]);\n"
      "    }\n"
      "\n"
      "    return +sum;\n"
      "  }\n"
      "\n"
      " function geometricMean(start, end) {\n"
      "    start = start|0;\n"
      "    end = end|0;\n"
      "\n"
      "    return +exp(+logSum(start, end) / +((end - start)|0));\n"
      "  }\n"
      "\n"
      "  return { geometricMean: geometricMean };\n"
      "}\n";

  CollectTypes(&handles, test_function, &types);
  CHECK_TYPES_BEGIN {
    // function logSum
    CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {
      CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(start, DEFAULT_TYPE);
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(start, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
        }
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(end, DEFAULT_TYPE);
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(end, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
        }
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(sum, DEFAULT_TYPE);
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(p, DEFAULT_TYPE);
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(q, DEFAULT_TYPE);
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
        // for (p = start << 3, q = end << 3;
        CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
          CHECK_EXPR(Assignment, DEFAULT_TYPE) {
            CHECK_VAR(p, DEFAULT_TYPE);
            CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
              CHECK_VAR(start, DEFAULT_TYPE);
              CHECK_EXPR(Literal, DEFAULT_TYPE);
            }
          }
          CHECK_EXPR(Assignment, DEFAULT_TYPE) {
            CHECK_VAR(q, DEFAULT_TYPE);
            CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
              CHECK_VAR(end, DEFAULT_TYPE);
              CHECK_EXPR(Literal, DEFAULT_TYPE);
            }
          }
        }
        // (p|0) < (q|0);
        CHECK_EXPR(CompareOperation, DEFAULT_TYPE) {
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(p, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(q, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
        }
        // p = (p + 8)|0) {\n"
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(p, DEFAULT_TYPE);
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
              CHECK_VAR(p, DEFAULT_TYPE);
              CHECK_EXPR(Literal, DEFAULT_TYPE);
            }
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
        }
        // sum = sum + +log(values[p>>3]);
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(sum, DEFAULT_TYPE);
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(sum, DEFAULT_TYPE);
            CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
              CHECK_EXPR(Call, DEFAULT_TYPE) {
                CHECK_VAR(log, DEFAULT_TYPE);
                CHECK_EXPR(Property, DEFAULT_TYPE) {
                  CHECK_VAR(values, DEFAULT_TYPE);
                  CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
                    CHECK_VAR(p, DEFAULT_TYPE);
                    CHECK_EXPR(Literal, DEFAULT_TYPE);
                  }
                }
              }
              CHECK_EXPR(Literal, DEFAULT_TYPE);
            }
          }
        }
        // return +sum;
        CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
          CHECK_VAR(sum, DEFAULT_TYPE);
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
      }
      // function geometricMean
      CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(start, DEFAULT_TYPE);
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(start, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
        }
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(end, DEFAULT_TYPE);
          CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
            CHECK_VAR(end, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
        }
        // return +exp(+logSum(start, end) / +((end - start)|0));
        CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
          CHECK_EXPR(Call, DEFAULT_TYPE) {
            CHECK_VAR(exp, DEFAULT_TYPE);
            CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
              CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
                CHECK_EXPR(Call, DEFAULT_TYPE) {
                  CHECK_VAR(logSum, DEFAULT_TYPE);
                  CHECK_VAR(start, DEFAULT_TYPE);
                  CHECK_VAR(end, DEFAULT_TYPE);
                }
                CHECK_EXPR(Literal, DEFAULT_TYPE);
              }
              CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
                CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
                  CHECK_EXPR(BinaryOperation, DEFAULT_TYPE) {
                    CHECK_VAR(end, DEFAULT_TYPE);
                    CHECK_VAR(start, DEFAULT_TYPE);
                  }
                  CHECK_EXPR(Literal, DEFAULT_TYPE);
                }
                CHECK_EXPR(Literal, DEFAULT_TYPE);
              }
            }
          }
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
      }
      // "use asm";
      CHECK_EXPR(Literal, DEFAULT_TYPE);
      // var exp = stdlib.Math.exp;
      CHECK_EXPR(Assignment, DEFAULT_TYPE) {
        CHECK_VAR(exp, DEFAULT_TYPE);
        CHECK_EXPR(Property, DEFAULT_TYPE) {
          CHECK_EXPR(Property, DEFAULT_TYPE) {
            CHECK_VAR(stdlib, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
      }
      // var log = stdlib.Math.log;
      CHECK_EXPR(Assignment, DEFAULT_TYPE) {
        CHECK_VAR(log, DEFAULT_TYPE);
        CHECK_EXPR(Property, DEFAULT_TYPE) {
          CHECK_EXPR(Property, DEFAULT_TYPE) {
            CHECK_VAR(stdlib, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
          CHECK_EXPR(Literal, DEFAULT_TYPE);
        }
      }
      // var values = new stdlib.Float64Array(buffer);
      CHECK_EXPR(Assignment, DEFAULT_TYPE) {
        CHECK_VAR(values, DEFAULT_TYPE);
        CHECK_EXPR(CallNew, DEFAULT_TYPE) {
          CHECK_EXPR(Property, DEFAULT_TYPE) {
            CHECK_VAR(stdlib, DEFAULT_TYPE);
            CHECK_EXPR(Literal, DEFAULT_TYPE);
          }
          CHECK_VAR(buffer, DEFAULT_TYPE);
        }
      }
      // return { geometricMean: geometricMean };
      CHECK_EXPR(ObjectLiteral, DEFAULT_TYPE) {
        CHECK_VAR(geometricMean, DEFAULT_TYPE);
      }
    }
  }
  CHECK_TYPES_END
}


TEST(VisitEmptyForStatment) {
  v8::V8::Initialize();
  HandleAndZoneScope handles;
  ZoneVector<ExpressionTypeEntry> types(handles.main_zone());
  // Check that traversing an empty for statement works.
  const char test_function[] =
      "function foo() {\n"
      "  for (;;) {}\n"
      "}\n";
  CollectTypes(&handles, test_function, &types);
  CHECK_TYPES_BEGIN {
    CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {}
  }
  CHECK_TYPES_END
}


TEST(VisitSwitchStatment) {
  v8::V8::Initialize();
  HandleAndZoneScope handles;
  ZoneVector<ExpressionTypeEntry> types(handles.main_zone());
  // Check that traversing a switch with a default works.
  const char test_function[] =
      "function foo() {\n"
      "  switch (0) { case 1: break; default: break; }\n"
      "}\n";
  CollectTypes(&handles, test_function, &types);
  CHECK_TYPES_BEGIN {
    CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {
      CHECK_EXPR(Assignment, DEFAULT_TYPE) {
        CHECK_VAR(.switch_tag, DEFAULT_TYPE);
        CHECK_EXPR(Literal, DEFAULT_TYPE);
      }
      CHECK_EXPR(Literal, DEFAULT_TYPE);
      CHECK_VAR(.switch_tag, DEFAULT_TYPE);
      CHECK_EXPR(Literal, DEFAULT_TYPE);
    }
  }
  CHECK_TYPES_END
}


TEST(VisitThrow) {
  v8::V8::Initialize();
  HandleAndZoneScope handles;
  ZoneVector<ExpressionTypeEntry> types(handles.main_zone());
  // Check that traversing an empty for statement works.
  const char test_function[] =
      "function foo() {\n"
      "  throw 123;\n"
      "}\n";
  CollectTypes(&handles, test_function, &types);
  CHECK_TYPES_BEGIN {
    CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {
      CHECK_EXPR(Throw, DEFAULT_TYPE) { CHECK_EXPR(Literal, DEFAULT_TYPE); }
    }
  }
  CHECK_TYPES_END
}


TEST(VisitYield) {
  v8::V8::Initialize();
  HandleAndZoneScope handles;
  ZoneVector<ExpressionTypeEntry> types(handles.main_zone());
  // Check that traversing an empty for statement works.
  const char test_function[] =
      "function* foo() {\n"
      "  yield 123;\n"
      "}\n";
  CollectTypes(&handles, test_function, &types);
  CHECK_TYPES_BEGIN {
    CHECK_EXPR(FunctionLiteral, DEFAULT_TYPE) {
      // Generator function yields generator on entry.
      CHECK_EXPR(Yield, DEFAULT_TYPE) {
        CHECK_VAR(.generator_object, DEFAULT_TYPE);
        CHECK_EXPR(Assignment, DEFAULT_TYPE) {
          CHECK_VAR(.generator_object, DEFAULT_TYPE);
          CHECK_EXPR(CallRuntime, DEFAULT_TYPE);
        }
      }
      // Then yields undefined.
      CHECK_EXPR(Yield, DEFAULT_TYPE) {
        CHECK_VAR(.generator_object, DEFAULT_TYPE);
        CHECK_EXPR(Literal, DEFAULT_TYPE);
      }
      // Then yields 123.
      CHECK_EXPR(Yield, DEFAULT_TYPE) {
        CHECK_VAR(.generator_object, DEFAULT_TYPE);
        CHECK_EXPR(Literal, DEFAULT_TYPE);
      }
    }
  }
  CHECK_TYPES_END
}
