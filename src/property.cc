// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/property.h"

#include "src/handles-inl.h"

namespace v8 {
namespace internal {

void LookupResult::Iterate(ObjectVisitor* visitor) {
  LookupResult* current = this;  // Could be NULL.
  while (current != NULL) {
    visitor->VisitPointer(BitCast<Object**>(&current->holder_));
    visitor->VisitPointer(BitCast<Object**>(&current->transition_));
    current = current->next_;
  }
}


#ifdef OBJECT_PRINT
void LookupResult::Print(FILE* out) {
  OFStream os(out);
  if (!IsFound()) {
    os << "Not Found\n";
    return;
  }

  os << "LookupResult:\n";
  os << " -cacheable = " << (IsCacheable() ? "true" : "false") << "\n";
  os << " -attributes = " << hex << GetAttributes() << dec << "\n";
  if (IsTransition()) {
    os << " -transition target:\n";
    GetTransitionTarget()->Print(out);
    os << "\n";
  }
  switch (type()) {
    case NORMAL:
      os << " -type = normal\n"
         << " -entry = " << GetDictionaryEntry() << "\n";
      break;
    case CONSTANT:
      os << " -type = constant\n"
         << " -value:\n";
      GetConstant()->Print(out);
      os << "\n";
      break;
    case FIELD:
      os << " -type = field\n"
         << " -index = " << GetFieldIndex().property_index() << "\n"
         << " -field type:";
      GetFieldType()->PrintTo(os);
      os << "\n";
      break;
    case CALLBACKS:
      os << " -type = call backs\n"
         << " -callback object:\n";
      GetCallbackObject()->Print(out);
      break;
    case HANDLER:
      os << " -type = lookup proxy\n";
      break;
    case INTERCEPTOR:
      os << " -type = lookup interceptor\n";
      break;
    case NONEXISTENT:
      UNREACHABLE();
      break;
  }
}


void Descriptor::Print(FILE* out) {
  PrintF(out, "Descriptor ");
  GetKey()->ShortPrint(out);
  PrintF(out, " @ ");
  GetValue()->ShortPrint(out);
}
#endif

} }  // namespace v8::internal
