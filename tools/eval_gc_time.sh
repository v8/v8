#!/bin/bash
#
# Copyright 2015 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# Convenience Script used to rank GC NVP output.

print_usage_and_die() {
  echo "Usage: $0 RANK SORT [LOGFILE]"
  echo ""
  echo "Arguments:"
  echo "  RANK: old-gen-rank | new-gen-rank"
  echo "  SORT: max | avg"
  echo "  LOGFILE: the file to process. will default to /dev/stdin"
  exit 1
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
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

if [ $# -eq 3 ]; then
  LOGFILE=$3
else
  LOGFILE=/dev/stdin
fi

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
  clear \
  clear.code_flush \
  clear.dependent_code \
  clear.global_handles \
  clear.maps \
  clear.slots_buffer \
  clear.store_buffer \
  clear.string_table \
  clear.weak_cells \
  clear.weak_collections \
  clear.weak_lists \
  finish \
  evacuate \
  evacuate.candidates \
  evacuate.clean_up \
  evacuate.copy \
  evacuate.update_pointers \
  evacuate.update_pointers.between_evacuated \
  evacuate.update_pointers.to_evacuated \
  evacuate.update_pointers.to_new \
  evacuate.update_pointers.weak \
  external.mc_prologue \
  external.mc_epilogue \
  external.mc_incremental_prologue \
  external.mc_incremental_epilogue \
  external.weak_global_handles \
  mark \
  mark.finish_incremental \
  mark.prepare_code_flush \
  mark.roots \
  mark.weak_closure \
  mark.weak_closure.ephemeral \
  mark.weak_closure.weak_handles \
  mark.weak_closure.weak_roots \
  mark.weak_closure.harmony \
  sweep \
  sweep.code \
  sweep.map \
  sweep.old \
  incremental_finalize \
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
    cat $LOGFILE | grep "gc=ms" \
      | $BASE_DIR/eval_gc_nvp.py \
      --no-histogram \
      --rank $RANK_MODE \
      ${INTERESTING_OLD_GEN_KEYS}
    ;;
  *)
    ;;
esac

