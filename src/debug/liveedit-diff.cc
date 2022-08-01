// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/liveedit-diff.h"

#include <map>

#include "src/base/logging.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

namespace {

// A simple implementation of dynamic programming algorithm. It solves
// the problem of finding the difference of 2 arrays. It uses a table of results
// of subproblems. Each cell contains a number together with 2-bit flag
// that helps building the chunk list.
class Differencer {
 public:
  explicit Differencer(Comparator::Input* input)
      : input_(input), len1_(input->GetLength1()), len2_(input->GetLength2()) {}

  void Initialize() {}

  // Makes sure that result for the full problem is calculated and stored
  // in the table together with flags showing a path through subproblems.
  void FillTable() {
    // Determine common prefix to skip.
    int minLen = std::min(len1_, len2_);
    while (prefixLen_ < minLen && input_->Equals(prefixLen_, prefixLen_)) {
      ++prefixLen_;
    }

    // Pre-fill common suffix in the table.
    for (int pos1 = len1_, pos2 = len2_; pos1 > prefixLen_ &&
                                         pos2 > prefixLen_ &&
                                         input_->Equals(--pos1, --pos2);) {
      set_value4_and_dir(pos1, pos2, 0, EQ);
    }

    CompareUpToTail(prefixLen_, prefixLen_);
  }

  void SaveResult(Comparator::Output* chunk_writer) {
    ResultWriter writer(chunk_writer);

    if (prefixLen_) writer.eq(prefixLen_);
    for (int pos1 = prefixLen_, pos2 = prefixLen_; true;) {
      if (pos1 < len1_) {
        if (pos2 < len2_) {
          Direction dir = get_direction(pos1, pos2);
          switch (dir) {
            case EQ:
              writer.eq();
              pos1++;
              pos2++;
              break;
            case SKIP1:
              writer.skip1(1);
              pos1++;
              break;
            case SKIP2:
            case SKIP_ANY:
              writer.skip2(1);
              pos2++;
              break;
            default:
              UNREACHABLE();
          }
        } else {
          writer.skip1(len1_ - pos1);
          break;
        }
      } else {
        if (len2_ != pos2) {
          writer.skip2(len2_ - pos2);
        }
        break;
      }
    }
    writer.close();
  }

 private:
  Comparator::Input* input_;
  std::map<std::pair<int, int>, int> buffer_;
  int len1_;
  int len2_;
  int prefixLen_ = 0;

  enum Direction {
    EQ = 0,
    SKIP1,
    SKIP2,
    SKIP_ANY,

    MAX_DIRECTION_FLAG_VALUE = SKIP_ANY
  };

  // Computes result for a subtask and optionally caches it in the buffer table.
  // All results values are shifted to make space for flags in the lower bits.
  int CompareUpToTail(int pos1, int pos2) {
    if (pos1 == len1_) {
      return (len2_ - pos2) << kDirectionSizeBits;
    }
    if (pos2 == len2_) {
      return (len1_ - pos1) << kDirectionSizeBits;
    }
    int res = get_value4(pos1, pos2);
    if (res != kEmptyCellValue) {
      return res;
    }
    Direction dir;
    if (input_->Equals(pos1, pos2)) {
      res = CompareUpToTail(pos1 + 1, pos2 + 1);
      dir = EQ;
    } else {
      int res1 = CompareUpToTail(pos1 + 1, pos2) + (1 << kDirectionSizeBits);
      int res2 = CompareUpToTail(pos1, pos2 + 1) + (1 << kDirectionSizeBits);
      if (res1 == res2) {
        res = res1;
        dir = SKIP_ANY;
      } else if (res1 < res2) {
        res = res1;
        dir = SKIP1;
      } else {
        res = res2;
        dir = SKIP2;
      }
    }
    set_value4_and_dir(pos1, pos2, res, dir);
    return res;
  }

  inline int get_cell(int i1, int i2) {
    auto it = buffer_.find(std::make_pair(i1, i2));
    return it == buffer_.end() ? kEmptyCellValue : it->second;
  }

  inline void set_cell(int i1, int i2, int value) {
    buffer_.insert(std::make_pair(std::make_pair(i1, i2), value));
  }

  // Each cell keeps a value plus direction. Value is multiplied by 4.
  void set_value4_and_dir(int i1, int i2, int value4, Direction dir) {
    DCHECK_EQ(0, value4 & kDirectionMask);
    set_cell(i1, i2, value4 | dir);
  }

  int get_value4(int i1, int i2) {
    return get_cell(i1, i2) & (kMaxUInt32 ^ kDirectionMask);
  }
  Direction get_direction(int i1, int i2) {
    return static_cast<Direction>(get_cell(i1, i2) & kDirectionMask);
  }

  static const int kDirectionSizeBits = 2;
  static const int kDirectionMask = (1 << kDirectionSizeBits) - 1;
  static const int kEmptyCellValue = ~0u << kDirectionSizeBits;

  // This method only holds static assert statement (unfortunately you cannot
  // place one in class scope).
  void StaticAssertHolder() {
    static_assert(MAX_DIRECTION_FLAG_VALUE < (1 << kDirectionSizeBits));
  }

  class ResultWriter {
   public:
    explicit ResultWriter(Comparator::Output* chunk_writer)
        : chunk_writer_(chunk_writer),
          pos1_(0),
          pos2_(0),
          pos1_begin_(-1),
          pos2_begin_(-1),
          has_open_chunk_(false) {}
    void eq(int len = 1) {
      FlushChunk();
      pos1_ += len;
      pos2_ += len;
    }
    void skip1(int len1) {
      StartChunk();
      pos1_ += len1;
    }
    void skip2(int len2) {
      StartChunk();
      pos2_ += len2;
    }
    void close() { FlushChunk(); }

   private:
    Comparator::Output* chunk_writer_;
    int pos1_;
    int pos2_;
    int pos1_begin_;
    int pos2_begin_;
    bool has_open_chunk_;

    void StartChunk() {
      if (!has_open_chunk_) {
        pos1_begin_ = pos1_;
        pos2_begin_ = pos2_;
        has_open_chunk_ = true;
      }
    }

    void FlushChunk() {
      if (has_open_chunk_) {
        chunk_writer_->AddChunk(pos1_begin_, pos2_begin_, pos1_ - pos1_begin_,
                                pos2_ - pos2_begin_);
        has_open_chunk_ = false;
      }
    }
  };
};

}  // namespace

void Comparator::CalculateDifference(Comparator::Input* input,
                                     Comparator::Output* result_writer) {
  Differencer differencer(input);
  differencer.Initialize();
  differencer.FillTable();
  differencer.SaveResult(result_writer);
}

}  // namespace internal
}  // namespace v8
