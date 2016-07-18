// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_AST_MODULES_H_
#define V8_AST_MODULES_H_

#include "src/parsing/scanner.h"  // Only for Scanner::Location.
#include "src/pending-compilation-error-handler.h"
#include "src/zone.h"

namespace v8 {
namespace internal {


class AstRawString;


class ModuleDescriptor : public ZoneObject {
 public:
  explicit ModuleDescriptor(Zone* zone)
      : exports_(1, zone), imports_(1, zone) {}

  // import x from "foo.js";
  // import {x} from "foo.js";
  // import {x as y} from "foo.js";
  void AddImport(
    const AstRawString* import_name, const AstRawString* local_name,
    const AstRawString* module_request, const Scanner::Location loc,
    Zone* zone);

  // import * as x from "foo.js";
  void AddStarImport(
    const AstRawString* local_name, const AstRawString* module_request,
    const Scanner::Location loc, Zone* zone);

  // import "foo.js";
  // import {} from "foo.js";
  // export {} from "foo.js";  (sic!)
  void AddEmptyImport(
      const AstRawString* module_request, const Scanner::Location loc,
      Zone* zone);

  // export {x};
  // export {x as y};
  // export VariableStatement
  // export Declaration
  // export default ...
  void AddExport(
    const AstRawString* local_name, const AstRawString* export_name,
    const Scanner::Location loc, Zone* zone);

  // export {x} from "foo.js";
  // export {x as y} from "foo.js";
  void AddExport(
    const AstRawString* export_name, const AstRawString* import_name,
    const AstRawString* module_request, const Scanner::Location loc,
    Zone* zone);

  // export * from "foo.js";
  void AddStarExport(
    const AstRawString* module_request, const Scanner::Location loc,
    Zone* zone);

  // Check if module is well-formed and report error if not.
  bool Validate(
      Scope* module_scope, PendingCompilationErrorHandler* error_handler,
      Zone* zone) const;


 private:
  struct ModuleEntry : public ZoneObject {
    const Scanner::Location location;
    const AstRawString* export_name;
    const AstRawString* local_name;
    const AstRawString* import_name;
    const AstRawString* module_request;

    explicit ModuleEntry(Scanner::Location loc)
        : location(loc),
          export_name(nullptr),
          local_name(nullptr),
          import_name(nullptr),
          module_request(nullptr) {}
  };

  ZoneList<const ModuleEntry*> exports_;
  ZoneList<const ModuleEntry*> imports_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_AST_MODULES_H_
