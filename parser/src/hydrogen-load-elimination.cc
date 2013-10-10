// Copyright 2013 the V8 project authors. All rights reserved.
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

#include "hydrogen-alias-analysis.h"
#include "hydrogen-load-elimination.h"
#include "hydrogen-instructions.h"

namespace v8 {
namespace internal {

static const int kMaxTrackedFields = 16;
static const int kMaxTrackedObjects = 5;

// An element in the field approximation list.
class HFieldApproximation : public ZoneObject {
 public:  // Just a data blob.
  HValue* object_;
  HLoadNamedField* last_load_;
  HValue* last_value_;
  HFieldApproximation* next_;
};


// The main datastructure used during load/store elimination. Each in-object
// field is tracked separately. For each field, store a list of known field
// values for known objects.
class HLoadEliminationTable BASE_EMBEDDED {
 public:
  HLoadEliminationTable(Zone* zone, HAliasAnalyzer* aliasing)
    : zone_(zone), fields_(kMaxTrackedFields, zone), aliasing_(aliasing) { }

  // Process a load instruction, updating internal table state. If a previous
  // load or store for this object and field exists, return the new value with
  // which the load should be replaced. Otherwise, return {instr}.
  HValue* load(HLoadNamedField* instr) {
    int field = FieldOf(instr->access());
    if (field < 0) return instr;

    HValue* object = instr->object()->ActualValue();
    HFieldApproximation* approx = FindOrCreate(object, field);

    if (approx->last_value_ == NULL) {
      // Load is not redundant. Fill out a new entry.
      approx->last_load_ = instr;
      approx->last_value_ = instr;
      return instr;
    } else {
      // Eliminate the load. Reuse previously stored value or load instruction.
      return approx->last_value_;
    }
  }

  // Process a store instruction, updating internal table state. If a previous
  // store to the same object and field makes this store redundant (e.g. because
  // the stored values are the same), return NULL indicating that this store
  // instruction is redundant. Otherwise, return {instr}.
  HValue* store(HStoreNamedField* instr) {
    int field = FieldOf(instr->access());
    if (field < 0) return instr;

    HValue* object = instr->object()->ActualValue();
    HValue* value = instr->value();

    // Kill non-equivalent may-alias entries.
    KillFieldInternal(object, field, value);
    if (instr->has_transition()) {
      // A transition store alters the map of the object.
      // TODO(titzer): remember the new map (a constant) for the object.
      KillFieldInternal(object, FieldOf(JSObject::kMapOffset), NULL);
    }
    HFieldApproximation* approx = FindOrCreate(object, field);

    if (Equal(approx->last_value_, value)) {
      // The store is redundant because the field already has this value.
      return NULL;
    } else {
      // The store is not redundant. Update the entry.
      approx->last_load_ = NULL;
      approx->last_value_ = value;
      return instr;
    }
  }

  // Kill everything in this table.
  void Kill() {
    fields_.Rewind(0);
  }

  // Kill all entries matching the given offset.
  void KillOffset(int offset) {
    int field = FieldOf(offset);
    if (field >= 0 && field < fields_.length()) {
      fields_[field] = NULL;
    }
  }

  // Compute the field index for the given object access; -1 if not tracked.
  int FieldOf(HObjectAccess access) {
    // Only track kMaxTrackedFields in-object fields.
    if (!access.IsInobject()) return -1;
    return FieldOf(access.offset());
  }

  // Print this table to stdout.
  void Print() {
    for (int i = 0; i < fields_.length(); i++) {
      PrintF("  field %d: ", i);
      for (HFieldApproximation* a = fields_[i]; a != NULL; a = a->next_) {
        PrintF("[o%d =", a->object_->id());
        if (a->last_load_ != NULL) PrintF(" L%d", a->last_load_->id());
        if (a->last_value_ != NULL) PrintF(" v%d", a->last_value_->id());
        PrintF("] ");
      }
      PrintF("\n");
    }
  }

 private:
  // Find or create an entry for the given object and field pair.
  HFieldApproximation* FindOrCreate(HValue* object, int field) {
    EnsureFields(field + 1);

    // Search for a field approximation for this object.
    HFieldApproximation* approx = fields_[field];
    int count = 0;
    while (approx != NULL) {
      if (aliasing_->MustAlias(object, approx->object_)) return approx;
      count++;
      approx = approx->next_;
    }

    if (count >= kMaxTrackedObjects) {
      // Pull the last entry off the end and repurpose it for this object.
      approx = ReuseLastApproximation(field);
    } else {
      // Allocate a new entry.
      approx = new(zone_) HFieldApproximation();
    }

    // Insert the entry at the head of the list.
    approx->object_ = object;
    approx->last_load_ = NULL;
    approx->last_value_ = NULL;
    approx->next_ = fields_[field];
    fields_[field] = approx;

    return approx;
  }

  // Kill all entries for a given field that _may_ alias the given object
  // and do _not_ have the given value.
  void KillFieldInternal(HValue* object, int field, HValue* value) {
    if (field >= fields_.length()) return;  // Nothing to do.

    HFieldApproximation* approx = fields_[field];
    HFieldApproximation* prev = NULL;
    while (approx != NULL) {
      if (aliasing_->MayAlias(object, approx->object_)) {
        if (!Equal(approx->last_value_, value)) {
          // Kill an aliasing entry that doesn't agree on the value.
          if (prev != NULL) {
            prev->next_ = approx->next_;
          } else {
            fields_[field] = approx->next_;
          }
          approx = approx->next_;
          continue;
        }
      }
      prev = approx;
      approx = approx->next_;
    }
  }

  bool Equal(HValue* a, HValue* b) {
    if (a == b) return true;
    if (a != NULL && b != NULL) return a->Equals(b);
    return false;
  }

  // Remove the last approximation for a field so that it can be reused.
  // We reuse the last entry because it was the first inserted and is thus
  // farthest away from the current instruction.
  HFieldApproximation* ReuseLastApproximation(int field) {
    HFieldApproximation* approx = fields_[field];
    ASSERT(approx != NULL);

    HFieldApproximation* prev = NULL;
    while (approx->next_ != NULL) {
      prev = approx;
      approx = approx->next_;
    }
    if (prev != NULL) prev->next_ = NULL;
    return approx;
  }

  // Ensure internal storage for the given number of fields.
  void EnsureFields(int num_fields) {
    while (fields_.length() < num_fields) fields_.Add(NULL, zone_);
  }

  // Compute the field index for the given in-object offset.
  int FieldOf(int offset) {
    if (offset >= kMaxTrackedFields * kPointerSize) return -1;
    ASSERT((offset % kPointerSize) == 0);  // Assume aligned accesses.
    return offset / kPointerSize;
  }

  Zone* zone_;
  ZoneList<HFieldApproximation*> fields_;
  HAliasAnalyzer* aliasing_;
};


void HLoadEliminationPhase::Run() {
  for (int i = 0; i < graph()->blocks()->length(); i++) {
    HBasicBlock* block = graph()->blocks()->at(i);
    EliminateLoads(block);
  }
}


// For code de-uglification.
#define TRACE(x) if (FLAG_trace_load_elimination) PrintF x


// Eliminate loads and stores local to a block.
void HLoadEliminationPhase::EliminateLoads(HBasicBlock* block) {
  HAliasAnalyzer aliasing;
  HLoadEliminationTable table(zone(), &aliasing);

  TRACE(("-- load-elim B%d -------------------------------------------------\n",
         block->block_id()));

  for (HInstructionIterator it(block); !it.Done(); it.Advance()) {
    bool changed = false;
    HInstruction* instr = it.Current();

    switch (instr->opcode()) {
      case HValue::kLoadNamedField: {
        HLoadNamedField* load = HLoadNamedField::cast(instr);
        TRACE((" process L%d field %d (o%d)\n",
               instr->id(),
               table.FieldOf(load->access()),
               load->object()->ActualValue()->id()));
        HValue* result = table.load(load);
        if (result != instr) {
          // The load can be replaced with a previous load or a value.
          TRACE(("  replace L%d -> v%d\n", instr->id(), result->id()));
          instr->DeleteAndReplaceWith(result);
        }
        changed = true;
        break;
      }
      case HValue::kStoreNamedField: {
        HStoreNamedField* store = HStoreNamedField::cast(instr);
        TRACE((" process S%d field %d (o%d) = v%d\n",
               instr->id(),
               table.FieldOf(store->access()),
               store->object()->ActualValue()->id(),
               store->value()->id()));
        HValue* result = table.store(store);
        if (result == NULL) {
          // The store is redundant. Remove it.
          TRACE(("  remove S%d\n", instr->id()));
          instr->DeleteAndReplaceWith(NULL);
        }
        changed = true;
        break;
      }
      default: {
        if (instr->CheckGVNFlag(kChangesInobjectFields)) {
          TRACE((" kill-all i%d\n", instr->id()));
          table.Kill();
          continue;
        }
        if (instr->CheckGVNFlag(kChangesMaps)) {
          TRACE((" kill-maps i%d\n", instr->id()));
          table.KillOffset(JSObject::kMapOffset);
        }
        if (instr->CheckGVNFlag(kChangesElementsKind)) {
          TRACE((" kill-elements-kind i%d\n", instr->id()));
          table.KillOffset(JSObject::kMapOffset);
          table.KillOffset(JSObject::kElementsOffset);
        }
        if (instr->CheckGVNFlag(kChangesElementsPointer)) {
          TRACE((" kill-elements i%d\n", instr->id()));
          table.KillOffset(JSObject::kElementsOffset);
        }
      }
    // Improvements possible:
    // - learn from HCheckMaps for field 0
    // - remove unobservable stores (write-after-write)
    }

    if (changed && FLAG_trace_load_elimination) {
      table.Print();
    }
  }
}


} }  // namespace v8::internal
