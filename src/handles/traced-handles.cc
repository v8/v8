// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/handles/traced-handles.h"

#include <iterator>

#include "include/v8-internal.h"
#include "include/v8-traced-handle.h"
#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/handles/handles.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/objects.h"
#include "src/objects/visitors.h"

namespace v8::internal {

class TracedHandlesImpl;
namespace {

class TracedNodeBlock;

// TODO(v8:13372): Avoid constant and instead make use of
// `v8::base::AllocateAtLeast()` to maximize utilization of the memory.
constexpr size_t kBlockSize = 256;

constexpr uint16_t kInvalidFreeListNodeIndex = -1;

class TracedNode final {
 public:
  static TracedNode* FromLocation(Address* location) {
    return reinterpret_cast<TracedNode*>(location);
  }

  static const TracedNode* FromLocation(const Address* location) {
    return reinterpret_cast<const TracedNode*>(location);
  }

  TracedNode() = default;
  void Initialize(uint8_t, uint16_t);

  uint8_t index() const { return index_; }

  bool is_root() const { return IsRoot::decode(flags_); }
  void set_root(bool v) { flags_ = IsRoot::update(flags_, v); }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  bool is_in_use() const {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      return IsInUse::decode(flags_);
    }
    const auto flags =
        reinterpret_cast<const std::atomic<uint8_t>&>(flags_).load(
            std::memory_order_relaxed);
    return IsInUse::decode(flags);
  }
  void set_is_in_use(bool v) { flags_ = IsInUse::update(flags_, v); }

  bool is_in_young_list() const { return IsInYoungList::decode(flags_); }
  void set_is_in_young_list(bool v) {
    flags_ = IsInYoungList::update(flags_, v);
  }

  uint16_t next_free() const { return next_free_index_; }
  void set_next_free(uint16_t next_free_index) {
    next_free_index_ = next_free_index;
  }
  void set_class_id(uint16_t class_id) { class_id_ = class_id; }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  void set_markbit() {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      flags_ = Markbit::update(flags_, true);
      return;
    }
    std::atomic<uint8_t>& atomic_flags =
        reinterpret_cast<std::atomic<uint8_t>&>(flags_);
    const uint8_t new_value =
        Markbit::update(atomic_flags.load(std::memory_order_relaxed), true);
    atomic_flags.fetch_or(new_value, std::memory_order_relaxed);
  }

  template <AccessMode access_mode = AccessMode::NON_ATOMIC>
  bool markbit() const {
    if constexpr (access_mode == AccessMode::NON_ATOMIC) {
      return Markbit::decode(flags_);
    }
    const auto flags =
        reinterpret_cast<const std::atomic<uint8_t>&>(flags_).load(
            std::memory_order_relaxed);
    return Markbit::decode(flags);
  }

  void clear_markbit() { flags_ = Markbit::update(flags_, false); }

  void set_raw_object(Address value) { object_ = value; }
  Address raw_object() const { return object_; }
  Object object() const { return Object(object_); }
  Handle<Object> handle() { return Handle<Object>(&object_); }
  FullObjectSlot location() { return FullObjectSlot(&object_); }

  TracedNodeBlock& GetNodeBlock();
  const TracedNodeBlock& GetNodeBlock() const;

  Handle<Object> Publish(Object object, bool needs_young_bit_update,
                         bool needs_black_allocation);
  void Release();

 private:
  using IsInUse = base::BitField8<bool, 0, 1>;
  using IsInYoungList = IsInUse::Next<bool, 1>;
  using IsRoot = IsInYoungList::Next<bool, 1>;
  // The markbit is the exception as it can be set from the main and marker
  // threads at the same time.
  using Markbit = IsRoot::Next<bool, 1>;

  Address object_ = kNullAddress;
  uint8_t index_ = 0;
  uint8_t flags_ = 0;
  union {
    // When a node is not in use, this index is used to build the free list.
    uint16_t next_free_index_;
    // When a node is in use, the user can specify a class id.
    uint16_t class_id_;
  };
};

void TracedNode::Initialize(uint8_t index, uint16_t next_free_index) {
  static_assert(offsetof(TracedNode, class_id_) ==
                Internals::kTracedNodeClassIdOffset);
  DCHECK(!is_in_use());
  DCHECK(!is_in_young_list());
  DCHECK(!is_root());
  DCHECK(!markbit());
  index_ = index;
  next_free_index_ = next_free_index;
}

// Publishes all internal state to be consumed by other threads.
Handle<Object> TracedNode::Publish(Object object, bool needs_young_bit_update,
                                   bool needs_black_allocation) {
  DCHECK(!is_in_use());
  DCHECK(!is_root());
  DCHECK(!markbit());
  set_class_id(0);
  if (needs_young_bit_update) {
    set_is_in_young_list(true);
  }
  if (needs_black_allocation) {
    set_markbit();
  }
  set_root(true);
  set_is_in_use(true);
  reinterpret_cast<std::atomic<Address>*>(&object_)->store(
      object.ptr(), std::memory_order_release);
  return Handle<Object>(&object_);
}

void TracedNode::Release() {
  DCHECK(is_in_use());
  // Only preserve the in-young-list bit which is used to avoid duplicates in
  // TracedHandlesImpl::young_nodes_;
  flags_ &= IsInYoungList::encode(true);
  DCHECK(!is_in_use());
  DCHECK(!is_root());
  DCHECK(!markbit());
  set_raw_object(kGlobalHandleZapValue);
}

template <typename T, typename NodeAccessor>
class DoublyLinkedList final {
  template <typename U>
  class IteratorImpl final
      : public base::iterator<std::forward_iterator_tag, U> {
   public:
    explicit IteratorImpl(U* object) : object_(object) {}
    IteratorImpl(const IteratorImpl& other) V8_NOEXCEPT
        : object_(other.object_) {}
    U* operator*() { return object_; }
    bool operator==(const IteratorImpl& rhs) const {
      return rhs.object_ == object_;
    }
    bool operator!=(const IteratorImpl& rhs) const { return !(*this == rhs); }
    inline IteratorImpl& operator++() {
      object_ = ListNodeFor(object_)->next;
      return *this;
    }
    inline IteratorImpl operator++(int) {
      IteratorImpl tmp(*this);
      operator++();
      return tmp;
    }

   private:
    U* object_;
  };

 public:
  using Iterator = IteratorImpl<T>;
  using ConstIterator = IteratorImpl<const T>;

  struct ListNode {
    T* prev = nullptr;
    T* next = nullptr;
  };

  T* Front() { return front_; }

  void PushFront(T* object) {
    DCHECK(!Contains(object));

    ListNodeFor(object)->next = front_;
    if (front_) {
      ListNodeFor(front_)->prev = object;
    }
    front_ = object;
    size_++;
  }

  void PopFront() {
    DCHECK(!Empty());

    if (ListNodeFor(front_)->next) {
      ListNodeFor(ListNodeFor(front_)->next)->prev = nullptr;
    }
    front_ = ListNodeFor(front_)->next;
    size_--;
  }

  void Remove(T* object) {
    if (front_ == object) {
      front_ = ListNodeFor(object)->next;
    }
    if (ListNodeFor(object)->next) {
      ListNodeFor(ListNodeFor(object)->next)->prev = ListNodeFor(object)->prev;
      ListNodeFor(object)->next = nullptr;
    }
    if (ListNodeFor(object)->prev) {
      ListNodeFor(ListNodeFor(object)->prev)->next = ListNodeFor(object)->next;
      ListNodeFor(object)->prev = nullptr;
    }
    size_--;
  }

  bool Contains(T* object) const {
    if (front_ == object) return true;
    auto* list_node = ListNodeFor(object);
    return list_node->prev || list_node->next;
  }

  size_t Size() const { return size_; }
  bool Empty() const { return size_ == 0; }

  Iterator begin() { return Iterator(front_); }
  Iterator end() { return Iterator(nullptr); }
  ConstIterator begin() const { return ConstIterator(front_); }
  ConstIterator end() const { return ConstIterator(nullptr); }

 private:
  static ListNode* ListNodeFor(T* object) {
    return NodeAccessor::GetListNode(object);
  }
  static const ListNode* ListNodeFor(const T* object) {
    return NodeAccessor::GetListNode(const_cast<T*>(object));
  }

  T* front_ = nullptr;
  size_t size_ = 0;
};

class TracedNodeBlock final {
  struct OverallListNode {
    static auto* GetListNode(TracedNodeBlock* block) {
      return &block->overall_list_node_;
    }
  };

  struct UsableListNode {
    static auto* GetListNode(TracedNodeBlock* block) {
      return &block->usable_list_node_;
    }
  };

  class NodeIteratorImpl final
      : public base::iterator<std::forward_iterator_tag, TracedNode> {
   public:
    explicit NodeIteratorImpl(TracedNodeBlock* block) : block_(block) {}
    NodeIteratorImpl(TracedNodeBlock* block, size_t current_index)
        : block_(block), current_index_(current_index) {}
    NodeIteratorImpl(const NodeIteratorImpl& other) V8_NOEXCEPT
        : block_(other.block_),
          current_index_(other.current_index_) {}

    TracedNode* operator*() { return block_->at(current_index_); }
    bool operator==(const NodeIteratorImpl& rhs) const {
      return rhs.block_ == block_ && rhs.current_index_ == current_index_;
    }
    bool operator!=(const NodeIteratorImpl& rhs) const {
      return !(*this == rhs);
    }
    inline NodeIteratorImpl& operator++() {
      if (current_index_ < kBlockSize) {
        current_index_++;
      }
      return *this;
    }
    inline NodeIteratorImpl operator++(int) {
      NodeIteratorImpl tmp(*this);
      operator++();
      return tmp;
    }

   private:
    TracedNodeBlock* block_;
    size_t current_index_ = 0;
  };

 public:
  using OverallList = DoublyLinkedList<TracedNodeBlock, OverallListNode>;
  using UsableList = DoublyLinkedList<TracedNodeBlock, UsableListNode>;
  using Iterator = NodeIteratorImpl;

  explicit TracedNodeBlock(TracedHandlesImpl&, OverallList&, UsableList&);

  TracedNode* AllocateNode();
  void FreeNode(TracedNode*);

  const void* nodes_begin_address() const { return nodes_; }
  const void* nodes_end_address() const { return &nodes_[kBlockSize]; }

  TracedHandlesImpl& traced_handles() const { return traced_handles_; }

  TracedNode* at(size_t index) { return &nodes_[index]; }

  Iterator begin() { return Iterator(this); }
  Iterator end() { return Iterator(this, kBlockSize); }

  bool IsFull() const { return used_ == kBlockSize; }
  bool IsEmpty() const { return used_ == 0; }

 private:
  TracedNode nodes_[kBlockSize];
  OverallList::ListNode overall_list_node_;
  UsableList::ListNode usable_list_node_;
  TracedHandlesImpl& traced_handles_;
  uint16_t used_ = 0;
  uint16_t first_free_node_ = 0;
};

TracedNodeBlock::TracedNodeBlock(TracedHandlesImpl& traced_handles,
                                 OverallList& overall_list,
                                 UsableList& usable_list)
    : traced_handles_(traced_handles) {
  for (size_t i = 0; i < (kBlockSize - 1); i++) {
    nodes_[i].Initialize(i, i + 1);
  }
  nodes_[kBlockSize - 1].Initialize(kBlockSize - 1, kInvalidFreeListNodeIndex);
  overall_list.PushFront(this);
  usable_list.PushFront(this);
}

TracedNode* TracedNodeBlock::AllocateNode() {
  if (used_ == kBlockSize) {
    DCHECK_EQ(first_free_node_, kInvalidFreeListNodeIndex);
    return nullptr;
  }

  DCHECK_NE(first_free_node_, kInvalidFreeListNodeIndex);
  auto* node = &nodes_[first_free_node_];
  first_free_node_ = node->next_free();
  used_++;
  DCHECK(!node->is_in_use());
  return node;
}

void TracedNodeBlock::FreeNode(TracedNode* node) {
  DCHECK(node->is_in_use());
  node->Release();
  DCHECK(!node->is_in_use());
  node->set_next_free(first_free_node_);
  first_free_node_ = node->index();
  used_--;
}

TracedNodeBlock& TracedNode::GetNodeBlock() {
  TracedNode* first_node = this - index_;
  return *reinterpret_cast<TracedNodeBlock*>(first_node);
}

const TracedNodeBlock& TracedNode::GetNodeBlock() const {
  const TracedNode* first_node = this - index_;
  return *reinterpret_cast<const TracedNodeBlock*>(first_node);
}

bool NeedsTrackingInYoungNodes(Object value, TracedNode* node) {
  return ObjectInYoungGeneration(value) && !node->is_in_young_list();
}

void SetSlotThreadSafe(Address** slot, Address* val) {
  reinterpret_cast<std::atomic<Address*>*>(slot)->store(
      val, std::memory_order_relaxed);
}

}  // namespace

class TracedHandlesImpl final {
 public:
  explicit TracedHandlesImpl(Isolate*);
  ~TracedHandlesImpl();

  Handle<Object> Create(Address value, Address* slot,
                        GlobalHandleStoreMode store_mode);
  void Destroy(TracedNodeBlock& node_block, TracedNode& node);
  void Copy(const TracedNode& from_node, Address** to);
  void Move(TracedNode& from_node, Address** from, Address** to);

  void SetIsMarking(bool);
  void SetIsSweepingOnMutatorThread(bool);

  const TracedHandles::NodeBounds GetNodeBounds() const;

  void UpdateListOfYoungNodes();
  void ClearListOfYoungNodes();

  void ResetDeadNodes(WeakSlotCallbackWithHeap should_reset_handle);

  void ComputeWeaknessForYoungObjects(WeakSlotCallback is_unmodified);
  void ProcessYoungObjects(RootVisitor* visitor,
                           WeakSlotCallbackWithHeap should_reset_handle);

  void Iterate(RootVisitor* visitor);
  void IterateYoung(RootVisitor* visitor);
  void IterateYoungRoots(RootVisitor* visitor);

  size_t used_node_count() const { return used_; }
  size_t total_size_bytes() const {
    return sizeof(TracedNode) * kBlockSize *
           (blocks_.Size() + empty_blocks_.size());
  }
  size_t used_size_bytes() const { return sizeof(TracedNode) * used_; }

  START_ALLOW_USE_DEPRECATED()

  void Iterate(v8::EmbedderHeapTracer::TracedGlobalHandleVisitor* visitor);

  END_ALLOW_USE_DEPRECATED()

 private:
  TracedNode* AllocateNode();
  void FreeNode(TracedNode*);

  TracedNodeBlock::OverallList blocks_;
  TracedNodeBlock::UsableList usable_blocks_;
  std::vector<TracedNode*> young_nodes_;
  std::vector<TracedNodeBlock*> empty_blocks_;
  Isolate* isolate_;
  bool is_marking_ = false;
  bool is_sweeping_on_mutator_thread_ = false;
  size_t used_ = 0;
};

TracedNode* TracedHandlesImpl::AllocateNode() {
  auto* block = usable_blocks_.Front();
  if (!block) {
    if (empty_blocks_.empty()) {
      block = new TracedNodeBlock(*this, blocks_, usable_blocks_);
    } else {
      block = empty_blocks_.back();
      empty_blocks_.pop_back();
      DCHECK(block->IsEmpty());
      usable_blocks_.PushFront(block);
      blocks_.PushFront(block);
    }
    DCHECK_EQ(block, usable_blocks_.Front());
  }
  auto* node = block->AllocateNode();
  if (node) {
    used_++;
    return node;
  }

  usable_blocks_.Remove(block);
  return AllocateNode();
}

void TracedHandlesImpl::FreeNode(TracedNode* node) {
  auto& block = node->GetNodeBlock();
  if (block.IsFull() && !usable_blocks_.Contains(&block)) {
    usable_blocks_.PushFront(&block);
  }
  block.FreeNode(node);
  if (block.IsEmpty()) {
    DCHECK(usable_blocks_.Contains(&block));
    usable_blocks_.Remove(&block);
    blocks_.Remove(&block);
    empty_blocks_.push_back(&block);
  }
  used_--;
}

TracedHandlesImpl::TracedHandlesImpl(Isolate* isolate) : isolate_(isolate) {}

TracedHandlesImpl::~TracedHandlesImpl() {
  while (!blocks_.Empty()) {
    auto* block = blocks_.Front();
    blocks_.PopFront();
    delete block;
  }
  for (auto* block : empty_blocks_) {
    delete block;
  }
}

Handle<Object> TracedHandlesImpl::Create(Address value, Address* slot,
                                         GlobalHandleStoreMode store_mode) {
  Object object(value);
  auto* node = AllocateNode();
  bool needs_young_bit_update = false;
  if (NeedsTrackingInYoungNodes(object, node)) {
    needs_young_bit_update = true;
    young_nodes_.push_back(node);
  }
  bool needs_black_allocation = false;
  if (is_marking_ && store_mode != GlobalHandleStoreMode::kInitializingStore) {
    needs_black_allocation = true;
    WriteBarrier::MarkingFromGlobalHandle(object);
  }
  return node->Publish(object, needs_young_bit_update, needs_black_allocation);
}

void TracedHandlesImpl::Destroy(TracedNodeBlock& node_block, TracedNode& node) {
  DCHECK_IMPLIES(is_marking_, !is_sweeping_on_mutator_thread_);
  DCHECK_IMPLIES(is_sweeping_on_mutator_thread_, !is_marking_);

  // If sweeping on the mutator thread is running then the handle destruction
  // may be a result of a Reset() call from a destructor. The node will be
  // reclaimed on the next cycle.
  //
  // This allows v8::TracedReference::Reset() calls from destructors on
  // objects that may be used from stack and heap.
  if (is_sweeping_on_mutator_thread_) {
    return;
  }

  if (is_marking_) {
    // Incremental marking is on. This also covers the scavenge case which
    // prohibits eagerly reclaiming nodes when marking is on during a scavenge.
    //
    // On-heap traced nodes are released in the atomic pause in
    // `IterateWeakRootsForPhantomHandles()` when they are discovered as not
    // marked. Eagerly clear out the object here to avoid needlessly marking it
    // from this point on. The node will be reclaimed on the next cycle.
    node.set_raw_object(kNullAddress);
    return;
  }

  // In case marking and sweeping are off, the handle may be freed immediately.
  // Note that this includes also the case when invoking the first pass
  // callbacks during the atomic pause which requires releasing a node fully.
  FreeNode(&node);
}

void TracedHandlesImpl::Copy(const TracedNode& from_node, Address** to) {
  DCHECK_NE(kGlobalHandleZapValue, from_node.raw_object());
  Handle<Object> o =
      Create(from_node.raw_object(), reinterpret_cast<Address*>(to),
             GlobalHandleStoreMode::kAssigningStore);
  SetSlotThreadSafe(to, o.location());
#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    Object(**to).ObjectVerify(isolate_);
  }
#endif  // VERIFY_HEAP
}

void TracedHandlesImpl::Move(TracedNode& from_node, Address** from,
                             Address** to) {
  DCHECK(from_node.is_in_use());

  // Deal with old "to".
  auto* to_node = TracedNode::FromLocation(*to);
  DCHECK_IMPLIES(*to, to_node->is_in_use());
  DCHECK_IMPLIES(*to, kGlobalHandleZapValue != to_node->raw_object());
  DCHECK_NE(kGlobalHandleZapValue, from_node.raw_object());
  if (*to) {
    auto& to_node_block = to_node->GetNodeBlock();
    Destroy(to_node_block, *to_node);
  }

  // Set "to" to "from".
  SetSlotThreadSafe(to, *from);
  to_node = &from_node;

  // Deal with new "to"
  DCHECK_NOT_NULL(*to);
  DCHECK_EQ(*from, *to);
  if (is_marking_) {
    // Write barrier needs to cover node as well as object.
    to_node->set_markbit<AccessMode::ATOMIC>();
    WriteBarrier::MarkingFromGlobalHandle(to_node->object());
  }
  SetSlotThreadSafe(from, nullptr);
}

void TracedHandlesImpl::SetIsMarking(bool value) {
  DCHECK_EQ(is_marking_, !value);
  is_marking_ = value;
}

void TracedHandlesImpl::SetIsSweepingOnMutatorThread(bool value) {
  DCHECK_EQ(is_sweeping_on_mutator_thread_, !value);
  is_sweeping_on_mutator_thread_ = value;
}

const TracedHandles::NodeBounds TracedHandlesImpl::GetNodeBounds() const {
  TracedHandles::NodeBounds block_bounds;
  block_bounds.reserve(blocks_.Size());
  for (const auto* block : blocks_) {
    block_bounds.push_back(
        {block->nodes_begin_address(), block->nodes_end_address()});
  }
  std::sort(block_bounds.begin(), block_bounds.end(),
            [](const auto& pair1, const auto& pair2) {
              return pair1.first < pair2.first;
            });
  return block_bounds;
}

namespace {

void DeleteEmptyBlocks(std::vector<TracedNodeBlock*>& empty_blocks) {
  // Keep one node block around for fast allocation/deallocation patterns.
  if (empty_blocks.size() <= 1) return;

  for (size_t i = 1; i < empty_blocks.size(); i++) {
    auto* block = empty_blocks[i];
    DCHECK(block->IsEmpty());
    delete block;
  }
  empty_blocks.resize(1);
  empty_blocks.shrink_to_fit();
}

}  // namespace

void TracedHandlesImpl::UpdateListOfYoungNodes() {
  size_t last = 0;
  for (auto* node : young_nodes_) {
    DCHECK(node->is_in_young_list());
    if (node->is_in_use()) {
      if (ObjectInYoungGeneration(node->object())) {
        young_nodes_[last++] = node;
      } else {
        node->set_is_in_young_list(false);
      }
    } else {
      node->set_is_in_young_list(false);
    }
  }
  DCHECK_LE(last, young_nodes_.size());
  young_nodes_.resize(last);
  young_nodes_.shrink_to_fit();
  DeleteEmptyBlocks(empty_blocks_);
}

void TracedHandlesImpl::ClearListOfYoungNodes() {
  for (auto* node : young_nodes_) {
    DCHECK(node->is_in_young_list());
    // Nodes in use and not in use can have this bit set to false.
    node->set_is_in_young_list(false);
  }
  young_nodes_.clear();
  young_nodes_.shrink_to_fit();
  DeleteEmptyBlocks(empty_blocks_);
}

void TracedHandlesImpl::ResetDeadNodes(
    WeakSlotCallbackWithHeap should_reset_handle) {
  for (auto* block : blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      // Detect unreachable nodes first.
      if (!node->markbit()) {
        FreeNode(node);
        continue;
      }

      // Node was reachable. Clear the markbit for the next GC.
      node->clear_markbit();
      // TODO(v8:13141): Turn into a DCHECK after some time.
      CHECK(!should_reset_handle(isolate_->heap(), node->location()));
    }
  }
}

void TracedHandlesImpl::ComputeWeaknessForYoungObjects(
    WeakSlotCallback is_unmodified) {
  if (!v8_flags.reclaim_unmodified_wrappers) return;

  // Treat all objects as roots during incremental marking to avoid corrupting
  // marking worklists.
  if (is_marking_) return;

  auto* const handler = isolate_->heap()->GetEmbedderRootsHandler();
  for (TracedNode* node : young_nodes_) {
    if (node->is_in_use()) {
      DCHECK(node->is_root());
      if (is_unmodified(node->location())) {
        v8::Value* value = ToApi<v8::Value>(node->handle());
        bool r = handler->IsRoot(
            *reinterpret_cast<v8::TracedReference<v8::Value>*>(&value));
        node->set_root(r);
      }
    }
  }
}

void TracedHandlesImpl::ProcessYoungObjects(
    RootVisitor* visitor, WeakSlotCallbackWithHeap should_reset_handle) {
  if (!v8_flags.reclaim_unmodified_wrappers) return;

  auto* const handler = isolate_->heap()->GetEmbedderRootsHandler();
  for (TracedNode* node : young_nodes_) {
    if (!node->is_in_use()) continue;

    DCHECK_IMPLIES(node->is_root(),
                   !should_reset_handle(isolate_->heap(), node->location()));
    if (should_reset_handle(isolate_->heap(), node->location())) {
      v8::Value* value = ToApi<v8::Value>(node->handle());
      handler->ResetRoot(
          *reinterpret_cast<v8::TracedReference<v8::Value>*>(&value));
      // We cannot check whether a node is in use here as the reset behavior
      // depends on whether incremental marking is running when reclaiming
      // young objects.
    } else {
      if (!node->is_root()) {
        node->set_root(true);
        visitor->VisitRootPointer(Root::kGlobalHandles, nullptr,
                                  node->location());
      }
    }
  }
}

void TracedHandlesImpl::Iterate(RootVisitor* visitor) {
  for (auto* block : blocks_) {
    for (auto* node : *block) {
      if (!node->is_in_use()) continue;

      visitor->VisitRootPointer(Root::kTracedHandles, nullptr,
                                node->location());
    }
  }
}

void TracedHandlesImpl::IterateYoung(RootVisitor* visitor) {
  for (auto* node : young_nodes_) {
    if (!node->is_in_use()) continue;

    visitor->VisitRootPointer(Root::kTracedHandles, nullptr, node->location());
  }
}

void TracedHandlesImpl::IterateYoungRoots(RootVisitor* visitor) {
  for (auto* node : young_nodes_) {
    if (!node->is_in_use()) continue;

    if (!node->is_root()) continue;

    visitor->VisitRootPointer(Root::kTracedHandles, nullptr, node->location());
  }
}

START_ALLOW_USE_DEPRECATED()

void TracedHandlesImpl::Iterate(
    v8::EmbedderHeapTracer::TracedGlobalHandleVisitor* visitor) {
  for (auto* block : blocks_) {
    for (auto* node : *block) {
      if (node->is_in_use()) {
        v8::Value* value = ToApi<v8::Value>(node->handle());
        visitor->VisitTracedReference(
            *reinterpret_cast<v8::TracedReference<v8::Value>*>(&value));
      }
    }
  }
}

END_ALLOW_USE_DEPRECATED()

TracedHandles::TracedHandles(Isolate* isolate)
    : impl_(std::make_unique<TracedHandlesImpl>(isolate)) {}

TracedHandles::~TracedHandles() = default;

Handle<Object> TracedHandles::Create(Address value, Address* slot,
                                     GlobalHandleStoreMode store_mode) {
  return impl_->Create(value, slot, store_mode);
}

void TracedHandles::SetIsMarking(bool value) { impl_->SetIsMarking(value); }

void TracedHandles::SetIsSweepingOnMutatorThread(bool value) {
  impl_->SetIsSweepingOnMutatorThread(value);
}

const TracedHandles::NodeBounds TracedHandles::GetNodeBounds() const {
  return impl_->GetNodeBounds();
}

void TracedHandles::UpdateListOfYoungNodes() {
  impl_->UpdateListOfYoungNodes();
}

void TracedHandles::ClearListOfYoungNodes() { impl_->ClearListOfYoungNodes(); }

void TracedHandles::ResetDeadNodes(
    WeakSlotCallbackWithHeap should_reset_handle) {
  impl_->ResetDeadNodes(should_reset_handle);
}

void TracedHandles::ComputeWeaknessForYoungObjects(
    WeakSlotCallback is_unmodified) {
  impl_->ComputeWeaknessForYoungObjects(is_unmodified);
}

void TracedHandles::ProcessYoungObjects(
    RootVisitor* visitor, WeakSlotCallbackWithHeap should_reset_handle) {
  impl_->ProcessYoungObjects(visitor, should_reset_handle);
}

void TracedHandles::Iterate(RootVisitor* visitor) { impl_->Iterate(visitor); }

void TracedHandles::IterateYoung(RootVisitor* visitor) {
  impl_->IterateYoung(visitor);
}

void TracedHandles::IterateYoungRoots(RootVisitor* visitor) {
  impl_->IterateYoungRoots(visitor);
}

size_t TracedHandles::used_node_count() const {
  return impl_->used_node_count();
}

size_t TracedHandles::total_size_bytes() const {
  return impl_->total_size_bytes();
}

size_t TracedHandles::used_size_bytes() const {
  return impl_->used_size_bytes();
}

START_ALLOW_USE_DEPRECATED()

void TracedHandles::Iterate(
    v8::EmbedderHeapTracer::TracedGlobalHandleVisitor* visitor) {
  impl_->Iterate(visitor);
}

END_ALLOW_USE_DEPRECATED()

// static
void TracedHandles::Destroy(Address* location) {
  if (!location) return;

  auto* node = TracedNode::FromLocation(location);
  auto& node_block = node->GetNodeBlock();
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Destroy(node_block, *node);
}

// static
void TracedHandles::Copy(const Address* const* from, Address** to) {
  DCHECK_NOT_NULL(*from);
  DCHECK_NULL(*to);

  const TracedNode* from_node = TracedNode::FromLocation(*from);
  const auto& node_block = from_node->GetNodeBlock();
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Copy(*from_node, to);
}

// static
void TracedHandles::Move(Address** from, Address** to) {
  // Fast path for moving from an empty reference.
  if (!*from) {
    Destroy(*to);
    SetSlotThreadSafe(to, nullptr);
    return;
  }

  TracedNode* from_node = TracedNode::FromLocation(*from);
  auto& node_block = from_node->GetNodeBlock();
  auto& traced_handles = node_block.traced_handles();
  traced_handles.Move(*from_node, from, to);
}

// static
void TracedHandles::Mark(Address* location) {
  auto* node = TracedNode::FromLocation(location);
  DCHECK(node->is_in_use());
  node->set_markbit<AccessMode::ATOMIC>();
}

// static
Object TracedHandles::MarkConservatively(Address* inner_location,
                                         Address* traced_node_block_base) {
  // Compute the `TracedNode` address based on its inner pointer.
  const ptrdiff_t delta = reinterpret_cast<uintptr_t>(inner_location) -
                          reinterpret_cast<uintptr_t>(traced_node_block_base);
  const auto index = delta / sizeof(TracedNode);
  TracedNode& node =
      reinterpret_cast<TracedNode*>(traced_node_block_base)[index];
  // `MarkConservatively()` runs concurrently with marking code. Reading
  // state concurrently to setting the markbit is safe.
  if (!node.is_in_use<AccessMode::ATOMIC>()) return Smi::zero();
  node.set_markbit<AccessMode::ATOMIC>();
  return node.object();
}

}  // namespace v8::internal
