// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-arch.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/constant-pool.h"

namespace v8 {
namespace internal {

// Pool entries are accessed with pc relative load therefore this cannot be more
// than 1 * MB. Since constant pool emission checks are interval based, and we
// want to keep entries close to the code, we try to emit every 64KB.
const size_t ConstantPool::kMaxDistToPool32 = 1 * MB;
const size_t ConstantPool::kMaxDistToPool64 = 1 * MB;
const size_t ConstantPool::kCheckInterval = 128 * kInstrSize;
const size_t ConstantPool::kApproxDistToPool32 = 64 * KB;
const size_t ConstantPool::kApproxDistToPool64 = kApproxDistToPool32;

const size_t ConstantPool::kOpportunityDistToPool32 = 64 * KB;
const size_t ConstantPool::kOpportunityDistToPool64 = 64 * KB;
const size_t ConstantPool::kApproxMaxEntryCount = 512;

ConstantPool::ConstantPool(Assembler* assm) : assm_(assm) {}
ConstantPool::~ConstantPool() { DCHECK_EQ(blocked_nesting_, 0); }

RelocInfoStatus ConstantPool::RecordEntry64(uint64_t data,
                                            RelocInfo::Mode rmode) {
  ConstantPoolKey key(data, rmode);
  CHECK(!key.is_value32());
  return RecordKey(std::move(key), assm_->pc_offset());
}

RelocInfoStatus ConstantPool::RecordKey(ConstantPoolKey key, int offset) {
  RelocInfoStatus write_reloc_info = GetRelocInfoStatusFor(key);
  if (write_reloc_info == RelocInfoStatus::kMustRecord) {
    if (key.is_value32()) {
      if (entry32_count_ == 0) first_use_32_ = offset;
      ++entry32_count_;
    } else {
      if (entry64_count_ == 0) first_use_64_ = offset;
      ++entry64_count_;
    }
  }
  entries_.insert(std::make_pair(key, offset));

  if (Entry32Count() + Entry64Count() > ConstantPool::kApproxMaxEntryCount) {
    // Request constant pool emission after the next instruction.
    SetNextCheckIn(1);
  }

  return write_reloc_info;
}

RelocInfoStatus ConstantPool::GetRelocInfoStatusFor(
    const ConstantPoolKey& key) {
  if (key.AllowsDeduplication()) {
    auto existing = entries_.find(key);
    if (existing != entries_.end()) {
      return RelocInfoStatus::kMustOmitForDuplicate;
    }
  }
  return RelocInfoStatus::kMustRecord;
}

void ConstantPool::EmitAndClear(Jump require_jump) {
  DCHECK(!IsBlocked());
  // Prevent recursive pool emission. We conservatively assume that we will
  // have to add padding for alignment, so the margin is guaranteed to be
  // at least as large as the actual size of the constant pool.
  int margin = ComputeSize(require_jump, Alignment::kRequired);
  Assembler::BlockPoolsScope block_pools(assm_, PoolEmissionCheck::kSkip,
                                         margin);

  // The pc offset may have changed as a result of blocking pools. We can
  // now go ahead and compute the required alignment and the correct size.
  Alignment require_alignment =
      IsAlignmentRequiredIfEmittedAt(require_jump, assm_->pc_offset());
  int size = ComputeSize(require_jump, require_alignment);
  DCHECK_LE(size, margin);
  Label size_check;
  assm_->bind(&size_check);
  assm_->RecordConstPool(size);

  // Emit the constant pool. It is preceded by an optional branch if
  // {require_jump} and a header which will:
  //  1) Encode the size of the constant pool, for use by the disassembler.
  //  2) Terminate the program, to try to prevent execution from accidentally
  //     flowing into the constant pool.
  //  3) align the 64bit pool entries to 64-bit.
  // TODO(all): Make the alignment part less fragile. Currently code is
  // allocated as a byte array so there are no guarantees the alignment will
  // be preserved on compaction. Currently it works as allocation seems to be
  // 64-bit aligned.
  DEBUG_PRINTF("\tConstant Pool start\n")
  Label after_pool;
  if (require_jump == Jump::kRequired) assm_->b(&after_pool);

  assm_->RecordComment("[ Constant Pool");

  EmitPrologue(require_alignment);
  if (require_alignment == Alignment::kRequired) assm_->DataAlign(kInt64Size);
  EmitEntries();
  // Emit padding data to ensure the constant pool size matches the expected
  // constant count during disassembly.
  if (v8_flags.riscv_c_extension) {
    int code_size = assm_->SizeOfCodeGeneratedSince(&size_check);
    DCHECK_LE(code_size, size);

    while (code_size < size) {
      assm_->db(0xcc);
      code_size++;
    }
  }
  assm_->RecordComment("]");
  assm_->bind(&after_pool);
  DEBUG_PRINTF("\tConstant Pool end\n")

  DCHECK_EQ(size, assm_->SizeOfCodeGeneratedSince(&size_check));
  Clear();
}

void ConstantPool::Clear() {
  entries_.clear();
  first_use_32_ = -1;
  first_use_64_ = -1;
  entry32_count_ = 0;
  entry64_count_ = 0;
  next_check_ = 0;
}

void ConstantPool::StartBlock() {
  if (blocked_nesting_ == 0) {
    // Prevent constant pool checks from happening by setting the next check to
    // the biggest possible offset.
    next_check_ = kMaxInt;
  }
  ++blocked_nesting_;
}

void ConstantPool::EndBlock() {
  --blocked_nesting_;
  if (blocked_nesting_ == 0) {
    DCHECK(IsInImmRangeIfEmittedAt(assm_->pc_offset()));
    // Make sure a check happens quickly after getting unblocked.
    next_check_ = 0;
  }
}

bool ConstantPool::IsBlocked() const { return blocked_nesting_ > 0; }

void ConstantPool::SetNextCheckIn(size_t instructions) {
  next_check_ =
      assm_->pc_offset() + static_cast<int>(instructions * kInstrSize);
}

void ConstantPool::EmitEntries() {
  for (auto iter = entries_.begin(); iter != entries_.end();) {
    DCHECK(iter->first.is_value32() || IsAligned(assm_->pc_offset(), 8));
    auto range = entries_.equal_range(iter->first);
    bool shared = iter->first.AllowsDeduplication();
    for (auto it = range.first; it != range.second; ++it) {
      SetLoadOffsetToConstPoolEntry(it->second, assm_->pc(), it->first);
      if (!shared) Emit(it->first);
    }
    if (shared) Emit(iter->first);
    iter = range.second;
  }
}

void ConstantPool::Emit(const ConstantPoolKey& key) {
  if (key.is_value32()) {
    assm_->dd(key.value32());
  } else {
    assm_->dq(key.value64());
  }
}

bool ConstantPool::ShouldEmitNow(Jump require_jump, size_t margin) const {
  if (IsEmpty()) return false;
  if (Entry32Count() + Entry64Count() > ConstantPool::kApproxMaxEntryCount) {
    return true;
  }
  // We compute {dist32/64}, i.e. the distance from the first instruction
  // accessing a 32bit/64bit entry in the constant pool to any of the
  // 32bit/64bit constant pool entries, respectively. This is required because
  // we do not guarantee that entries are emitted in order of reference, i.e. it
  // is possible that the entry with the earliest reference is emitted last.
  // The constant pool should be emitted if either of the following is true:
  // (A) {dist32/64} will be out of range at the next check in.
  // (B) Emission can be done behind an unconditional branch and {dist32/64}
  // exceeds {kOpportunityDist*}.
  // (C) {dist32/64} exceeds the desired approximate distance to the pool.
  int worst_case_size = ComputeSize(Jump::kRequired, Alignment::kRequired);
  size_t pool_end_32 = assm_->pc_offset() + margin + worst_case_size;
  size_t pool_end_64 = pool_end_32 - Entry32Count() * kInt32Size;
  if (Entry64Count() != 0) {
    // The 64-bit constants are always emitted before the 32-bit constants, so
    // we subtract the size of the 32-bit constants from {size}.
    size_t dist64 = pool_end_64 - first_use_64_;
    bool next_check_too_late = dist64 + 2 * kCheckInterval >= kMaxDistToPool64;
    bool opportune_emission_without_jump =
        require_jump == Jump::kOmitted && (dist64 >= kOpportunityDistToPool64);
    bool approximate_distance_exceeded = dist64 >= kApproxDistToPool64;
    if (next_check_too_late || opportune_emission_without_jump ||
        approximate_distance_exceeded) {
      return true;
    }
  }
  if (Entry32Count() != 0) {
    size_t dist32 = pool_end_32 - first_use_32_;
    bool next_check_too_late = dist32 + 2 * kCheckInterval >= kMaxDistToPool32;
    bool opportune_emission_without_jump =
        require_jump == Jump::kOmitted && (dist32 >= kOpportunityDistToPool32);
    bool approximate_distance_exceeded = dist32 >= kApproxDistToPool32;
    if (next_check_too_late || opportune_emission_without_jump ||
        approximate_distance_exceeded) {
      return true;
    }
  }
  return false;
}

int ConstantPool::ComputeSize(Jump require_jump,
                              Alignment require_alignment) const {
  int size_up_to_marker = PrologueSize(require_jump);
  // With RVC enabled, constant pool alignment must use kInt64Size to ensure
  // sufficient padding space for 8-byte alignment; otherwise, alignment may
  // fail.
  //
  // Example:
  //   pc_offset = 0x22
  //   Aligned(0x22, kInt64Size) = 0x28 → 6 bytes of padding needed.
  int alignment = require_alignment == Alignment::kRequired
                      ? (v8_flags.riscv_c_extension ? kInt64Size : kInstrSize)
                      : 0;
  size_t size_after_marker =
      Entry32Count() * kInt32Size + alignment + Entry64Count() * kInt64Size;
  return size_up_to_marker + static_cast<int>(size_after_marker);
}

Alignment ConstantPool::IsAlignmentRequiredIfEmittedAt(Jump require_jump,
                                                       int pc_offset) const {
  // When the RVC extension is enabled, constant pool entries must be aligned to
  // kInstrSize to prevent unaligned 32-bit memory accesses.
  int size_up_to_marker = PrologueSize(require_jump);
  if ((Entry64Count() != 0 &&
       !IsAligned(pc_offset + size_up_to_marker, kInt64Size)) ||
      (Entry32Count() != 0 && v8_flags.riscv_c_extension &&
       !IsAligned(pc_offset + size_up_to_marker, kInstrSize))) {
    return Alignment::kRequired;
  }
  return Alignment::kOmitted;
}

bool ConstantPool::IsInImmRangeIfEmittedAt(int pc_offset) {
  // Check that all entries are in range if the pool is emitted at {pc_offset}.
  // This ignores kPcLoadDelta (conservatively, since all offsets are positive),
  // and over-estimates the last entry's address with the pool's end.
  Alignment require_alignment =
      IsAlignmentRequiredIfEmittedAt(Jump::kRequired, pc_offset);
  size_t pool_end_32 =
      pc_offset + ComputeSize(Jump::kRequired, require_alignment);
  size_t pool_end_64 = pool_end_32 - Entry32Count() * kInt32Size;
  bool entries_in_range_32 =
      Entry32Count() == 0 || (pool_end_32 < first_use_32_ + kMaxDistToPool32);
  bool entries_in_range_64 =
      Entry64Count() == 0 || (pool_end_64 < first_use_64_ + kMaxDistToPool64);
  return entries_in_range_32 && entries_in_range_64;
}

ConstantPool::BlockScope::BlockScope(Assembler* assm, size_t margin)
    : pool_(&assm->constpool_) {
  pool_->assm_->EmitConstPoolWithJumpIfNeeded(margin);
  pool_->StartBlock();
}

ConstantPool::BlockScope::BlockScope(Assembler* assm, PoolEmissionCheck check)
    : pool_(&assm->constpool_) {
  DCHECK_EQ(check, PoolEmissionCheck::kSkip);
  pool_->StartBlock();
}

ConstantPool::BlockScope::~BlockScope() { pool_->EndBlock(); }

void ConstantPool::MaybeCheck() {
  if (assm_->pc_offset() >= next_check_) {
    Check(Emission::kIfNeeded, Jump::kRequired);
  }
}

void ConstantPool::EmitPrologue(Alignment require_alignment) {
  // Recorded constant pool size is expressed in number of 32-bits words,
  // and includes prologue and alignment, but not the jump around the pool
  // and the size of the marker itself.
  // word_count may exceed 12 bits, so auipc is used.
  const int marker_size = 1;
  int word_count =
      ComputeSize(Jump::kOmitted, require_alignment) / kInt32Size - marker_size;
  DCHECK(is_int20(word_count));
  assm_->auipc(zero_reg, word_count);
  assm_->EmitPoolGuard();
}

int ConstantPool::PrologueSize(Jump require_jump) const {
  // Prologue is:
  //   j over  ;; if require_jump
  //   ld x0, x0, #pool_size
  //   j 0x0
  int prologue_size = require_jump == Jump::kRequired ? kInstrSize : 0;
  prologue_size += 2 * kInstrSize;
  return prologue_size;
}

void ConstantPool::SetLoadOffsetToConstPoolEntry(int load_offset,
                                                 Instruction* entry_offset,
                                                 const ConstantPoolKey& key) {
  Instr instr_auipc = assm_->instr_at(load_offset);
  Instr instr_load = assm_->instr_at(load_offset + 4);
  // Instruction to patch must be 'ld/lw rd, offset(rd)' with 'offset == 0'.
  DCHECK(assm_->IsAuipc(instr_auipc));
  DCHECK(assm_->IsLoadWord(instr_load));
  DCHECK_EQ(assm_->LoadOffset(instr_load), 1);
  DCHECK_EQ(assm_->AuipcOffset(instr_auipc), 0);
  int32_t distance = static_cast<int32_t>(
      reinterpret_cast<Address>(entry_offset) -
      reinterpret_cast<Address>(assm_->toAddress(load_offset)));
  CHECK(is_int32(distance + 0x800));
  int32_t Hi20 = (static_cast<int32_t>(distance) + 0x800) >> 12;
  int32_t Lo12 = static_cast<int32_t>(distance) << 20 >> 20;
  assm_->instr_at_put(load_offset, SetHi20Offset(Hi20, instr_auipc));
  assm_->instr_at_put(load_offset + 4, SetLo12Offset(Lo12, instr_load));
}

void ConstantPool::Check(Emission force_emit, Jump require_jump,
                         size_t margin) {
  // Some short sequence of instruction must not be broken up by constant pool
  // emission, such sequences are protected by a ConstPool::BlockScope.
  if (IsBlocked() || assm_->is_trampoline_pool_blocked()) {
    // Something is wrong if emission is forced and blocked at the same time.
    DCHECK_EQ(force_emit, Emission::kIfNeeded);
    return;
  }

  // We emit a constant pool only if :
  //  * it is not empty
  //  * emission is forced by parameter force_emit (e.g. at function end).
  //  * emission is mandatory or opportune according to {ShouldEmitNow}.
  if (!IsEmpty() && (force_emit == Emission::kForced ||
                     ShouldEmitNow(require_jump, margin))) {
    // Check that the code buffer is large enough before emitting the constant
    // pool (this includes the gap to the relocation information).
    int worst_case_size = ComputeSize(Jump::kRequired, Alignment::kRequired);
    int needed_space = worst_case_size + assm_->kGap;
    while (assm_->buffer_space() <= needed_space) {
      assm_->GrowBuffer();
    }

    // Since we do not know how much space the constant pool is going to take
    // up, we cannot handle getting here while the trampoline pool is blocked.
    CHECK(!assm_->is_trampoline_pool_blocked());
    EmitAndClear(require_jump);
  }
  // Since a constant pool is (now) empty, move the check offset forward by
  // the standard interval.
  SetNextCheckIn(ConstantPool::kCheckInterval);
}

}  // namespace internal
}  // namespace v8
