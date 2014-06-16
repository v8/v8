// Copyright 2014 the V8 project authors. All rights reserved.
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

#ifndef V8_AST_VALUE_FACTORY_H_
#define V8_AST_VALUE_FACTORY_H_

#include "src/api.h"
#include "src/hashmap.h"
#include "src/utils.h"

// AstString, AstValue and AstValueFactory are for storing strings and values
// independent of the V8 heap and internalizing them later. During parsing,
// AstStrings and AstValues are created and stored outside the heap, in
// AstValueFactory. After parsing, the strings and values are internalized
// (moved into the V8 heap).
namespace v8 {
namespace internal {

class AstString {
 public:
  AstString(bool i, Vector<const byte> lb, int h)
      : is_one_byte_(i),
        literal_bytes_(lb),
        hash_(h) {}

  AstString()
      : is_one_byte_(true),
        hash_(0) {}

  bool AsArrayIndex(uint32_t* index) const;

  // The string is not null-terminated, use length() to find out the length.
  const unsigned char* raw_data() const { return literal_bytes_.start(); }
  int length() const {
    if (is_one_byte_)
      return literal_bytes_.length();
    return literal_bytes_.length() / 2;
  }
  bool is_one_byte() const { return is_one_byte_; }
  bool IsEmpty() const { return literal_bytes_.length() == 0; }
  bool IsOneByteEqualTo(const char* data) const;
  uint16_t FirstCharacter() const {
    if (is_one_byte_)
      return literal_bytes_[0];
    const uint16_t* c =
        reinterpret_cast<const uint16_t*>(literal_bytes_.start());
    return *c;
  }

  // Puts the string into the V8 heap.
  void Internalize(Isolate* isolate);

  // This function can be called after internalizing.
  V8_INLINE Handle<String> string() const {
    ASSERT(!string_.is_null());
    return string_;
  }

  // For storing AstStrings in a hash map.
  int hash() const { return hash_; }
  static bool Compare(void* a, void* b);

 private:
  friend class AstValueFactory;

  bool is_one_byte_;
  // Weak. Points to memory owned by AstValueFactory.
  Vector<const byte> literal_bytes_;
  int hash_;

  // This is null until the string is internalized.
  Handle<String> string_;
};


// AstValue is either a string, a number, a string array, a boolean, or a
// special value (null, undefined, the hole).
class AstValue : public ZoneObject {
 public:
  bool IsString() const {
    return type_ == STRING;
  }

  bool IsNumber() const {
    return type_ == NUMBER || type_ == SMI;
  }

  const AstString* AsString() const {
    if (type_ == STRING)
      return string_;
    UNREACHABLE();
    return 0;
  }

  double AsNumber() const {
    if (type_ == NUMBER)
      return number_;
    if (type_ == SMI)
      return smi_;
    UNREACHABLE();
    return 0;
  }

  bool EqualsString(const AstString* string) const {
    return type_ == STRING && string_ == string;
  }

  bool IsPropertyName() const;

  bool BooleanValue() const;

  void Internalize(Isolate* isolate);

  // Can be called after Internalize has been called.
  V8_INLINE Handle<Object> value() const {
    if (type_ == STRING) {
      return string_->string();
    }
    ASSERT(!value_.is_null());
    return value_;
  }

 private:
  friend class AstValueFactory;

  enum Type {
    STRING,
    SYMBOL,
    NUMBER,
    SMI,
    BOOLEAN,
    STRING_ARRAY,
    NULL_TYPE,
    UNDEFINED,
    THE_HOLE
  };

  explicit AstValue(const AstString* s) : type_(STRING) { string_ = s; }

  explicit AstValue(const char* name) : type_(SYMBOL) { symbol_name_ = name; }

  explicit AstValue(double n) : type_(NUMBER) { number_ = n; }

  AstValue(Type t, int i) : type_(t) {
    ASSERT(type_ == SMI);
    smi_ = i;
  }

  explicit AstValue(bool b) : type_(BOOLEAN) { bool_ = b; }

  explicit AstValue(ZoneList<const AstString*>* s) : type_(STRING_ARRAY) {
    strings_ = s;
  }

  explicit AstValue(Type t) : type_(t) {
    ASSERT(t == NULL_TYPE || t == UNDEFINED || t == THE_HOLE);
  }

  Type type_;

  // Uninternalized value.
  union {
    const AstString* string_;
    double number_;
    int smi_;
    bool bool_;
    ZoneList<const AstString*>* strings_;
    const char* symbol_name_;
  };

  // Internalized value (empty before internalized).
  Handle<Object> value_;
};


// For generating string constants.
#define STRING_CONSTANTS(F) \
  F(anonymous_function, "(anonymous function)") \
  F(arguments, "arguments") \
  F(done, "done") \
  F(dot_for, ".for") \
  F(dot_generator, ".generator") \
  F(dot_generator_object, ".generator_object") \
  F(dot_iterable, ".iterable") \
  F(dot_iterator, ".iterator") \
  F(dot_module, ".module") \
  F(dot_result, ".result") \
  F(empty, "") \
  F(eval, "eval") \
  F(initialize_const_global, "initializeConstGlobal") \
  F(initialize_var_global, "initializeVarGlobal") \
  F(make_reference_error, "MakeReferenceError") \
  F(make_syntax_error, "MakeSyntaxError") \
  F(make_type_error, "MakeTypeError") \
  F(module, "module") \
  F(native, "native") \
  F(next, "next") \
  F(proto, "__proto__") \
  F(prototype, "prototype") \
  F(this, "this") \
  F(use_strict, "use strict") \
  F(value, "value")

class AstValueFactory {
 public:
  explicit AstValueFactory(Zone* zone)
      : literal_chars_(0),
        string_table_keys_(0),
        string_table_(AstString::Compare),
        zone_(zone),
        isolate_(NULL) {
#define F(name, str) { \
      const char* data = str; \
      name##_string_ = GetOneByteString( \
          Vector<const uint8_t>(reinterpret_cast<const uint8_t*>(data), \
                                static_cast<int>(strlen(data)))); \
    }
    STRING_CONSTANTS(F)
#undef F
  }

  const AstString* GetOneByteString(Vector<const uint8_t> literal);
  const AstString* GetTwoByteString(Vector<const uint16_t> literal);
  const AstString* GetString(Handle<String> literal);

  void Internalize(Isolate* isolate);

#define F(name, str) \
  const AstString* name##_string() const { return name##_string_; }
  STRING_CONSTANTS(F)
#undef F

  const AstValue* NewString(const AstString* string);
  // A JavaScript symbol (ECMA-262 edition 6).
  const AstValue* NewSymbol(const char* name);
  const AstValue* NewNumber(double number);
  const AstValue* NewSmi(int number);
  const AstValue* NewBoolean(bool b);
  const AstValue* NewStringList(ZoneList<const AstString*>* strings);
  const AstValue* NewNull();
  const AstValue* NewUndefined();
  const AstValue* NewTheHole();

 private:
  const AstString* GetString(int hash, bool is_one_byte,
                             Vector<const byte> literal_bytes);

  // All strings are copied here, one after another (no NULLs inbetween).
  Collector<byte> literal_chars_;
  // List of all AstStrings we have created; keys of string_table_ are pointers
  // into AstStrings in string_table_keys_.
  Collector<AstString> string_table_keys_;
  HashMap string_table_;
  // For keeping track of all AstValues we've created (so that they can be
  // internalized later).
  List<AstValue*> values_;
  Zone* zone_;
  Isolate* isolate_;

#define F(name, str) \
  const AstString* name##_string_;
  STRING_CONSTANTS(F)
#undef F
};

} }  // namespace v8::internal

#undef STRING_CONSTANTS

#endif  // V8_AST_VALUE_FACTORY_H_
