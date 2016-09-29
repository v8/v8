// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/code-factory.h"

namespace v8 {
namespace internal {

namespace {

enum ResultMode { kDontNegateResult, kNegateResult };

void GenerateStringEqual(CodeStubAssembler* assembler, ResultMode mode) {
  // Here's pseudo-code for the algorithm below in case of kDontNegateResult
  // mode; for kNegateResult mode we properly negate the result.
  //
  // if (lhs == rhs) return true;
  // if (lhs->length() != rhs->length()) return false;
  // if (lhs->IsInternalizedString() && rhs->IsInternalizedString()) {
  //   return false;
  // }
  // if (lhs->IsSeqOneByteString() && rhs->IsSeqOneByteString()) {
  //   for (i = 0; i != lhs->length(); ++i) {
  //     if (lhs[i] != rhs[i]) return false;
  //   }
  //   return true;
  // }
  // return %StringEqual(lhs, rhs);

  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* lhs = assembler->Parameter(0);
  Node* rhs = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);

  Label if_equal(assembler), if_notequal(assembler);

  // Fast check to see if {lhs} and {rhs} refer to the same String object.
  Label if_same(assembler), if_notsame(assembler);
  assembler->Branch(assembler->WordEqual(lhs, rhs), &if_same, &if_notsame);

  assembler->Bind(&if_same);
  assembler->Goto(&if_equal);

  assembler->Bind(&if_notsame);
  {
    // The {lhs} and {rhs} don't refer to the exact same String object.

    // Load the length of {lhs} and {rhs}.
    Node* lhs_length = assembler->LoadStringLength(lhs);
    Node* rhs_length = assembler->LoadStringLength(rhs);

    // Check if the lengths of {lhs} and {rhs} are equal.
    Label if_lengthisequal(assembler), if_lengthisnotequal(assembler);
    assembler->Branch(assembler->WordEqual(lhs_length, rhs_length),
                      &if_lengthisequal, &if_lengthisnotequal);

    assembler->Bind(&if_lengthisequal);
    {
      // Load instance types of {lhs} and {rhs}.
      Node* lhs_instance_type = assembler->LoadInstanceType(lhs);
      Node* rhs_instance_type = assembler->LoadInstanceType(rhs);

      // Combine the instance types into a single 16-bit value, so we can check
      // both of them at once.
      Node* both_instance_types = assembler->Word32Or(
          lhs_instance_type,
          assembler->Word32Shl(rhs_instance_type, assembler->Int32Constant(8)));

      // Check if both {lhs} and {rhs} are internalized.
      int const kBothInternalizedMask =
          kIsNotInternalizedMask | (kIsNotInternalizedMask << 8);
      int const kBothInternalizedTag =
          kInternalizedTag | (kInternalizedTag << 8);
      Label if_bothinternalized(assembler), if_notbothinternalized(assembler);
      assembler->Branch(assembler->Word32Equal(
                            assembler->Word32And(both_instance_types,
                                                 assembler->Int32Constant(
                                                     kBothInternalizedMask)),
                            assembler->Int32Constant(kBothInternalizedTag)),
                        &if_bothinternalized, &if_notbothinternalized);

      assembler->Bind(&if_bothinternalized);
      {
        // Fast negative check for internalized-to-internalized equality.
        assembler->Goto(&if_notequal);
      }

      assembler->Bind(&if_notbothinternalized);
      {
        // Check that both {lhs} and {rhs} are flat one-byte strings.
        int const kBothSeqOneByteStringMask =
            kStringEncodingMask | kStringRepresentationMask |
            ((kStringEncodingMask | kStringRepresentationMask) << 8);
        int const kBothSeqOneByteStringTag =
            kOneByteStringTag | kSeqStringTag |
            ((kOneByteStringTag | kSeqStringTag) << 8);
        Label if_bothonebyteseqstrings(assembler),
            if_notbothonebyteseqstrings(assembler);
        assembler->Branch(
            assembler->Word32Equal(
                assembler->Word32And(
                    both_instance_types,
                    assembler->Int32Constant(kBothSeqOneByteStringMask)),
                assembler->Int32Constant(kBothSeqOneByteStringTag)),
            &if_bothonebyteseqstrings, &if_notbothonebyteseqstrings);

        assembler->Bind(&if_bothonebyteseqstrings);
        {
          // Compute the effective offset of the first character.
          Node* begin = assembler->IntPtrConstant(
              SeqOneByteString::kHeaderSize - kHeapObjectTag);

          // Compute the first offset after the string from the length.
          Node* end =
              assembler->IntPtrAdd(begin, assembler->SmiUntag(lhs_length));

          // Loop over the {lhs} and {rhs} strings to see if they are equal.
          Variable var_offset(assembler, MachineType::PointerRepresentation());
          Label loop(assembler, &var_offset);
          var_offset.Bind(begin);
          assembler->Goto(&loop);
          assembler->Bind(&loop);
          {
            // Check if {offset} equals {end}.
            Node* offset = var_offset.value();
            Label if_done(assembler), if_notdone(assembler);
            assembler->Branch(assembler->WordEqual(offset, end), &if_done,
                              &if_notdone);

            assembler->Bind(&if_notdone);
            {
              // Load the next characters from {lhs} and {rhs}.
              Node* lhs_value =
                  assembler->Load(MachineType::Uint8(), lhs, offset);
              Node* rhs_value =
                  assembler->Load(MachineType::Uint8(), rhs, offset);

              // Check if the characters match.
              Label if_valueissame(assembler), if_valueisnotsame(assembler);
              assembler->Branch(assembler->Word32Equal(lhs_value, rhs_value),
                                &if_valueissame, &if_valueisnotsame);

              assembler->Bind(&if_valueissame);
              {
                // Advance to next character.
                var_offset.Bind(
                    assembler->IntPtrAdd(offset, assembler->IntPtrConstant(1)));
              }
              assembler->Goto(&loop);

              assembler->Bind(&if_valueisnotsame);
              assembler->Goto(&if_notequal);
            }

            assembler->Bind(&if_done);
            assembler->Goto(&if_equal);
          }
        }

        assembler->Bind(&if_notbothonebyteseqstrings);
        {
          // TODO(bmeurer): Add fast case support for flattened cons strings;
          // also add support for two byte string equality checks.
          Runtime::FunctionId function_id = (mode == kDontNegateResult)
                                                ? Runtime::kStringEqual
                                                : Runtime::kStringNotEqual;
          assembler->TailCallRuntime(function_id, context, lhs, rhs);
        }
      }
    }

    assembler->Bind(&if_lengthisnotequal);
    {
      // Mismatch in length of {lhs} and {rhs}, cannot be equal.
      assembler->Goto(&if_notequal);
    }
  }

  assembler->Bind(&if_equal);
  assembler->Return(assembler->BooleanConstant(mode == kDontNegateResult));

  assembler->Bind(&if_notequal);
  assembler->Return(assembler->BooleanConstant(mode == kNegateResult));
}

enum RelationalComparisonMode {
  kLessThan,
  kLessThanOrEqual,
  kGreaterThan,
  kGreaterThanOrEqual
};

void GenerateStringRelationalComparison(CodeStubAssembler* assembler,
                                        RelationalComparisonMode mode) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* lhs = assembler->Parameter(0);
  Node* rhs = assembler->Parameter(1);
  Node* context = assembler->Parameter(2);

  Label if_less(assembler), if_equal(assembler), if_greater(assembler);

  // Fast check to see if {lhs} and {rhs} refer to the same String object.
  Label if_same(assembler), if_notsame(assembler);
  assembler->Branch(assembler->WordEqual(lhs, rhs), &if_same, &if_notsame);

  assembler->Bind(&if_same);
  assembler->Goto(&if_equal);

  assembler->Bind(&if_notsame);
  {
    // Load instance types of {lhs} and {rhs}.
    Node* lhs_instance_type = assembler->LoadInstanceType(lhs);
    Node* rhs_instance_type = assembler->LoadInstanceType(rhs);

    // Combine the instance types into a single 16-bit value, so we can check
    // both of them at once.
    Node* both_instance_types = assembler->Word32Or(
        lhs_instance_type,
        assembler->Word32Shl(rhs_instance_type, assembler->Int32Constant(8)));

    // Check that both {lhs} and {rhs} are flat one-byte strings.
    int const kBothSeqOneByteStringMask =
        kStringEncodingMask | kStringRepresentationMask |
        ((kStringEncodingMask | kStringRepresentationMask) << 8);
    int const kBothSeqOneByteStringTag =
        kOneByteStringTag | kSeqStringTag |
        ((kOneByteStringTag | kSeqStringTag) << 8);
    Label if_bothonebyteseqstrings(assembler),
        if_notbothonebyteseqstrings(assembler);
    assembler->Branch(assembler->Word32Equal(
                          assembler->Word32And(both_instance_types,
                                               assembler->Int32Constant(
                                                   kBothSeqOneByteStringMask)),
                          assembler->Int32Constant(kBothSeqOneByteStringTag)),
                      &if_bothonebyteseqstrings, &if_notbothonebyteseqstrings);

    assembler->Bind(&if_bothonebyteseqstrings);
    {
      // Load the length of {lhs} and {rhs}.
      Node* lhs_length = assembler->LoadStringLength(lhs);
      Node* rhs_length = assembler->LoadStringLength(rhs);

      // Determine the minimum length.
      Node* length = assembler->SmiMin(lhs_length, rhs_length);

      // Compute the effective offset of the first character.
      Node* begin = assembler->IntPtrConstant(SeqOneByteString::kHeaderSize -
                                              kHeapObjectTag);

      // Compute the first offset after the string from the length.
      Node* end = assembler->IntPtrAdd(begin, assembler->SmiUntag(length));

      // Loop over the {lhs} and {rhs} strings to see if they are equal.
      Variable var_offset(assembler, MachineType::PointerRepresentation());
      Label loop(assembler, &var_offset);
      var_offset.Bind(begin);
      assembler->Goto(&loop);
      assembler->Bind(&loop);
      {
        // Check if {offset} equals {end}.
        Node* offset = var_offset.value();
        Label if_done(assembler), if_notdone(assembler);
        assembler->Branch(assembler->WordEqual(offset, end), &if_done,
                          &if_notdone);

        assembler->Bind(&if_notdone);
        {
          // Load the next characters from {lhs} and {rhs}.
          Node* lhs_value = assembler->Load(MachineType::Uint8(), lhs, offset);
          Node* rhs_value = assembler->Load(MachineType::Uint8(), rhs, offset);

          // Check if the characters match.
          Label if_valueissame(assembler), if_valueisnotsame(assembler);
          assembler->Branch(assembler->Word32Equal(lhs_value, rhs_value),
                            &if_valueissame, &if_valueisnotsame);

          assembler->Bind(&if_valueissame);
          {
            // Advance to next character.
            var_offset.Bind(
                assembler->IntPtrAdd(offset, assembler->IntPtrConstant(1)));
          }
          assembler->Goto(&loop);

          assembler->Bind(&if_valueisnotsame);
          assembler->BranchIf(assembler->Uint32LessThan(lhs_value, rhs_value),
                              &if_less, &if_greater);
        }

        assembler->Bind(&if_done);
        {
          // All characters up to the min length are equal, decide based on
          // string length.
          Label if_lengthisequal(assembler), if_lengthisnotequal(assembler);
          assembler->Branch(assembler->SmiEqual(lhs_length, rhs_length),
                            &if_lengthisequal, &if_lengthisnotequal);

          assembler->Bind(&if_lengthisequal);
          assembler->Goto(&if_equal);

          assembler->Bind(&if_lengthisnotequal);
          assembler->BranchIfSmiLessThan(lhs_length, rhs_length, &if_less,
                                         &if_greater);
        }
      }
    }

    assembler->Bind(&if_notbothonebyteseqstrings);
    {
      // TODO(bmeurer): Add fast case support for flattened cons strings;
      // also add support for two byte string relational comparisons.
      switch (mode) {
        case kLessThan:
          assembler->TailCallRuntime(Runtime::kStringLessThan, context, lhs,
                                     rhs);
          break;
        case kLessThanOrEqual:
          assembler->TailCallRuntime(Runtime::kStringLessThanOrEqual, context,
                                     lhs, rhs);
          break;
        case kGreaterThan:
          assembler->TailCallRuntime(Runtime::kStringGreaterThan, context, lhs,
                                     rhs);
          break;
        case kGreaterThanOrEqual:
          assembler->TailCallRuntime(Runtime::kStringGreaterThanOrEqual,
                                     context, lhs, rhs);
          break;
      }
    }
  }

  assembler->Bind(&if_less);
  switch (mode) {
    case kLessThan:
    case kLessThanOrEqual:
      assembler->Return(assembler->BooleanConstant(true));
      break;

    case kGreaterThan:
    case kGreaterThanOrEqual:
      assembler->Return(assembler->BooleanConstant(false));
      break;
  }

  assembler->Bind(&if_equal);
  switch (mode) {
    case kLessThan:
    case kGreaterThan:
      assembler->Return(assembler->BooleanConstant(false));
      break;

    case kLessThanOrEqual:
    case kGreaterThanOrEqual:
      assembler->Return(assembler->BooleanConstant(true));
      break;
  }

  assembler->Bind(&if_greater);
  switch (mode) {
    case kLessThan:
    case kLessThanOrEqual:
      assembler->Return(assembler->BooleanConstant(false));
      break;

    case kGreaterThan:
    case kGreaterThanOrEqual:
      assembler->Return(assembler->BooleanConstant(true));
      break;
  }
}

}  // namespace

// static
void Builtins::Generate_StringEqual(CodeStubAssembler* assembler) {
  GenerateStringEqual(assembler, kDontNegateResult);
}

// static
void Builtins::Generate_StringNotEqual(CodeStubAssembler* assembler) {
  GenerateStringEqual(assembler, kNegateResult);
}

// static
void Builtins::Generate_StringLessThan(CodeStubAssembler* assembler) {
  GenerateStringRelationalComparison(assembler, kLessThan);
}

// static
void Builtins::Generate_StringLessThanOrEqual(CodeStubAssembler* assembler) {
  GenerateStringRelationalComparison(assembler, kLessThanOrEqual);
}

// static
void Builtins::Generate_StringGreaterThan(CodeStubAssembler* assembler) {
  GenerateStringRelationalComparison(assembler, kGreaterThan);
}

// static
void Builtins::Generate_StringGreaterThanOrEqual(CodeStubAssembler* assembler) {
  GenerateStringRelationalComparison(assembler, kGreaterThanOrEqual);
}

// -----------------------------------------------------------------------------
// ES6 section 21.1 String Objects

// ES6 section 21.1.2.1 String.fromCharCode ( ...codeUnits )
void Builtins::Generate_StringFromCharCode(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Node* code = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  // Check if we have exactly one argument (plus the implicit receiver), i.e.
  // if the parent frame is not an arguments adaptor frame.
  Label if_oneargument(assembler), if_notoneargument(assembler);
  Node* parent_frame_pointer = assembler->LoadParentFramePointer();
  Node* parent_frame_type =
      assembler->Load(MachineType::Pointer(), parent_frame_pointer,
                      assembler->IntPtrConstant(
                          CommonFrameConstants::kContextOrFrameTypeOffset));
  assembler->Branch(
      assembler->WordEqual(
          parent_frame_type,
          assembler->SmiConstant(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR))),
      &if_notoneargument, &if_oneargument);

  assembler->Bind(&if_oneargument);
  {
    // Single argument case, perform fast single character string cache lookup
    // for one-byte code units, or fall back to creating a single character
    // string on the fly otherwise.
    Node* code32 = assembler->TruncateTaggedToWord32(context, code);
    Node* code16 = assembler->Word32And(
        code32, assembler->Int32Constant(String::kMaxUtf16CodeUnit));
    Node* result = assembler->StringFromCharCode(code16);
    assembler->Return(result);
  }

  assembler->Bind(&if_notoneargument);
  {
    // Determine the resulting string length.
    Node* length = assembler->LoadAndUntagSmi(
        parent_frame_pointer, ArgumentsAdaptorFrameConstants::kLengthOffset);

    // Assume that the resulting string contains only one-byte characters.
    Node* result = assembler->AllocateSeqOneByteString(context, length);

    // Truncate all input parameters and append them to the resulting string.
    Variable var_offset(assembler, MachineType::PointerRepresentation());
    Label loop(assembler, &var_offset), done_loop(assembler);
    var_offset.Bind(assembler->IntPtrConstant(0));
    assembler->Goto(&loop);
    assembler->Bind(&loop);
    {
      // Load the current {offset}.
      Node* offset = var_offset.value();

      // Check if we're done with the string.
      assembler->GotoIf(assembler->WordEqual(offset, length), &done_loop);

      // Load the next code point and truncate it to a 16-bit value.
      Node* code = assembler->Load(
          MachineType::AnyTagged(), parent_frame_pointer,
          assembler->IntPtrAdd(
              assembler->WordShl(assembler->IntPtrSub(length, offset),
                                 assembler->IntPtrConstant(kPointerSizeLog2)),
              assembler->IntPtrConstant(
                  CommonFrameConstants::kFixedFrameSizeAboveFp -
                  kPointerSize)));
      Node* code32 = assembler->TruncateTaggedToWord32(context, code);
      Node* code16 = assembler->Word32And(
          code32, assembler->Int32Constant(String::kMaxUtf16CodeUnit));

      // Check if {code16} fits into a one-byte string.
      Label if_codeisonebyte(assembler), if_codeistwobyte(assembler);
      assembler->Branch(
          assembler->Int32LessThanOrEqual(
              code16, assembler->Int32Constant(String::kMaxOneByteCharCode)),
          &if_codeisonebyte, &if_codeistwobyte);

      assembler->Bind(&if_codeisonebyte);
      {
        // The {code16} fits into the SeqOneByteString {result}.
        assembler->StoreNoWriteBarrier(
            MachineRepresentation::kWord8, result,
            assembler->IntPtrAdd(
                assembler->IntPtrConstant(SeqOneByteString::kHeaderSize -
                                          kHeapObjectTag),
                offset),
            code16);
        var_offset.Bind(
            assembler->IntPtrAdd(offset, assembler->IntPtrConstant(1)));
        assembler->Goto(&loop);
      }

      assembler->Bind(&if_codeistwobyte);
      {
        // Allocate a SeqTwoByteString to hold the resulting string.
        Node* cresult = assembler->AllocateSeqTwoByteString(context, length);

        // Copy all characters that were previously written to the
        // SeqOneByteString in {result} over to the new {cresult}.
        Variable var_coffset(assembler, MachineType::PointerRepresentation());
        Label cloop(assembler, &var_coffset), done_cloop(assembler);
        var_coffset.Bind(assembler->IntPtrConstant(0));
        assembler->Goto(&cloop);
        assembler->Bind(&cloop);
        {
          Node* coffset = var_coffset.value();
          assembler->GotoIf(assembler->WordEqual(coffset, offset), &done_cloop);
          Node* ccode = assembler->Load(
              MachineType::Uint8(), result,
              assembler->IntPtrAdd(
                  assembler->IntPtrConstant(SeqOneByteString::kHeaderSize -
                                            kHeapObjectTag),
                  coffset));
          assembler->StoreNoWriteBarrier(
              MachineRepresentation::kWord16, cresult,
              assembler->IntPtrAdd(
                  assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                            kHeapObjectTag),
                  assembler->WordShl(coffset, 1)),
              ccode);
          var_coffset.Bind(
              assembler->IntPtrAdd(coffset, assembler->IntPtrConstant(1)));
          assembler->Goto(&cloop);
        }

        // Write the pending {code16} to {offset}.
        assembler->Bind(&done_cloop);
        assembler->StoreNoWriteBarrier(
            MachineRepresentation::kWord16, cresult,
            assembler->IntPtrAdd(
                assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                          kHeapObjectTag),
                assembler->WordShl(offset, 1)),
            code16);

        // Copy the remaining parameters to the SeqTwoByteString {cresult}.
        Label floop(assembler, &var_offset), done_floop(assembler);
        assembler->Goto(&floop);
        assembler->Bind(&floop);
        {
          // Compute the next {offset}.
          Node* offset = assembler->IntPtrAdd(var_offset.value(),
                                              assembler->IntPtrConstant(1));

          // Check if we're done with the string.
          assembler->GotoIf(assembler->WordEqual(offset, length), &done_floop);

          // Load the next code point and truncate it to a 16-bit value.
          Node* code = assembler->Load(
              MachineType::AnyTagged(), parent_frame_pointer,
              assembler->IntPtrAdd(
                  assembler->WordShl(
                      assembler->IntPtrSub(length, offset),
                      assembler->IntPtrConstant(kPointerSizeLog2)),
                  assembler->IntPtrConstant(
                      CommonFrameConstants::kFixedFrameSizeAboveFp -
                      kPointerSize)));
          Node* code32 = assembler->TruncateTaggedToWord32(context, code);
          Node* code16 = assembler->Word32And(
              code32, assembler->Int32Constant(String::kMaxUtf16CodeUnit));

          // Store the truncated {code} point at the next offset.
          assembler->StoreNoWriteBarrier(
              MachineRepresentation::kWord16, cresult,
              assembler->IntPtrAdd(
                  assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                            kHeapObjectTag),
                  assembler->WordShl(offset, 1)),
              code16);
          var_offset.Bind(offset);
          assembler->Goto(&floop);
        }

        // Return the SeqTwoByteString.
        assembler->Bind(&done_floop);
        assembler->Return(cresult);
      }
    }

    assembler->Bind(&done_loop);
    assembler->Return(result);
  }
}

namespace {  // for String.fromCodePoint

bool IsValidCodePoint(Isolate* isolate, Handle<Object> value) {
  if (!value->IsNumber() && !Object::ToNumber(value).ToHandle(&value)) {
    return false;
  }

  if (Object::ToInteger(isolate, value).ToHandleChecked()->Number() !=
      value->Number()) {
    return false;
  }

  if (value->Number() < 0 || value->Number() > 0x10FFFF) {
    return false;
  }

  return true;
}

uc32 NextCodePoint(Isolate* isolate, BuiltinArguments args, int index) {
  Handle<Object> value = args.at<Object>(1 + index);
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, value, Object::ToNumber(value), -1);
  if (!IsValidCodePoint(isolate, value)) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidCodePoint, value));
    return -1;
  }
  return DoubleToUint32(value->Number());
}

}  // namespace

// ES6 section 21.1.2.2 String.fromCodePoint ( ...codePoints )
BUILTIN(StringFromCodePoint) {
  HandleScope scope(isolate);
  int const length = args.length() - 1;
  if (length == 0) return isolate->heap()->empty_string();
  DCHECK_LT(0, length);

  // Optimistically assume that the resulting String contains only one byte
  // characters.
  List<uint8_t> one_byte_buffer(length);
  uc32 code = 0;
  int index;
  for (index = 0; index < length; index++) {
    code = NextCodePoint(isolate, args, index);
    if (code < 0) {
      return isolate->heap()->exception();
    }
    if (code > String::kMaxOneByteCharCode) {
      break;
    }
    one_byte_buffer.Add(code);
  }

  if (index == length) {
    RETURN_RESULT_OR_FAILURE(isolate, isolate->factory()->NewStringFromOneByte(
                                          one_byte_buffer.ToConstVector()));
  }

  List<uc16> two_byte_buffer(length - index);

  while (true) {
    if (code <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
      two_byte_buffer.Add(code);
    } else {
      two_byte_buffer.Add(unibrow::Utf16::LeadSurrogate(code));
      two_byte_buffer.Add(unibrow::Utf16::TrailSurrogate(code));
    }

    if (++index == length) {
      break;
    }
    code = NextCodePoint(isolate, args, index);
    if (code < 0) {
      return isolate->heap()->exception();
    }
  }

  Handle<SeqTwoByteString> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      isolate->factory()->NewRawTwoByteString(one_byte_buffer.length() +
                                              two_byte_buffer.length()));

  CopyChars(result->GetChars(), one_byte_buffer.ToConstVector().start(),
            one_byte_buffer.length());
  CopyChars(result->GetChars() + one_byte_buffer.length(),
            two_byte_buffer.ToConstVector().start(), two_byte_buffer.length());

  return *result;
}

// ES6 section 21.1.3.1 String.prototype.charAt ( pos )
void Builtins::Generate_StringPrototypeCharAt(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* position = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  // Check that {receiver} is coercible to Object and convert it to a String.
  receiver =
      assembler->ToThisString(context, receiver, "String.prototype.charAt");

  // Convert the {position} to a Smi and check that it's in bounds of the
  // {receiver}.
  {
    Label return_emptystring(assembler, Label::kDeferred);
    position = assembler->ToInteger(context, position,
                                    CodeStubAssembler::kTruncateMinusZero);
    assembler->GotoUnless(assembler->WordIsSmi(position), &return_emptystring);

    // Determine the actual length of the {receiver} String.
    Node* receiver_length =
        assembler->LoadObjectField(receiver, String::kLengthOffset);

    // Return "" if the Smi {position} is outside the bounds of the {receiver}.
    Label if_positioninbounds(assembler);
    assembler->Branch(assembler->SmiAboveOrEqual(position, receiver_length),
                      &return_emptystring, &if_positioninbounds);

    assembler->Bind(&return_emptystring);
    assembler->Return(assembler->EmptyStringConstant());

    assembler->Bind(&if_positioninbounds);
  }

  // Load the character code at the {position} from the {receiver}.
  Node* code = assembler->StringCharCodeAt(receiver, position);

  // And return the single character string with only that {code}.
  Node* result = assembler->StringFromCharCode(code);
  assembler->Return(result);
}

// ES6 section 21.1.3.2 String.prototype.charCodeAt ( pos )
void Builtins::Generate_StringPrototypeCharCodeAt(
    CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* position = assembler->Parameter(1);
  Node* context = assembler->Parameter(4);

  // Check that {receiver} is coercible to Object and convert it to a String.
  receiver =
      assembler->ToThisString(context, receiver, "String.prototype.charCodeAt");

  // Convert the {position} to a Smi and check that it's in bounds of the
  // {receiver}.
  {
    Label return_nan(assembler, Label::kDeferred);
    position = assembler->ToInteger(context, position,
                                    CodeStubAssembler::kTruncateMinusZero);
    assembler->GotoUnless(assembler->WordIsSmi(position), &return_nan);

    // Determine the actual length of the {receiver} String.
    Node* receiver_length =
        assembler->LoadObjectField(receiver, String::kLengthOffset);

    // Return NaN if the Smi {position} is outside the bounds of the {receiver}.
    Label if_positioninbounds(assembler);
    assembler->Branch(assembler->SmiAboveOrEqual(position, receiver_length),
                      &return_nan, &if_positioninbounds);

    assembler->Bind(&return_nan);
    assembler->Return(assembler->NaNConstant());

    assembler->Bind(&if_positioninbounds);
  }

  // Load the character at the {position} from the {receiver}.
  Node* value = assembler->StringCharCodeAt(receiver, position);
  Node* result = assembler->SmiFromWord32(value);
  assembler->Return(result);
}

// ES6 section 21.1.3.9
// String.prototype.lastIndexOf ( searchString [ , position ] )
BUILTIN(StringPrototypeLastIndexOf) {
  HandleScope handle_scope(isolate);
  return String::LastIndexOf(isolate, args.receiver(),
                             args.atOrUndefined(isolate, 1),
                             args.atOrUndefined(isolate, 2));
}

// ES6 section 21.1.3.10 String.prototype.localeCompare ( that )
//
// This function is implementation specific.  For now, we do not
// do anything locale specific.
// If internationalization is enabled, then i18n.js will override this function
// and provide the proper functionality, so this is just a fallback.
BUILTIN(StringPrototypeLocaleCompare) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());

  TO_THIS_STRING(str1, "String.prototype.localeCompare");
  Handle<String> str2;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, str2, Object::ToString(isolate, args.at<Object>(1)));

  if (str1.is_identical_to(str2)) return Smi::FromInt(0);  // Equal.
  int str1_length = str1->length();
  int str2_length = str2->length();

  // Decide trivial cases without flattening.
  if (str1_length == 0) {
    if (str2_length == 0) return Smi::FromInt(0);  // Equal.
    return Smi::FromInt(-str2_length);
  } else {
    if (str2_length == 0) return Smi::FromInt(str1_length);
  }

  int end = str1_length < str2_length ? str1_length : str2_length;

  // No need to flatten if we are going to find the answer on the first
  // character. At this point we know there is at least one character
  // in each string, due to the trivial case handling above.
  int d = str1->Get(0) - str2->Get(0);
  if (d != 0) return Smi::FromInt(d);

  str1 = String::Flatten(str1);
  str2 = String::Flatten(str2);

  DisallowHeapAllocation no_gc;
  String::FlatContent flat1 = str1->GetFlatContent();
  String::FlatContent flat2 = str2->GetFlatContent();

  for (int i = 0; i < end; i++) {
    if (flat1.Get(i) != flat2.Get(i)) {
      return Smi::FromInt(flat1.Get(i) - flat2.Get(i));
    }
  }

  return Smi::FromInt(str1_length - str2_length);
}

// ES6 section 21.1.3.12 String.prototype.normalize ( [form] )
//
// Simply checks the argument is valid and returns the string itself.
// If internationalization is enabled, then i18n.js will override this function
// and provide the proper functionality, so this is just a fallback.
BUILTIN(StringPrototypeNormalize) {
  HandleScope handle_scope(isolate);
  TO_THIS_STRING(string, "String.prototype.normalize");

  Handle<Object> form_input = args.atOrUndefined(isolate, 1);
  if (form_input->IsUndefined(isolate)) return *string;

  Handle<String> form;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, form,
                                     Object::ToString(isolate, form_input));

  if (!(String::Equals(form,
                       isolate->factory()->NewStringFromStaticChars("NFC")) ||
        String::Equals(form,
                       isolate->factory()->NewStringFromStaticChars("NFD")) ||
        String::Equals(form,
                       isolate->factory()->NewStringFromStaticChars("NFKC")) ||
        String::Equals(form,
                       isolate->factory()->NewStringFromStaticChars("NFKD")))) {
    Handle<String> valid_forms =
        isolate->factory()->NewStringFromStaticChars("NFC, NFD, NFKC, NFKD");
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewRangeError(MessageTemplate::kNormalizationForm, valid_forms));
  }

  return *string;
}

namespace {

compiler::Node* ToSmiBetweenZeroAnd(CodeStubAssembler* a,
                                    compiler::Node* context,
                                    compiler::Node* value,
                                    compiler::Node* limit) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Label out(a);
  Variable var_result(a, MachineRepresentation::kTagged);

  Node* const value_int =
      a->ToInteger(context, value, CodeStubAssembler::kTruncateMinusZero);

  Label if_issmi(a), if_isnotsmi(a, Label::kDeferred);
  a->Branch(a->WordIsSmi(value_int), &if_issmi, &if_isnotsmi);

  a->Bind(&if_issmi);
  {
    Label if_isinbounds(a), if_isoutofbounds(a, Label::kDeferred);
    a->Branch(a->SmiAbove(value_int, limit), &if_isoutofbounds, &if_isinbounds);

    a->Bind(&if_isinbounds);
    {
      var_result.Bind(value_int);
      a->Goto(&out);
    }

    a->Bind(&if_isoutofbounds);
    {
      Node* const zero = a->SmiConstant(Smi::FromInt(0));
      var_result.Bind(a->Select(a->SmiLessThan(value_int, zero), zero, limit));
      a->Goto(&out);
    }
  }

  a->Bind(&if_isnotsmi);
  {
    // {value} is a heap number - in this case, it is definitely out of bounds.
    a->Assert(a->WordEqual(a->LoadMap(value_int), a->HeapNumberMapConstant()));

    Node* const float_zero = a->Float64Constant(0.);
    Node* const smi_zero = a->SmiConstant(Smi::FromInt(0));
    Node* const value_float = a->LoadHeapNumberValue(value_int);
    var_result.Bind(a->Select(a->Float64LessThan(value_float, float_zero),
                              smi_zero, limit));
    a->Goto(&out);
  }

  a->Bind(&out);
  return var_result.value();
}

}  // namespace

// ES6 section 21.1.3.19 String.prototype.substring ( start, end )
void Builtins::Generate_StringPrototypeSubstring(CodeStubAssembler* a) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Label out(a);

  Variable var_start(a, MachineRepresentation::kTagged);
  Variable var_end(a, MachineRepresentation::kTagged);

  Node* const receiver = a->Parameter(0);
  Node* const start = a->Parameter(1);
  Node* const end = a->Parameter(2);
  Node* const context = a->Parameter(5);

  // Check that {receiver} is coercible to Object and convert it to a String.
  Node* const string =
      a->ToThisString(context, receiver, "String.prototype.substring");

  Node* const length = a->LoadStringLength(string);

  // Conversion and bounds-checks for {start}.
  var_start.Bind(ToSmiBetweenZeroAnd(a, context, start, length));

  // Conversion and bounds-checks for {end}.
  {
    var_end.Bind(length);
    a->GotoIf(a->WordEqual(end, a->UndefinedConstant()), &out);

    var_end.Bind(ToSmiBetweenZeroAnd(a, context, end, length));

    Label if_endislessthanstart(a);
    a->Branch(a->SmiLessThan(var_end.value(), var_start.value()),
              &if_endislessthanstart, &out);

    a->Bind(&if_endislessthanstart);
    {
      Node* const tmp = var_end.value();
      var_end.Bind(var_start.value());
      var_start.Bind(tmp);
      a->Goto(&out);
    }
  }

  a->Bind(&out);
  {
    Node* result =
        a->SubString(context, string, var_start.value(), var_end.value());
    a->Return(result);
  }
}

// ES6 section 21.1.3.25 String.prototype.toString ()
void Builtins::Generate_StringPrototypeToString(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kString, "String.prototype.toString");
  assembler->Return(result);
}

// ES6 section 21.1.3.27 String.prototype.trim ()
BUILTIN(StringPrototypeTrim) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.trim");
  return *String::Trim(string, String::kTrim);
}

// Non-standard WebKit extension
BUILTIN(StringPrototypeTrimLeft) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.trimLeft");
  return *String::Trim(string, String::kTrimLeft);
}

// Non-standard WebKit extension
BUILTIN(StringPrototypeTrimRight) {
  HandleScope scope(isolate);
  TO_THIS_STRING(string, "String.prototype.trimRight");
  return *String::Trim(string, String::kTrimRight);
}

// ES6 section 21.1.3.28 String.prototype.valueOf ( )
void Builtins::Generate_StringPrototypeValueOf(CodeStubAssembler* assembler) {
  typedef compiler::Node Node;

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* result = assembler->ToThisValue(
      context, receiver, PrimitiveType::kString, "String.prototype.valueOf");
  assembler->Return(result);
}

void Builtins::Generate_StringPrototypeIterator(CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Variable var_string(assembler, MachineRepresentation::kTagged);
  Variable var_index(assembler, MachineRepresentation::kTagged);

  Variable* loop_inputs[] = {&var_string, &var_index};
  Label loop(assembler, 2, loop_inputs);
  Label allocate_iterator(assembler);

  Node* receiver = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  Node* string = assembler->ToThisString(context, receiver,
                                         "String.prototype[Symbol.iterator]");
  var_string.Bind(string);
  var_index.Bind(assembler->SmiConstant(Smi::FromInt(0)));

  assembler->Goto(&loop);
  assembler->Bind(&loop);
  {
    // Load the instance type of the {string}.
    Node* string_instance_type = assembler->LoadInstanceType(string);

    // Check if the {string} is a SeqString.
    Label if_stringisnotsequential(assembler);
    assembler->Branch(assembler->Word32Equal(
                          assembler->Word32And(string_instance_type,
                                               assembler->Int32Constant(
                                                   kStringRepresentationMask)),
                          assembler->Int32Constant(kSeqStringTag)),
                      &allocate_iterator, &if_stringisnotsequential);

    assembler->Bind(&if_stringisnotsequential);
    {
      // Check if the {string} is a ConsString.
      Label if_stringiscons(assembler), if_stringisnotcons(assembler);
      assembler->Branch(
          assembler->Word32Equal(
              assembler->Word32And(
                  string_instance_type,
                  assembler->Int32Constant(kStringRepresentationMask)),
              assembler->Int32Constant(kConsStringTag)),
          &if_stringiscons, &if_stringisnotcons);

      assembler->Bind(&if_stringiscons);
      {
        // Flatten cons-string and finish.
        var_string.Bind(assembler->CallRuntime(
            Runtime::kFlattenString, assembler->NoContextConstant(), string));
        assembler->Goto(&allocate_iterator);
      }

      assembler->Bind(&if_stringisnotcons);
      {
        // Check if the {string} is an ExternalString.
        Label if_stringisnotexternal(assembler);
        assembler->Branch(
            assembler->Word32Equal(
                assembler->Word32And(
                    string_instance_type,
                    assembler->Int32Constant(kStringRepresentationMask)),
                assembler->Int32Constant(kExternalStringTag)),
            &allocate_iterator, &if_stringisnotexternal);

        assembler->Bind(&if_stringisnotexternal);
        {
          // The {string} is a SlicedString, continue with its parent.
          Node* index = var_index.value();
          Node* string_offset =
              assembler->LoadObjectField(string, SlicedString::kOffsetOffset);
          Node* string_parent =
              assembler->LoadObjectField(string, SlicedString::kParentOffset);
          var_index.Bind(assembler->SmiAdd(index, string_offset));
          var_string.Bind(string_parent);
          assembler->Goto(&loop);
        }
      }
    }
  }

  assembler->Bind(&allocate_iterator);
  {
    Node* native_context = assembler->LoadNativeContext(context);
    Node* map = assembler->LoadFixedArrayElement(
        native_context,
        assembler->IntPtrConstant(Context::STRING_ITERATOR_MAP_INDEX), 0,
        CodeStubAssembler::INTPTR_PARAMETERS);
    Node* iterator = assembler->Allocate(JSStringIterator::kSize);
    assembler->StoreMapNoWriteBarrier(iterator, map);
    assembler->StoreObjectFieldRoot(iterator, JSValue::kPropertiesOffset,
                                    Heap::kEmptyFixedArrayRootIndex);
    assembler->StoreObjectFieldRoot(iterator, JSObject::kElementsOffset,
                                    Heap::kEmptyFixedArrayRootIndex);
    assembler->StoreObjectFieldNoWriteBarrier(
        iterator, JSStringIterator::kStringOffset, var_string.value());

    assembler->StoreObjectFieldNoWriteBarrier(
        iterator, JSStringIterator::kNextIndexOffset, var_index.value());
    assembler->Return(iterator);
  }
}

namespace {

// Return the |word32| codepoint at {index}. Supports SeqStrings and
// ExternalStrings.
compiler::Node* LoadSurrogatePairInternal(CodeStubAssembler* assembler,
                                          compiler::Node* string,
                                          compiler::Node* length,
                                          compiler::Node* index,
                                          UnicodeEncoding encoding) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;
  Label handle_surrogate_pair(assembler), return_result(assembler);
  Variable var_result(assembler, MachineRepresentation::kWord32);
  Variable var_trail(assembler, MachineRepresentation::kWord16);
  var_result.Bind(assembler->Int32Constant(0));
  var_trail.Bind(assembler->Int32Constant(0));

  Node* string_instance_type = assembler->LoadInstanceType(string);

  Label if_stringissequential(assembler), if_stringisexternal(assembler);
  assembler->Branch(assembler->Word32Equal(
                        assembler->Word32And(string_instance_type,
                                             assembler->Int32Constant(
                                                 kStringRepresentationMask)),
                        assembler->Int32Constant(kSeqStringTag)),
                    &if_stringissequential, &if_stringisexternal);

  assembler->Bind(&if_stringissequential);
  {
    Label if_stringisonebyte(assembler), if_stringistwobyte(assembler);
    assembler->Branch(
        assembler->Word32Equal(
            assembler->Word32And(string_instance_type,
                                 assembler->Int32Constant(kStringEncodingMask)),
            assembler->Int32Constant(kOneByteStringTag)),
        &if_stringisonebyte, &if_stringistwobyte);

    assembler->Bind(&if_stringisonebyte);
    {
      var_result.Bind(assembler->Load(
          MachineType::Uint8(), string,
          assembler->IntPtrAdd(
              index, assembler->IntPtrConstant(SeqOneByteString::kHeaderSize -
                                               kHeapObjectTag))));
      assembler->Goto(&return_result);
    }

    assembler->Bind(&if_stringistwobyte);
    {
      Node* lead = assembler->Load(
          MachineType::Uint16(), string,
          assembler->IntPtrAdd(
              assembler->WordShl(index, assembler->IntPtrConstant(1)),
              assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                        kHeapObjectTag)));
      var_result.Bind(lead);
      Node* next_pos = assembler->Int32Add(index, assembler->Int32Constant(1));

      Label if_isdoublecodeunit(assembler);
      assembler->GotoIf(assembler->Int32GreaterThanOrEqual(next_pos, length),
                        &return_result);
      assembler->GotoIf(
          assembler->Uint32LessThan(lead, assembler->Int32Constant(0xD800)),
          &return_result);
      assembler->Branch(
          assembler->Uint32LessThan(lead, assembler->Int32Constant(0xDC00)),
          &if_isdoublecodeunit, &return_result);

      assembler->Bind(&if_isdoublecodeunit);
      {
        Node* trail = assembler->Load(
            MachineType::Uint16(), string,
            assembler->IntPtrAdd(
                assembler->WordShl(next_pos, assembler->IntPtrConstant(1)),
                assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                          kHeapObjectTag)));
        assembler->GotoIf(
            assembler->Uint32LessThan(trail, assembler->Int32Constant(0xDC00)),
            &return_result);
        assembler->GotoIf(assembler->Uint32GreaterThanOrEqual(
                              trail, assembler->Int32Constant(0xE000)),
                          &return_result);

        var_trail.Bind(trail);
        assembler->Goto(&handle_surrogate_pair);
      }
    }
  }

  assembler->Bind(&if_stringisexternal);
  {
    assembler->Assert(assembler->Word32Equal(
        assembler->Word32And(
            string_instance_type,
            assembler->Int32Constant(kStringRepresentationMask)),
        assembler->Int32Constant(kExternalStringTag)));
    Label if_stringisshort(assembler), if_stringisnotshort(assembler);

    assembler->Branch(assembler->Word32Equal(
                          assembler->Word32And(string_instance_type,
                                               assembler->Int32Constant(
                                                   kShortExternalStringMask)),
                          assembler->Int32Constant(0)),
                      &if_stringisshort, &if_stringisnotshort);

    assembler->Bind(&if_stringisshort);
    {
      // Load the actual resource data from the {string}.
      Node* string_resource_data = assembler->LoadObjectField(
          string, ExternalString::kResourceDataOffset, MachineType::Pointer());

      Label if_stringistwobyte(assembler), if_stringisonebyte(assembler);
      assembler->Branch(assembler->Word32Equal(
                            assembler->Word32And(
                                string_instance_type,
                                assembler->Int32Constant(kStringEncodingMask)),
                            assembler->Int32Constant(kTwoByteStringTag)),
                        &if_stringistwobyte, &if_stringisonebyte);

      assembler->Bind(&if_stringisonebyte);
      {
        var_result.Bind(
            assembler->Load(MachineType::Uint8(), string_resource_data, index));
        assembler->Goto(&return_result);
      }

      assembler->Bind(&if_stringistwobyte);
      {
        Label if_isdoublecodeunit(assembler);
        Node* lead = assembler->Load(
            MachineType::Uint16(), string_resource_data,
            assembler->WordShl(index, assembler->IntPtrConstant(1)));
        var_result.Bind(lead);
        Node* next_pos =
            assembler->Int32Add(index, assembler->Int32Constant(1));

        assembler->GotoIf(assembler->Int32GreaterThanOrEqual(next_pos, length),
                          &return_result);
        assembler->GotoIf(
            assembler->Uint32LessThan(lead, assembler->Int32Constant(0xD800)),
            &return_result);
        assembler->Branch(
            assembler->Uint32LessThan(lead, assembler->Int32Constant(0xDC00)),
            &if_isdoublecodeunit, &return_result);

        assembler->Bind(&if_isdoublecodeunit);
        {
          Node* trail = assembler->Load(
              MachineType::Uint16(), string,
              assembler->IntPtrAdd(
                  assembler->WordShl(next_pos, assembler->IntPtrConstant(1)),
                  assembler->IntPtrConstant(SeqTwoByteString::kHeaderSize -
                                            kHeapObjectTag)));
          assembler->GotoIf(assembler->Uint32LessThan(
                                trail, assembler->Int32Constant(0xDC00)),
                            &return_result);
          assembler->GotoIf(assembler->Uint32GreaterThanOrEqual(
                                trail, assembler->Int32Constant(0xE000)),
                            &return_result);

          var_trail.Bind(trail);
          assembler->Goto(&handle_surrogate_pair);
        }
      }
    }

    assembler->Bind(&if_stringisnotshort);
    {
      Label if_isdoublecodeunit(assembler);
      Node* lead = assembler->SmiToWord32(assembler->CallRuntime(
          Runtime::kExternalStringGetChar, assembler->NoContextConstant(),
          string, assembler->SmiTag(index)));
      var_result.Bind(lead);
      Node* next_pos = assembler->Int32Add(index, assembler->Int32Constant(1));

      assembler->GotoIf(assembler->Int32GreaterThanOrEqual(next_pos, length),
                        &return_result);
      assembler->GotoIf(
          assembler->Uint32LessThan(lead, assembler->Int32Constant(0xD800)),
          &return_result);
      assembler->Branch(assembler->Uint32GreaterThanOrEqual(
                            lead, assembler->Int32Constant(0xDC00)),
                        &return_result, &if_isdoublecodeunit);

      assembler->Bind(&if_isdoublecodeunit);
      {
        Node* trail = assembler->SmiToWord32(assembler->CallRuntime(
            Runtime::kExternalStringGetChar, assembler->NoContextConstant(),
            string, assembler->SmiTag(next_pos)));
        assembler->GotoIf(
            assembler->Uint32LessThan(trail, assembler->Int32Constant(0xDC00)),
            &return_result);
        assembler->GotoIf(assembler->Uint32GreaterThanOrEqual(
                              trail, assembler->Int32Constant(0xE000)),
                          &return_result);
        var_trail.Bind(trail);
        assembler->Goto(&handle_surrogate_pair);
      }
    }
  }

  assembler->Bind(&handle_surrogate_pair);
  {
    Node* lead = var_result.value();
    Node* trail = var_trail.value();
#ifdef ENABLE_SLOW_DCHECKS
    // Check that this path is only taken if a surrogate pair is found
    assembler->Assert(assembler->Uint32GreaterThanOrEqual(
        lead, assembler->Int32Constant(0xD800)));
    assembler->Assert(
        assembler->Uint32LessThan(lead, assembler->Int32Constant(0xDC00)));
    assembler->Assert(assembler->Uint32GreaterThanOrEqual(
        trail, assembler->Int32Constant(0xDC00)));
    assembler->Assert(
        assembler->Uint32LessThan(trail, assembler->Int32Constant(0xE000)));
#endif

    switch (encoding) {
      case UnicodeEncoding::UTF16:
        var_result.Bind(assembler->WordOr(
            assembler->WordShl(trail, assembler->Int32Constant(16)), lead));
        break;

      case UnicodeEncoding::UTF32: {
        // Convert UTF16 surrogate pair into |word32| code point, encoded as
        // UTF32.
        Node* surrogate_offset =
            assembler->Int32Constant(0x10000 - (0xD800 << 10) - 0xDC00);

        // (lead << 10) + trail + SURROGATE_OFFSET
        var_result.Bind(assembler->Int32Add(
            assembler->WordShl(lead, assembler->Int32Constant(10)),
            assembler->Int32Add(trail, surrogate_offset)));
        break;
      }
    }
    assembler->Goto(&return_result);
  }

  assembler->Bind(&return_result);
  return var_result.value();
}

compiler::Node* LoadSurrogatePairAt(CodeStubAssembler* assembler,
                                    compiler::Node* string,
                                    compiler::Node* length,
                                    compiler::Node* index) {
  return LoadSurrogatePairInternal(assembler, string, length, index,
                                   UnicodeEncoding::UTF16);
}

}  // namespace

void Builtins::Generate_StringIteratorPrototypeNext(
    CodeStubAssembler* assembler) {
  typedef CodeStubAssembler::Label Label;
  typedef compiler::Node Node;
  typedef CodeStubAssembler::Variable Variable;

  Variable var_value(assembler, MachineRepresentation::kTagged);
  Variable var_done(assembler, MachineRepresentation::kTagged);

  var_value.Bind(assembler->UndefinedConstant());
  var_done.Bind(assembler->BooleanConstant(true));

  Label throw_bad_receiver(assembler), next_codepoint(assembler),
      return_result(assembler);

  Node* iterator = assembler->Parameter(0);
  Node* context = assembler->Parameter(3);

  assembler->GotoIf(assembler->WordIsSmi(iterator), &throw_bad_receiver);
  assembler->GotoUnless(
      assembler->WordEqual(assembler->LoadInstanceType(iterator),
                           assembler->Int32Constant(JS_STRING_ITERATOR_TYPE)),
      &throw_bad_receiver);

  Node* string =
      assembler->LoadObjectField(iterator, JSStringIterator::kStringOffset);
  Node* position =
      assembler->LoadObjectField(iterator, JSStringIterator::kNextIndexOffset);
  Node* length = assembler->LoadObjectField(string, String::kLengthOffset);

  assembler->Branch(assembler->SmiLessThan(position, length), &next_codepoint,
                    &return_result);

  assembler->Bind(&next_codepoint);
  {
    Node* ch =
        LoadSurrogatePairAt(assembler, string, assembler->SmiUntag(length),
                            assembler->SmiUntag(position));
    Node* value = assembler->StringFromCodePoint(ch, UnicodeEncoding::UTF16);
    var_value.Bind(value);
    Node* length = assembler->LoadObjectField(value, String::kLengthOffset);
    assembler->StoreObjectFieldNoWriteBarrier(
        iterator, JSStringIterator::kNextIndexOffset,
        assembler->SmiAdd(position, length));
    var_done.Bind(assembler->BooleanConstant(false));
    assembler->Goto(&return_result);
  }

  assembler->Bind(&return_result);
  {
    Node* native_context = assembler->LoadNativeContext(context);
    Node* map = assembler->LoadFixedArrayElement(
        native_context,
        assembler->IntPtrConstant(Context::ITERATOR_RESULT_MAP_INDEX), 0,
        CodeStubAssembler::INTPTR_PARAMETERS);
    Node* result = assembler->Allocate(JSIteratorResult::kSize);
    assembler->StoreMapNoWriteBarrier(result, map);
    assembler->StoreObjectFieldRoot(result, JSIteratorResult::kPropertiesOffset,
                                    Heap::kEmptyFixedArrayRootIndex);
    assembler->StoreObjectFieldRoot(result, JSIteratorResult::kElementsOffset,
                                    Heap::kEmptyFixedArrayRootIndex);
    assembler->StoreObjectFieldNoWriteBarrier(
        result, JSIteratorResult::kValueOffset, var_value.value());
    assembler->StoreObjectFieldNoWriteBarrier(
        result, JSIteratorResult::kDoneOffset, var_done.value());
    assembler->Return(result);
  }

  assembler->Bind(&throw_bad_receiver);
  {
    // The {receiver} is not a valid JSGeneratorObject.
    Node* result = assembler->CallRuntime(
        Runtime::kThrowIncompatibleMethodReceiver, context,
        assembler->HeapConstant(assembler->factory()->NewStringFromAsciiChecked(
            "String Iterator.prototype.next", TENURED)),
        iterator);
    assembler->Return(result);  // Never reached.
  }
}

}  // namespace internal
}  // namespace v8
