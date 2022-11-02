;; expected = R"---(;; This is a polyglot C++/WAT file.
;; Comment lines are ignored and not expected in the disassembler output.
(module
  ;; Globals: mutable, non-mutable, imported, exported.
  (global $env.imported_global (;0;) (import "env" "imported_global") i32)
  (global $global1 (mut i32) (i32.const 0))
  (global $global2 i32 (global.get $env.imported_global))
  (global $exported_global (;3;) (export "exported_global") i64 (i64.const 0))
  ;; Instructions on globals, locals, parameters.
  (func $func0 (param $var0 i32)
    (local $var1 i64)
    local.get $var0
    local.tee $var0
    local.set $var0
    local.get $var1
    local.tee $var1
    local.set $var1
    global.get $global1
    global.set $global1
  )
  ;; i32 operations.
  (func $func1 (result i32)
    i32.const 0
    i32.eqz
    i32.const 1
    i32.eq
    i32.const -1
    i32.ne
    i32.const 0
    i32.lt_s
    i32.const 0
    i32.lt_u
    i32.const 0
    i32.gt_s
    i32.const 0
    i32.gt_u
    i32.const 0
    i32.le_s
    i32.const 0
    i32.le_u
    i32.const 0
    i32.ge_s
    i32.const 0
    i32.ge_u
    i32.clz
    i32.ctz
    i32.popcnt
    i32.const 0
    i32.add
    i32.const 0
    i32.sub
    i32.const 0
    i32.mul
    i32.const 0
    i32.div_s
    i32.const 0
    i32.div_u
    i32.const 0
    i32.rem_s
    i32.const 0
    i32.rem_u
    i32.const 0
    i32.and
    i32.const 0
    i32.or
    i32.const 0
    i32.xor
    i32.const 0
    i32.shl
    i32.const 0
    i32.shr_s
    i32.const 0
    i32.shr_u
    i32.const 0
    i32.rotl
    i32.const 0
    i32.rotr
    i32.extend8_s
    i32.extend16_s
  )
  ;; Other instructions.
  (func $func2
    nop
    unreachable
  )
  ;; TODO(dlehmann): i64, f32, f64 operations.
  ;; TODO(dlehmann): select, blocks, loops, branches.
  ;; TODO(dlehmann): calls, call_indirect, table, element section.
  ;; TODO(dlehmann): memory and data section.
  ;; TODO(dlehmann): name and extended name section.
)
;;)---";
