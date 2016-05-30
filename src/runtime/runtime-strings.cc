// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/runtime/runtime-utils.h"

#include "src/arguments.h"
#include "src/regexp/jsregexp-inl.h"
#include "src/string-builder.h"
#include "src/string-search.h"

namespace v8 {
namespace internal {


// Perform string match of pattern on subject, starting at start index.
// Caller must ensure that 0 <= start_index <= sub->length(),
// and should check that pat->length() + start_index <= sub->length().
int StringMatch(Isolate* isolate, Handle<String> sub, Handle<String> pat,
                int start_index) {
  DCHECK(0 <= start_index);
  DCHECK(start_index <= sub->length());

  int pattern_length = pat->length();
  if (pattern_length == 0) return start_index;

  int subject_length = sub->length();
  if (start_index + pattern_length > subject_length) return -1;

  sub = String::Flatten(sub);
  pat = String::Flatten(pat);

  DisallowHeapAllocation no_gc;  // ensure vectors stay valid
  // Extract flattened substrings of cons strings before getting encoding.
  String::FlatContent seq_sub = sub->GetFlatContent();
  String::FlatContent seq_pat = pat->GetFlatContent();

  // dispatch on type of strings
  if (seq_pat.IsOneByte()) {
    Vector<const uint8_t> pat_vector = seq_pat.ToOneByteVector();
    if (seq_sub.IsOneByte()) {
      return SearchString(isolate, seq_sub.ToOneByteVector(), pat_vector,
                          start_index);
    }
    return SearchString(isolate, seq_sub.ToUC16Vector(), pat_vector,
                        start_index);
  }
  Vector<const uc16> pat_vector = seq_pat.ToUC16Vector();
  if (seq_sub.IsOneByte()) {
    return SearchString(isolate, seq_sub.ToOneByteVector(), pat_vector,
                        start_index);
  }
  return SearchString(isolate, seq_sub.ToUC16Vector(), pat_vector, start_index);
}


// This may return an empty MaybeHandle if an exception is thrown or
// we abort due to reaching the recursion limit.
MaybeHandle<String> StringReplaceOneCharWithString(
    Isolate* isolate, Handle<String> subject, Handle<String> search,
    Handle<String> replace, bool* found, int recursion_limit) {
  StackLimitCheck stackLimitCheck(isolate);
  if (stackLimitCheck.HasOverflowed() || (recursion_limit == 0)) {
    return MaybeHandle<String>();
  }
  recursion_limit--;
  if (subject->IsConsString()) {
    ConsString* cons = ConsString::cast(*subject);
    Handle<String> first = Handle<String>(cons->first());
    Handle<String> second = Handle<String>(cons->second());
    Handle<String> new_first;
    if (!StringReplaceOneCharWithString(isolate, first, search, replace, found,
                                        recursion_limit).ToHandle(&new_first)) {
      return MaybeHandle<String>();
    }
    if (*found) return isolate->factory()->NewConsString(new_first, second);

    Handle<String> new_second;
    if (!StringReplaceOneCharWithString(isolate, second, search, replace, found,
                                        recursion_limit)
             .ToHandle(&new_second)) {
      return MaybeHandle<String>();
    }
    if (*found) return isolate->factory()->NewConsString(first, new_second);

    return subject;
  } else {
    int index = StringMatch(isolate, subject, search, 0);
    if (index == -1) return subject;
    *found = true;
    Handle<String> first = isolate->factory()->NewSubString(subject, 0, index);
    Handle<String> cons1;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, cons1, isolate->factory()->NewConsString(first, replace),
        String);
    Handle<String> second =
        isolate->factory()->NewSubString(subject, index + 1, subject->length());
    return isolate->factory()->NewConsString(cons1, second);
  }
}


RUNTIME_FUNCTION(Runtime_StringReplaceOneCharWithString) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(String, subject, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, search, 1);
  CONVERT_ARG_HANDLE_CHECKED(String, replace, 2);

  // If the cons string tree is too deep, we simply abort the recursion and
  // retry with a flattened subject string.
  const int kRecursionLimit = 0x1000;
  bool found = false;
  Handle<String> result;
  if (StringReplaceOneCharWithString(isolate, subject, search, replace, &found,
                                     kRecursionLimit).ToHandle(&result)) {
    return *result;
  }
  if (isolate->has_pending_exception()) return isolate->heap()->exception();

  subject = String::Flatten(subject);
  if (StringReplaceOneCharWithString(isolate, subject, search, replace, &found,
                                     kRecursionLimit).ToHandle(&result)) {
    return *result;
  }
  if (isolate->has_pending_exception()) return isolate->heap()->exception();
  // In case of empty handle and no pending exception we have stack overflow.
  return isolate->StackOverflow();
}


RUNTIME_FUNCTION(Runtime_StringIndexOf) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);

  CONVERT_ARG_HANDLE_CHECKED(String, sub, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, pat, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, index, 2);

  uint32_t start_index = 0;
  if (!index->ToArrayIndex(&start_index)) return Smi::FromInt(-1);

  RUNTIME_ASSERT(start_index <= static_cast<uint32_t>(sub->length()));
  int position = StringMatch(isolate, sub, pat, start_index);
  return Smi::FromInt(position);
}


template <typename schar, typename pchar>
static int StringMatchBackwards(Vector<const schar> subject,
                                Vector<const pchar> pattern, int idx) {
  int pattern_length = pattern.length();
  DCHECK(pattern_length >= 1);
  DCHECK(idx + pattern_length <= subject.length());

  if (sizeof(schar) == 1 && sizeof(pchar) > 1) {
    for (int i = 0; i < pattern_length; i++) {
      uc16 c = pattern[i];
      if (c > String::kMaxOneByteCharCode) {
        return -1;
      }
    }
  }

  pchar pattern_first_char = pattern[0];
  for (int i = idx; i >= 0; i--) {
    if (subject[i] != pattern_first_char) continue;
    int j = 1;
    while (j < pattern_length) {
      if (pattern[j] != subject[i + j]) {
        break;
      }
      j++;
    }
    if (j == pattern_length) {
      return i;
    }
  }
  return -1;
}


RUNTIME_FUNCTION(Runtime_StringLastIndexOf) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);

  CONVERT_ARG_HANDLE_CHECKED(String, sub, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, pat, 1);
  CONVERT_ARG_HANDLE_CHECKED(Object, index, 2);

  uint32_t start_index = 0;
  if (!index->ToArrayIndex(&start_index)) return Smi::FromInt(-1);

  uint32_t pat_length = pat->length();
  uint32_t sub_length = sub->length();

  if (start_index + pat_length > sub_length) {
    start_index = sub_length - pat_length;
  }

  if (pat_length == 0) {
    return Smi::FromInt(start_index);
  }

  sub = String::Flatten(sub);
  pat = String::Flatten(pat);

  int position = -1;
  DisallowHeapAllocation no_gc;  // ensure vectors stay valid

  String::FlatContent sub_content = sub->GetFlatContent();
  String::FlatContent pat_content = pat->GetFlatContent();

  if (pat_content.IsOneByte()) {
    Vector<const uint8_t> pat_vector = pat_content.ToOneByteVector();
    if (sub_content.IsOneByte()) {
      position = StringMatchBackwards(sub_content.ToOneByteVector(), pat_vector,
                                      start_index);
    } else {
      position = StringMatchBackwards(sub_content.ToUC16Vector(), pat_vector,
                                      start_index);
    }
  } else {
    Vector<const uc16> pat_vector = pat_content.ToUC16Vector();
    if (sub_content.IsOneByte()) {
      position = StringMatchBackwards(sub_content.ToOneByteVector(), pat_vector,
                                      start_index);
    } else {
      position = StringMatchBackwards(sub_content.ToUC16Vector(), pat_vector,
                                      start_index);
    }
  }

  return Smi::FromInt(position);
}


RUNTIME_FUNCTION(Runtime_StringLocaleCompare) {
  HandleScope handle_scope(isolate);
  DCHECK(args.length() == 2);

  CONVERT_ARG_HANDLE_CHECKED(String, str1, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, str2, 1);

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
  // character.  At this point we know there is at least one character
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


RUNTIME_FUNCTION(Runtime_SubString) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);

  CONVERT_ARG_HANDLE_CHECKED(String, string, 0);
  int start, end;
  // We have a fast integer-only case here to avoid a conversion to double in
  // the common case where from and to are Smis.
  if (args[1]->IsSmi() && args[2]->IsSmi()) {
    CONVERT_SMI_ARG_CHECKED(from_number, 1);
    CONVERT_SMI_ARG_CHECKED(to_number, 2);
    start = from_number;
    end = to_number;
  } else {
    CONVERT_DOUBLE_ARG_CHECKED(from_number, 1);
    CONVERT_DOUBLE_ARG_CHECKED(to_number, 2);
    start = FastD2IChecked(from_number);
    end = FastD2IChecked(to_number);
  }
  RUNTIME_ASSERT(end >= start);
  RUNTIME_ASSERT(start >= 0);
  RUNTIME_ASSERT(end <= string->length());
  isolate->counters()->sub_string_runtime()->Increment();

  return *isolate->factory()->NewSubString(string, start, end);
}


RUNTIME_FUNCTION(Runtime_StringAdd) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(String, str1, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, str2, 1);
  isolate->counters()->string_add_runtime()->Increment();
  RETURN_RESULT_OR_FAILURE(isolate,
                           isolate->factory()->NewConsString(str1, str2));
}


RUNTIME_FUNCTION(Runtime_InternalizeString) {
  HandleScope handles(isolate);
  RUNTIME_ASSERT(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(String, string, 0);
  return *isolate->factory()->InternalizeString(string);
}


RUNTIME_FUNCTION(Runtime_StringMatch) {
  HandleScope handles(isolate);
  DCHECK(args.length() == 3);

  CONVERT_ARG_HANDLE_CHECKED(String, subject, 0);
  CONVERT_ARG_HANDLE_CHECKED(JSRegExp, regexp, 1);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, regexp_info, 2);

  RUNTIME_ASSERT(regexp_info->HasFastObjectElements());

  RegExpImpl::GlobalCache global_cache(regexp, subject, isolate);
  if (global_cache.HasException()) return isolate->heap()->exception();

  int capture_count = regexp->CaptureCount();

  ZoneScope zone_scope(isolate->runtime_zone());
  ZoneList<int> offsets(8, zone_scope.zone());

  while (true) {
    int32_t* match = global_cache.FetchNext();
    if (match == NULL) break;
    offsets.Add(match[0], zone_scope.zone());  // start
    offsets.Add(match[1], zone_scope.zone());  // end
  }

  if (global_cache.HasException()) return isolate->heap()->exception();

  if (offsets.length() == 0) {
    // Not a single match.
    return isolate->heap()->null_value();
  }

  RegExpImpl::SetLastMatchInfo(regexp_info, subject, capture_count,
                               global_cache.LastSuccessfulMatch());

  int matches = offsets.length() / 2;
  Handle<FixedArray> elements = isolate->factory()->NewFixedArray(matches);
  Handle<String> substring =
      isolate->factory()->NewSubString(subject, offsets.at(0), offsets.at(1));
  elements->set(0, *substring);
  FOR_WITH_HANDLE_SCOPE(isolate, int, i = 1, i, i < matches, i++, {
    int from = offsets.at(i * 2);
    int to = offsets.at(i * 2 + 1);
    Handle<String> substring =
        isolate->factory()->NewProperSubString(subject, from, to);
    elements->set(i, *substring);
  });
  Handle<JSArray> result = isolate->factory()->NewJSArrayWithElements(elements);
  result->set_length(Smi::FromInt(matches));
  return *result;
}


RUNTIME_FUNCTION(Runtime_StringCharCodeAtRT) {
  HandleScope handle_scope(isolate);
  DCHECK(args.length() == 2);

  CONVERT_ARG_HANDLE_CHECKED(String, subject, 0);
  CONVERT_NUMBER_CHECKED(uint32_t, i, Uint32, args[1]);

  // Flatten the string.  If someone wants to get a char at an index
  // in a cons string, it is likely that more indices will be
  // accessed.
  subject = String::Flatten(subject);

  if (i >= static_cast<uint32_t>(subject->length())) {
    return isolate->heap()->nan_value();
  }

  return Smi::FromInt(subject->Get(i));
}


RUNTIME_FUNCTION(Runtime_StringCompare) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  isolate->counters()->string_compare_runtime()->Increment();
  switch (String::Compare(x, y)) {
    case ComparisonResult::kLessThan:
      return Smi::FromInt(LESS);
    case ComparisonResult::kEqual:
      return Smi::FromInt(EQUAL);
    case ComparisonResult::kGreaterThan:
      return Smi::FromInt(GREATER);
    case ComparisonResult::kUndefined:
      break;
  }
  UNREACHABLE();
  return Smi::FromInt(0);
}


RUNTIME_FUNCTION(Runtime_StringBuilderConcat) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, array, 0);
  int32_t array_length;
  if (!args[1]->ToInt32(&array_length)) {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewInvalidStringLengthError());
  }
  CONVERT_ARG_HANDLE_CHECKED(String, special, 2);

  size_t actual_array_length = 0;
  RUNTIME_ASSERT(
      TryNumberToSize(isolate, array->length(), &actual_array_length));
  RUNTIME_ASSERT(array_length >= 0);
  RUNTIME_ASSERT(static_cast<size_t>(array_length) <= actual_array_length);

  // This assumption is used by the slice encoding in one or two smis.
  DCHECK(Smi::kMaxValue >= String::kMaxLength);

  RUNTIME_ASSERT(array->HasFastElements());
  JSObject::EnsureCanContainHeapObjectElements(array);

  int special_length = special->length();
  if (!array->HasFastObjectElements()) {
    return isolate->Throw(isolate->heap()->illegal_argument_string());
  }

  int length;
  bool one_byte = special->HasOnlyOneByteChars();

  {
    DisallowHeapAllocation no_gc;
    FixedArray* fixed_array = FixedArray::cast(array->elements());
    if (fixed_array->length() < array_length) {
      array_length = fixed_array->length();
    }

    if (array_length == 0) {
      return isolate->heap()->empty_string();
    } else if (array_length == 1) {
      Object* first = fixed_array->get(0);
      if (first->IsString()) return first;
    }
    length = StringBuilderConcatLength(special_length, fixed_array,
                                       array_length, &one_byte);
  }

  if (length == -1) {
    return isolate->Throw(isolate->heap()->illegal_argument_string());
  }

  if (one_byte) {
    Handle<SeqOneByteString> answer;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, answer, isolate->factory()->NewRawOneByteString(length));
    StringBuilderConcatHelper(*special, answer->GetChars(),
                              FixedArray::cast(array->elements()),
                              array_length);
    return *answer;
  } else {
    Handle<SeqTwoByteString> answer;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, answer, isolate->factory()->NewRawTwoByteString(length));
    StringBuilderConcatHelper(*special, answer->GetChars(),
                              FixedArray::cast(array->elements()),
                              array_length);
    return *answer;
  }
}


RUNTIME_FUNCTION(Runtime_StringBuilderJoin) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, array, 0);
  int32_t array_length;
  if (!args[1]->ToInt32(&array_length)) {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewInvalidStringLengthError());
  }
  CONVERT_ARG_HANDLE_CHECKED(String, separator, 2);
  RUNTIME_ASSERT(array->HasFastObjectElements());
  RUNTIME_ASSERT(array_length >= 0);

  Handle<FixedArray> fixed_array(FixedArray::cast(array->elements()));
  if (fixed_array->length() < array_length) {
    array_length = fixed_array->length();
  }

  if (array_length == 0) {
    return isolate->heap()->empty_string();
  } else if (array_length == 1) {
    Object* first = fixed_array->get(0);
    RUNTIME_ASSERT(first->IsString());
    return first;
  }

  int separator_length = separator->length();
  RUNTIME_ASSERT(separator_length > 0);
  int max_nof_separators =
      (String::kMaxLength + separator_length - 1) / separator_length;
  if (max_nof_separators < (array_length - 1)) {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewInvalidStringLengthError());
  }
  int length = (array_length - 1) * separator_length;
  for (int i = 0; i < array_length; i++) {
    Object* element_obj = fixed_array->get(i);
    RUNTIME_ASSERT(element_obj->IsString());
    String* element = String::cast(element_obj);
    int increment = element->length();
    if (increment > String::kMaxLength - length) {
      STATIC_ASSERT(String::kMaxLength < kMaxInt);
      length = kMaxInt;  // Provoke exception;
      break;
    }
    length += increment;
  }

  Handle<SeqTwoByteString> answer;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, answer, isolate->factory()->NewRawTwoByteString(length));

  DisallowHeapAllocation no_gc;

  uc16* sink = answer->GetChars();
#ifdef DEBUG
  uc16* end = sink + length;
#endif

  RUNTIME_ASSERT(fixed_array->get(0)->IsString());
  String* first = String::cast(fixed_array->get(0));
  String* separator_raw = *separator;

  int first_length = first->length();
  String::WriteToFlat(first, sink, 0, first_length);
  sink += first_length;

  for (int i = 1; i < array_length; i++) {
    DCHECK(sink + separator_length <= end);
    String::WriteToFlat(separator_raw, sink, 0, separator_length);
    sink += separator_length;

    RUNTIME_ASSERT(fixed_array->get(i)->IsString());
    String* element = String::cast(fixed_array->get(i));
    int element_length = element->length();
    DCHECK(sink + element_length <= end);
    String::WriteToFlat(element, sink, 0, element_length);
    sink += element_length;
  }
  DCHECK(sink == end);

  // Use %_FastOneByteArrayJoin instead.
  DCHECK(!answer->IsOneByteRepresentation());
  return *answer;
}

template <typename sinkchar>
static void WriteRepeatToFlat(String* src, Vector<sinkchar> buffer, int cursor,
                              int repeat, int length) {
  if (repeat == 0) return;

  sinkchar* start = &buffer[cursor];
  String::WriteToFlat<sinkchar>(src, start, 0, length);

  int done = 1;
  sinkchar* next = start + length;

  while (done < repeat) {
    int block = Min(done, repeat - done);
    int block_chars = block * length;
    CopyChars(next, start, block_chars);
    next += block_chars;
    done += block;
  }
}

template <typename Char>
static void JoinSparseArrayWithSeparator(FixedArray* elements,
                                         int elements_length,
                                         uint32_t array_length,
                                         String* separator,
                                         Vector<Char> buffer) {
  DisallowHeapAllocation no_gc;
  int previous_separator_position = 0;
  int separator_length = separator->length();
  DCHECK_LT(0, separator_length);
  int cursor = 0;
  for (int i = 0; i < elements_length; i += 2) {
    int position = NumberToInt32(elements->get(i));
    String* string = String::cast(elements->get(i + 1));
    int string_length = string->length();
    if (string->length() > 0) {
      int repeat = position - previous_separator_position;
      WriteRepeatToFlat<Char>(separator, buffer, cursor, repeat,
                              separator_length);
      cursor += repeat * separator_length;
      previous_separator_position = position;
      String::WriteToFlat<Char>(string, &buffer[cursor], 0, string_length);
      cursor += string->length();
    }
  }

  int last_array_index = static_cast<int>(array_length - 1);
  // Array length must be representable as a signed 32-bit number,
  // otherwise the total string length would have been too large.
  DCHECK(array_length <= 0x7fffffff);  // Is int32_t.
  int repeat = last_array_index - previous_separator_position;
  WriteRepeatToFlat<Char>(separator, buffer, cursor, repeat, separator_length);
  cursor += repeat * separator_length;
  DCHECK(cursor <= buffer.length());
}


RUNTIME_FUNCTION(Runtime_SparseJoinWithSeparator) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 3);
  CONVERT_ARG_HANDLE_CHECKED(JSArray, elements_array, 0);
  CONVERT_NUMBER_CHECKED(uint32_t, array_length, Uint32, args[1]);
  CONVERT_ARG_HANDLE_CHECKED(String, separator, 2);
  // elements_array is fast-mode JSarray of alternating positions
  // (increasing order) and strings.
  RUNTIME_ASSERT(elements_array->HasFastSmiOrObjectElements());
  // array_length is length of original array (used to add separators);
  // separator is string to put between elements. Assumed to be non-empty.
  RUNTIME_ASSERT(array_length > 0);

  // Find total length of join result.
  int string_length = 0;
  bool is_one_byte = separator->IsOneByteRepresentation();
  bool overflow = false;
  CONVERT_NUMBER_CHECKED(int, elements_length, Int32, elements_array->length());
  RUNTIME_ASSERT(elements_length <= elements_array->elements()->length());
  RUNTIME_ASSERT((elements_length & 1) == 0);  // Even length.
  FixedArray* elements = FixedArray::cast(elements_array->elements());
  {
    DisallowHeapAllocation no_gc;
    for (int i = 0; i < elements_length; i += 2) {
      String* string = String::cast(elements->get(i + 1));
      int length = string->length();
      if (is_one_byte && !string->IsOneByteRepresentation()) {
        is_one_byte = false;
      }
      if (length > String::kMaxLength ||
          String::kMaxLength - length < string_length) {
        overflow = true;
        break;
      }
      string_length += length;
    }
  }

  int separator_length = separator->length();
  if (!overflow && separator_length > 0) {
    if (array_length <= 0x7fffffffu) {
      int separator_count = static_cast<int>(array_length) - 1;
      int remaining_length = String::kMaxLength - string_length;
      if ((remaining_length / separator_length) >= separator_count) {
        string_length += separator_length * (array_length - 1);
      } else {
        // Not room for the separators within the maximal string length.
        overflow = true;
      }
    } else {
      // Nonempty separator and at least 2^31-1 separators necessary
      // means that the string is too large to create.
      STATIC_ASSERT(String::kMaxLength < 0x7fffffff);
      overflow = true;
    }
  }
  if (overflow) {
    // Throw an exception if the resulting string is too large. See
    // https://code.google.com/p/chromium/issues/detail?id=336820
    // for details.
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewInvalidStringLengthError());
  }

  if (is_one_byte) {
    Handle<SeqOneByteString> result = isolate->factory()
                                          ->NewRawOneByteString(string_length)
                                          .ToHandleChecked();
    JoinSparseArrayWithSeparator<uint8_t>(
        FixedArray::cast(elements_array->elements()), elements_length,
        array_length, *separator,
        Vector<uint8_t>(result->GetChars(), string_length));
    return *result;
  } else {
    Handle<SeqTwoByteString> result = isolate->factory()
                                          ->NewRawTwoByteString(string_length)
                                          .ToHandleChecked();
    JoinSparseArrayWithSeparator<uc16>(
        FixedArray::cast(elements_array->elements()), elements_length,
        array_length, *separator,
        Vector<uc16>(result->GetChars(), string_length));
    return *result;
  }
}


// Copies Latin1 characters to the given fixed array looking up
// one-char strings in the cache. Gives up on the first char that is
// not in the cache and fills the remainder with smi zeros. Returns
// the length of the successfully copied prefix.
static int CopyCachedOneByteCharsToArray(Heap* heap, const uint8_t* chars,
                                         FixedArray* elements, int length) {
  DisallowHeapAllocation no_gc;
  FixedArray* one_byte_cache = heap->single_character_string_cache();
  Object* undefined = heap->undefined_value();
  int i;
  WriteBarrierMode mode = elements->GetWriteBarrierMode(no_gc);
  for (i = 0; i < length; ++i) {
    Object* value = one_byte_cache->get(chars[i]);
    if (value == undefined) break;
    elements->set(i, value, mode);
  }
  if (i < length) {
    DCHECK(Smi::FromInt(0) == 0);
    memset(elements->data_start() + i, 0, kPointerSize * (length - i));
  }
#ifdef DEBUG
  for (int j = 0; j < length; ++j) {
    Object* element = elements->get(j);
    DCHECK(element == Smi::FromInt(0) ||
           (element->IsString() && String::cast(element)->LooksValid()));
  }
#endif
  return i;
}


// Converts a String to JSArray.
// For example, "foo" => ["f", "o", "o"].
RUNTIME_FUNCTION(Runtime_StringToArray) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 2);
  CONVERT_ARG_HANDLE_CHECKED(String, s, 0);
  CONVERT_NUMBER_CHECKED(uint32_t, limit, Uint32, args[1]);

  s = String::Flatten(s);
  const int length = static_cast<int>(Min<uint32_t>(s->length(), limit));

  Handle<FixedArray> elements;
  int position = 0;
  if (s->IsFlat() && s->IsOneByteRepresentation()) {
    // Try using cached chars where possible.
    elements = isolate->factory()->NewUninitializedFixedArray(length);

    DisallowHeapAllocation no_gc;
    String::FlatContent content = s->GetFlatContent();
    if (content.IsOneByte()) {
      Vector<const uint8_t> chars = content.ToOneByteVector();
      // Note, this will initialize all elements (not only the prefix)
      // to prevent GC from seeing partially initialized array.
      position = CopyCachedOneByteCharsToArray(isolate->heap(), chars.start(),
                                               *elements, length);
    } else {
      MemsetPointer(elements->data_start(), isolate->heap()->undefined_value(),
                    length);
    }
  } else {
    elements = isolate->factory()->NewFixedArray(length);
  }
  for (int i = position; i < length; ++i) {
    Handle<Object> str =
        isolate->factory()->LookupSingleCharacterStringFromCode(s->Get(i));
    elements->set(i, *str);
  }

#ifdef DEBUG
  for (int i = 0; i < length; ++i) {
    DCHECK(String::cast(elements->get(i))->length() == 1);
  }
#endif

  return *isolate->factory()->NewJSArrayWithElements(elements);
}


RUNTIME_FUNCTION(Runtime_StringLessThan) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  switch (String::Compare(x, y)) {
    case ComparisonResult::kLessThan:
      return isolate->heap()->true_value();
    case ComparisonResult::kEqual:
    case ComparisonResult::kGreaterThan:
      return isolate->heap()->false_value();
    case ComparisonResult::kUndefined:
      break;
  }
  UNREACHABLE();
  return Smi::FromInt(0);
}

RUNTIME_FUNCTION(Runtime_StringLessThanOrEqual) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  switch (String::Compare(x, y)) {
    case ComparisonResult::kEqual:
    case ComparisonResult::kLessThan:
      return isolate->heap()->true_value();
    case ComparisonResult::kGreaterThan:
      return isolate->heap()->false_value();
    case ComparisonResult::kUndefined:
      break;
  }
  UNREACHABLE();
  return Smi::FromInt(0);
}

RUNTIME_FUNCTION(Runtime_StringGreaterThan) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  switch (String::Compare(x, y)) {
    case ComparisonResult::kGreaterThan:
      return isolate->heap()->true_value();
    case ComparisonResult::kEqual:
    case ComparisonResult::kLessThan:
      return isolate->heap()->false_value();
    case ComparisonResult::kUndefined:
      break;
  }
  UNREACHABLE();
  return Smi::FromInt(0);
}

RUNTIME_FUNCTION(Runtime_StringGreaterThanOrEqual) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  switch (String::Compare(x, y)) {
    case ComparisonResult::kEqual:
    case ComparisonResult::kGreaterThan:
      return isolate->heap()->true_value();
    case ComparisonResult::kLessThan:
      return isolate->heap()->false_value();
    case ComparisonResult::kUndefined:
      break;
  }
  UNREACHABLE();
  return Smi::FromInt(0);
}

RUNTIME_FUNCTION(Runtime_StringEqual) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  return isolate->heap()->ToBoolean(String::Equals(x, y));
}

RUNTIME_FUNCTION(Runtime_StringNotEqual) {
  HandleScope handle_scope(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_HANDLE_CHECKED(String, x, 0);
  CONVERT_ARG_HANDLE_CHECKED(String, y, 1);
  return isolate->heap()->ToBoolean(!String::Equals(x, y));
}

RUNTIME_FUNCTION(Runtime_FlattenString) {
  HandleScope scope(isolate);
  DCHECK(args.length() == 1);
  CONVERT_ARG_HANDLE_CHECKED(String, str, 0);
  return *String::Flatten(str);
}


RUNTIME_FUNCTION(Runtime_StringCharFromCode) {
  HandleScope handlescope(isolate);
  DCHECK_EQ(1, args.length());
  if (args[0]->IsNumber()) {
    CONVERT_NUMBER_CHECKED(uint32_t, code, Uint32, args[0]);
    code &= 0xffff;
    return *isolate->factory()->LookupSingleCharacterStringFromCode(code);
  }
  return isolate->heap()->empty_string();
}

RUNTIME_FUNCTION(Runtime_ExternalStringGetChar) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  CONVERT_ARG_CHECKED(ExternalString, string, 0);
  CONVERT_INT32_ARG_CHECKED(index, 1);
  return Smi::FromInt(string->Get(index));
}

RUNTIME_FUNCTION(Runtime_StringCharCodeAt) {
  SealHandleScope shs(isolate);
  DCHECK(args.length() == 2);
  if (!args[0]->IsString()) return isolate->heap()->undefined_value();
  if (!args[1]->IsNumber()) return isolate->heap()->undefined_value();
  if (std::isinf(args.number_at(1))) return isolate->heap()->nan_value();
  return __RT_impl_Runtime_StringCharCodeAtRT(args, isolate);
}

}  // namespace internal
}  // namespace v8
