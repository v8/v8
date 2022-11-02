  0x00, 0x61, 0x73, 0x6d,  // wasm magic
  0x01, 0x00, 0x00, 0x00,  // wasm version

  0x01,                    // section kind: Type
  0x0c,                    // section length 12
  0x03,                    // types count 3
  0x60,                    //  kind: func
  0x01, 0x7f,              // param count 1:  i32
  0x00,                    // return count 0
  0x60,                    //  kind: func
  0x00,                    // param count 0
  0x01, 0x7f,              // return count 1:  i32
  0x60,                    //  kind: func
  0x00,                    // param count 0
  0x00,                    // return count 0

  0x02,                    // section kind: Import
  0x18,                    // section length 24
  0x01,                    // imports count 1: import #0
  0x03,                    // module name length: 3
  0x65, 0x6e, 0x76,        // module name: env
  0x0f,                    // field name length: 15
  0x69, 0x6d, 0x70, 0x6f, 0x72, 0x74, 0x65, 0x64, 
  0x5f, 0x67, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 
                           // field name: imported_global
  0x03, 0x7f, 0x00,        // kind: global i32 immutable

  0x03,                    // section kind: Function
  0x04,                    // section length 4
  0x03,                    // functions count 3
  0x00,                    // 0 $func0 (param i32)
  0x01,                    // 1 $func1 (result i32)
  0x02,                    // 2 $func2

  0x06,                    // section kind: Global
  0x10,                    // section length 16
  0x03,                    // globals count 3
  0x7f, 0x01,              // global #1: i32 mutable
  0x41, 0x00, 0x0b,        // i32.const 0
  0x7f, 0x00,              // global #2: i32 immutable
  0x23, 0x00, 0x0b,        // global.get $env.imported_global
  0x7e, 0x00,              // global #3: i64 immutable
  0x42, 0x00, 0x0b,        // i64.const 0

  0x07,                    // section kind: Export
  0x13,                    // section length 19
  0x01,                    // exports count 1: export # 0
  0x0f,                    // field name length: 15
  0x65, 0x78, 0x70, 0x6f, 0x72, 0x74, 0x65, 0x64, 
  0x5f, 0x67, 0x6c, 0x6f, 0x62, 0x61, 0x6c, 
                           // field name: exported_global
  0x03, 0x03,              // kind: global index: 3

  0x0a,                    // section kind: Code
  0x71,                    // section length 113
  0x03,                    // functions count 3
                           // function #0 $func0
  0x14,                    // body size 20
  0x01,                    // 1 entries in locals list
  0x01, 0x7e,              // 1 local of type i64
  0x20, 0x00,              // local.get $var0
  0x22, 0x00,              // local.tee $var0
  0x21, 0x00,              // local.set $var0
  0x20, 0x01,              // local.get $var1
  0x22, 0x01,              // local.tee $var1
  0x21, 0x01,              // local.set $var1
  0x23, 0x01,              // global.get $global1
  0x24, 0x01,              // global.set $global1
  0x0b,                    // end
                           // function #1 $func1
  0x55,                    // body size 85
  0x00,                    // 0 entries in locals list
  0x41, 0x00,              // i32.const 0
  0x45,                    // i32.eqz
  0x41, 0x01,              // i32.const 1
  0x46,                    // i32.eq
  0x41, 0x7f,              // i32.const -1
  0x47,                    // i32.ne
  0x41, 0x00,              // i32.const 0
  0x48,                    // i32.lt_s
  0x41, 0x00,              // i32.const 0
  0x49,                    // i32.lt_u
  0x41, 0x00,              // i32.const 0
  0x4a,                    // i32.gt_s
  0x41, 0x00,              // i32.const 0
  0x4b,                    // i32.gt_u
  0x41, 0x00,              // i32.const 0
  0x4c,                    // i32.le_s
  0x41, 0x00,              // i32.const 0
  0x4d,                    // i32.le_u
  0x41, 0x00,              // i32.const 0
  0x4e,                    // i32.ge_s
  0x41, 0x00,              // i32.const 0
  0x4f,                    // i32.ge_u
  0x67,                    // i32.clz
  0x68,                    // i32.ctz
  0x69,                    // i32.popcnt
  0x41, 0x00,              // i32.const 0
  0x6a,                    // i32.add
  0x41, 0x00,              // i32.const 0
  0x6b,                    // i32.sub
  0x41, 0x00,              // i32.const 0
  0x6c,                    // i32.mul
  0x41, 0x00,              // i32.const 0
  0x6d,                    // i32.div_s
  0x41, 0x00,              // i32.const 0
  0x6e,                    // i32.div_u
  0x41, 0x00,              // i32.const 0
  0x6f,                    // i32.rem_s
  0x41, 0x00,              // i32.const 0
  0x70,                    // i32.rem_u
  0x41, 0x00,              // i32.const 0
  0x71,                    // i32.and
  0x41, 0x00,              // i32.const 0
  0x72,                    // i32.or
  0x41, 0x00,              // i32.const 0
  0x73,                    // i32.xor
  0x41, 0x00,              // i32.const 0
  0x74,                    // i32.shl
  0x41, 0x00,              // i32.const 0
  0x75,                    // i32.shr_s
  0x41, 0x00,              // i32.const 0
  0x76,                    // i32.shr_u
  0x41, 0x00,              // i32.const 0
  0x77,                    // i32.rotl
  0x41, 0x00,              // i32.const 0
  0x78,                    // i32.rotr
  0xc0,                    // i32.extend8_s
  0xc1,                    // i32.extend16_s
  0x0b,                    // end
                           // function #2 $func2
  0x04,                    // body size 4
  0x00,                    // 0 entries in locals list
  0x01,                    // nop
  0x00,                    // unreachable
  0x0b,                    // end
