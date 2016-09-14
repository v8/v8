// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ast/modules.h"
#include "src/ast/ast-value-factory.h"
#include "src/ast/scopes.h"

namespace v8 {
namespace internal {

void ModuleDescriptor::AddImport(
    const AstRawString* import_name, const AstRawString* local_name,
    const AstRawString* module_request, Scanner::Location loc, Zone* zone) {
  Entry* entry = new (zone) Entry(loc);
  entry->local_name = local_name;
  entry->import_name = import_name;
  entry->module_request = module_request;
  AddRegularImport(entry);
}


void ModuleDescriptor::AddStarImport(
    const AstRawString* local_name, const AstRawString* module_request,
    Scanner::Location loc, Zone* zone) {
  DCHECK_NOT_NULL(local_name);
  DCHECK_NOT_NULL(module_request);
  Entry* entry = new (zone) Entry(loc);
  entry->local_name = local_name;
  entry->module_request = module_request;
  AddSpecialImport(entry, zone);
}


void ModuleDescriptor::AddEmptyImport(
    const AstRawString* module_request, Scanner::Location loc, Zone* zone) {
  Entry* entry = new (zone) Entry(loc);
  entry->module_request = module_request;
  AddSpecialImport(entry, zone);
}


void ModuleDescriptor::AddExport(
    const AstRawString* local_name, const AstRawString* export_name,
    Scanner::Location loc, Zone* zone) {
  Entry* entry = new (zone) Entry(loc);
  entry->export_name = export_name;
  entry->local_name = local_name;
  AddRegularExport(entry);
}


void ModuleDescriptor::AddExport(
    const AstRawString* import_name, const AstRawString* export_name,
    const AstRawString* module_request, Scanner::Location loc, Zone* zone) {
  DCHECK_NOT_NULL(import_name);
  DCHECK_NOT_NULL(export_name);
  Entry* entry = new (zone) Entry(loc);
  entry->export_name = export_name;
  entry->import_name = import_name;
  entry->module_request = module_request;
  AddSpecialExport(entry, zone);
}


void ModuleDescriptor::AddStarExport(
    const AstRawString* module_request, Scanner::Location loc, Zone* zone) {
  Entry* entry = new (zone) Entry(loc);
  entry->module_request = module_request;
  AddSpecialExport(entry, zone);
}

namespace {

Handle<Object> ToStringOrUndefined(Isolate* isolate, const AstRawString* s) {
  return (s == nullptr)
             ? Handle<Object>::cast(isolate->factory()->undefined_value())
             : Handle<Object>::cast(s->string());
}

const AstRawString* FromStringOrUndefined(Isolate* isolate,
                                          AstValueFactory* avfactory,
                                          Handle<Object> object) {
  if (object->IsUndefined(isolate)) return nullptr;
  return avfactory->GetString(Handle<String>::cast(object));
}

}  // namespace

Handle<ModuleInfoEntry> ModuleDescriptor::Entry::Serialize(
    Isolate* isolate) const {
  return ModuleInfoEntry::New(isolate,
                              ToStringOrUndefined(isolate, export_name),
                              ToStringOrUndefined(isolate, local_name),
                              ToStringOrUndefined(isolate, import_name),
                              ToStringOrUndefined(isolate, module_request));
}

ModuleDescriptor::Entry* ModuleDescriptor::Entry::Deserialize(
    Isolate* isolate, AstValueFactory* avfactory,
    Handle<ModuleInfoEntry> entry) {
  Entry* result = new (avfactory->zone()) Entry(Scanner::Location::invalid());
  result->export_name = FromStringOrUndefined(
      isolate, avfactory, handle(entry->export_name(), isolate));
  result->local_name = FromStringOrUndefined(
      isolate, avfactory, handle(entry->local_name(), isolate));
  result->import_name = FromStringOrUndefined(
      isolate, avfactory, handle(entry->import_name(), isolate));
  result->module_request = FromStringOrUndefined(
      isolate, avfactory, handle(entry->module_request(), isolate));
  return result;
}

void ModuleDescriptor::MakeIndirectExportsExplicit(Zone* zone) {
  for (auto it = regular_exports_.begin(); it != regular_exports_.end();) {
    Entry* entry = it->second;
    DCHECK_NOT_NULL(entry->local_name);
    auto import = regular_imports_.find(entry->local_name);
    if (import != regular_imports_.end()) {
      // Found an indirect export.  Patch export entry and move it from regular
      // to special.
      DCHECK_NULL(entry->import_name);
      DCHECK_NULL(entry->module_request);
      DCHECK_NOT_NULL(import->second->import_name);
      DCHECK_NOT_NULL(import->second->module_request);
      entry->import_name = import->second->import_name;
      entry->module_request = import->second->module_request;
      entry->local_name = nullptr;
      special_exports_.Add(entry, zone);
      it = regular_exports_.erase(it);
    } else {
      it++;
    }
  }
}

const ModuleDescriptor::Entry* ModuleDescriptor::FindDuplicateExport(
    Zone* zone) const {
  const ModuleDescriptor::Entry* candidate = nullptr;
  ZoneSet<const AstRawString*> export_names(zone);
  for (const auto& it : regular_exports_) {
    const Entry* entry = it.second;
    DCHECK_NOT_NULL(entry->export_name);
    DCHECK(entry->location.IsValid());
    bool is_duplicate = !export_names.insert(entry->export_name).second;
    if (is_duplicate &&
        (candidate == nullptr ||
         entry->location.beg_pos > candidate->location.beg_pos)) {
      candidate = entry;
    }
  }
  for (auto entry : special_exports_) {
    if (entry->export_name == nullptr) continue;  // Star export.
    DCHECK(entry->location.IsValid());
    bool is_duplicate = !export_names.insert(entry->export_name).second;
    if (is_duplicate &&
        (candidate == nullptr ||
         entry->location.beg_pos > candidate->location.beg_pos)) {
      candidate = entry;
    }
  }
  return candidate;
}

bool ModuleDescriptor::Validate(ModuleScope* module_scope,
                                PendingCompilationErrorHandler* error_handler,
                                Zone* zone) {
  DCHECK_EQ(this, module_scope->module());
  DCHECK_NOT_NULL(error_handler);

  // Report error iff there are duplicate exports.
  {
    const Entry* entry = FindDuplicateExport(zone);
    if (entry != nullptr) {
      error_handler->ReportMessageAt(
          entry->location.beg_pos, entry->location.end_pos,
          MessageTemplate::kDuplicateExport, entry->export_name);
      return false;
    }
  }

  // Report error iff there are exports of non-existent local names.
  for (const auto& it : regular_exports_) {
    const Entry* entry = it.second;
    DCHECK_NOT_NULL(entry->local_name);
    if (module_scope->LookupLocal(entry->local_name) == nullptr) {
      error_handler->ReportMessageAt(
          entry->location.beg_pos, entry->location.end_pos,
          MessageTemplate::kModuleExportUndefined, entry->local_name);
      return false;
    }
  }

  MakeIndirectExportsExplicit(zone);
  return true;
}

}  // namespace internal
}  // namespace v8
