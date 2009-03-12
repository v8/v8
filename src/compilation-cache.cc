// Copyright 2008 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "compilation-cache.h"

namespace v8 { namespace internal {

enum {
  NUMBER_OF_ENTRY_KINDS = CompilationCache::LAST_ENTRY + 1
};


// Keep separate tables for the different entry kinds.
static Object* tables[NUMBER_OF_ENTRY_KINDS] = { 0, };


static Handle<CompilationCacheTable> AllocateTable(int size) {
  CALL_HEAP_FUNCTION(CompilationCacheTable::Allocate(size),
                     CompilationCacheTable);
}


static Handle<CompilationCacheTable> GetTable(CompilationCache::Entry entry) {
  Handle<CompilationCacheTable> result;
  if (tables[entry]->IsUndefined()) {
    static const int kInitialCacheSize = 64;
    result = AllocateTable(kInitialCacheSize);
    tables[entry] = *result;
  } else {
    CompilationCacheTable* table = CompilationCacheTable::cast(tables[entry]);
    result = Handle<CompilationCacheTable>(table);
  }
  return result;
}


static Handle<JSFunction> Lookup(Handle<String> source,
                                 Handle<Context> context,
                                 CompilationCache::Entry entry) {
  // Make sure not to leak the table into the surrounding handle
  // scope. Otherwise, we risk keeping old tables around even after
  // having cleared the cache.
  Object* result;
  { HandleScope scope;
    Handle<CompilationCacheTable> table = GetTable(entry);
    result = table->LookupEval(*source, *context);
  }
  if (result->IsJSFunction()) {
    return Handle<JSFunction>(JSFunction::cast(result));
  } else {
    return Handle<JSFunction>::null();
  }
}


static Handle<FixedArray> Lookup(Handle<String> source,
                                 JSRegExp::Flags flags) {
  // Make sure not to leak the table into the surrounding handle
  // scope. Otherwise, we risk keeping old tables around even after
  // having cleared the cache.
  Object* result;
  { HandleScope scope;
    Handle<CompilationCacheTable> table = GetTable(CompilationCache::REGEXP);
    result = table->LookupRegExp(*source, flags);
  }
  if (result->IsFixedArray()) {
    return Handle<FixedArray>(FixedArray::cast(result));
  } else {
    return Handle<FixedArray>::null();
  }
}


Handle<JSFunction> CompilationCache::LookupScript(Handle<String> source,
                                                  Handle<Object> name,
                                                  int line_offset,
                                                  int column_offset) {
  // TODO(245): Start caching scripts again but make it local to a
  // global context to avoid sharing code between independent
  // environments.
  return Handle<JSFunction>::null();
}


Handle<JSFunction> CompilationCache::LookupEval(Handle<String> source,
                                                Handle<Context> context,
                                                Entry entry) {
  ASSERT(entry == EVAL_GLOBAL || entry == EVAL_CONTEXTUAL);
  Handle<JSFunction> result = Lookup(source, context, entry);
  if (result.is_null()) {
    Counters::compilation_cache_misses.Increment();
  } else {
    Counters::compilation_cache_hits.Increment();
  }
  return result;
}


Handle<FixedArray> CompilationCache::LookupRegExp(Handle<String> source,
                                                  JSRegExp::Flags flags) {
  Handle<FixedArray> result = Lookup(source, flags);
  if (result.is_null()) {
    Counters::compilation_cache_misses.Increment();
  } else {
    Counters::compilation_cache_hits.Increment();
  }
  return result;
}


void CompilationCache::PutScript(Handle<String> source,
                                 Entry entry,
                                 Handle<JSFunction> boilerplate) {
  // TODO(245): Start caching scripts again but make it local to a
  // global context to avoid sharing code between independent
  // environments.
}


void CompilationCache::PutEval(Handle<String> source,
                               Handle<Context> context,
                               Entry entry,
                               Handle<JSFunction> boilerplate) {
  HandleScope scope;
  ASSERT(boilerplate->IsBoilerplate());
  Handle<CompilationCacheTable> table = GetTable(entry);
  CALL_HEAP_FUNCTION_VOID(table->PutEval(*source, *context, *boilerplate));
}



void CompilationCache::PutRegExp(Handle<String> source,
                                 JSRegExp::Flags flags,
                                 Handle<FixedArray> data) {
  HandleScope scope;
  Handle<CompilationCacheTable> table = GetTable(REGEXP);
  CALL_HEAP_FUNCTION_VOID(table->PutRegExp(*source, flags, *data));
}


void CompilationCache::Clear() {
  for (int i = 0; i < NUMBER_OF_ENTRY_KINDS; i++) {
    tables[i] = Heap::undefined_value();
  }
}


void CompilationCache::Iterate(ObjectVisitor* v) {
  v->VisitPointers(&tables[0], &tables[NUMBER_OF_ENTRY_KINDS]);
}


} }  // namespace v8::internal
