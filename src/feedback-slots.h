// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FEEDBACK_SLOTS_H_
#define V8_FEEDBACK_SLOTS_H_

#include "v8.h"

#include "isolate.h"

namespace v8 {
namespace internal {

enum ComputablePhase {
  DURING_PARSE,
  AFTER_SCOPING
};


class FeedbackSlotInterface {
 public:
  static const int kInvalidFeedbackSlot = -1;

  virtual ~FeedbackSlotInterface() {}

  // When can we ask how many feedback slots are necessary?
  virtual ComputablePhase GetComputablePhase() = 0;
  virtual int ComputeFeedbackSlotCount(Isolate* isolate) = 0;
  virtual void SetFirstFeedbackSlot(int slot) = 0;
};


class DeferredFeedbackSlotProcessor {
 public:
  DeferredFeedbackSlotProcessor()
    : slot_nodes_(NULL),
      slot_count_(0) { }

  void add_slot_node(Zone* zone, FeedbackSlotInterface* slot) {
    if (slot->GetComputablePhase() == DURING_PARSE) {
      // No need to add to the list
      int count = slot->ComputeFeedbackSlotCount(zone->isolate());
      slot->SetFirstFeedbackSlot(slot_count_);
      slot_count_ += count;
    } else {
      if (slot_nodes_ == NULL) {
        slot_nodes_ = new(zone) ZoneList<FeedbackSlotInterface*>(10, zone);
      }
      slot_nodes_->Add(slot, zone);
    }
  }

  void ProcessFeedbackSlots(Isolate* isolate) {
    // Scope analysis must have been done.
    if (slot_nodes_ == NULL) {
      return;
    }

    int current_slot = slot_count_;
    for (int i = 0; i < slot_nodes_->length(); i++) {
      FeedbackSlotInterface* slot_interface = slot_nodes_->at(i);
      int count = slot_interface->ComputeFeedbackSlotCount(isolate);
      if (count > 0) {
        slot_interface->SetFirstFeedbackSlot(current_slot);
        current_slot += count;
      }
    }

    slot_count_ = current_slot;
    slot_nodes_->Clear();
  }

  int slot_count() {
    ASSERT(slot_count_ >= 0);
    return slot_count_;
  }

 private:
  ZoneList<FeedbackSlotInterface*>* slot_nodes_;
  int slot_count_;
};


} }  // namespace v8::internal

#endif  // V8_FEEDBACK_SLOTS_H_
