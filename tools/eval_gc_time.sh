#!/bin/bash
#
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Convenience Script used to rank GC NVP output.

print_usage_and_die() {
  echo "Usage: $0 new-gen-rank|old-gen-rank max|avg logfile"
  exit 1
}

if [ $# -ne 3 ]; then
  print_usage_and_die
fi

case $1 in
  new-gen-rank|old-gen-rank)
    OP=$1
    ;;
  *)
    print_usage_and_die
esac

case $2 in 
  max|avg)
    RANK_MODE=$2
    ;;
  *)
    print_usage_and_die
esac

LOGFILE=$3

GENERAL_INTERESTING_KEYS="\
  pause \
"

INTERESTING_NEW_GEN_KEYS="\
  ${GENERAL_INTERESTING_KEYS} \
  scavenge \
  weak \
  roots \
  old_new \
  code \
  semispace \
  object_groups \
"

INTERESTING_OLD_GEN_KEYS="\
  ${GENERAL_INTERESTING_KEYS} \
  external \
  mark \
  mark_inc \
  mark_prepcodeflush \
  mark_root \
  mark_topopt \
  mark_retainmaps \
  mark_weakclosure \
  mark_stringtable \
  mark_weakrefs \
  mark_globalhandles \
  mark_codeflush \
  mark_optimizedcodemaps \
  store_buffer_clear \
  slots_buffer_clear \
  sweep \
  sweepns \
  sweepos \
  sweepcode \
  sweepcell \
  sweepmap \
  sweepaborted \
  evacuate \
  new_new \
  old_new \
  root_new \
  compaction_ptrs \
  intracompaction_ptrs \
  misc_compaction \
  inc_weak_closure \
  weakcollection_process \
  weakcollection_clear \
  weakcollection_abort \
  weakcells \
  nonlive_refs \
"

BASE_DIR=$(dirname $0)

case $OP in
  new-gen-rank)
    cat $LOGFILE | grep "gc=s" \
      | $BASE_DIR/eval_gc_nvp.py \
      --no-histogram \
      --rank $RANK_MODE \
      ${INTERESTING_NEW_GEN_KEYS}
    ;;
  old-gen-rank)
    cat $LOGFILE | grep "gc=ms" | grep "reduce_memory=0" | grep -v "steps=0" \
      | $BASE_DIR/eval_gc_nvp.py \
      --no-histogram \
      --rank $RANK_MODE \
      ${INTERESTING_OLD_GEN_KEYS}
    ;;
  *)
    ;;
esac

