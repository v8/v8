// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/log-utils.h"

#include "src/assert-scope.h"
#include "src/base/platform/platform.h"
#include "src/objects-inl.h"
#include "src/string-stream.h"
#include "src/utils.h"
#include "src/version.h"

namespace v8 {
namespace internal {


const char* const Log::kLogToTemporaryFile = "&";
const char* const Log::kLogToConsole = "-";

// static
FILE* Log::CreateOutputHandle(const char* file_name) {
  // If we're logging anything, we need to open the log file.
  if (!Log::InitLogAtStart()) {
    return nullptr;
  } else if (strcmp(file_name, kLogToConsole) == 0) {
    return stdout;
  } else if (strcmp(file_name, kLogToTemporaryFile) == 0) {
    return base::OS::OpenTemporaryFile();
  } else {
    return base::OS::FOpen(file_name, base::OS::LogFileOpenMode);
  }
}

Log::Log(Logger* logger, const char* file_name)
    : is_stopped_(false),
      output_handle_(Log::CreateOutputHandle(file_name)),
      os_(output_handle_ == nullptr ? stdout : output_handle_),
      format_buffer_(NewArray<char>(kMessageBufferSize)),
      logger_(logger) {
  // --log-all enables all the log flags.
  if (FLAG_log_all) {
    FLAG_log_api = true;
    FLAG_log_code = true;
    FLAG_log_gc = true;
    FLAG_log_suspect = true;
    FLAG_log_handles = true;
    FLAG_log_internal_timer_events = true;
  }

  // --prof implies --log-code.
  if (FLAG_prof) FLAG_log_code = true;

  if (output_handle_ != nullptr) {
    Log::MessageBuilder msg(this);
    if (strlen(Version::GetEmbedder()) == 0) {
      msg.Append("v8-version,%d,%d,%d,%d,%d", Version::GetMajor(),
                 Version::GetMinor(), Version::GetBuild(), Version::GetPatch(),
                 Version::IsCandidate());
    } else {
      msg.Append("v8-version,%d,%d,%d,%d,%s,%d", Version::GetMajor(),
                 Version::GetMinor(), Version::GetBuild(), Version::GetPatch(),
                 Version::GetEmbedder(), Version::IsCandidate());
    }
    msg.WriteToLogFile();
  }
}

FILE* Log::Close() {
  FILE* result = nullptr;
  if (output_handle_ != nullptr) {
    if (strcmp(FLAG_logfile, kLogToTemporaryFile) != 0) {
      fclose(output_handle_);
    } else {
      result = output_handle_;
    }
  }
  output_handle_ = nullptr;

  DeleteArray(format_buffer_);
  format_buffer_ = nullptr;

  is_stopped_ = false;
  return result;
}

Log::MessageBuilder::MessageBuilder(Log* log)
    : log_(log), lock_guard_(&log_->mutex_) {
  DCHECK_NOT_NULL(log_->format_buffer_);
}


void Log::MessageBuilder::Append(const char* format, ...) {
  va_list args;
  va_start(args, format);
  AppendVA(format, args);
  va_end(args);
}


void Log::MessageBuilder::AppendVA(const char* format, va_list args) {
  Vector<char> buf(log_->format_buffer_, Log::kMessageBufferSize);
  int length = v8::internal::VSNPrintF(buf, format, args);
  // {length} is -1 if output was truncated.
  if (length == -1) {
    length = Log::kMessageBufferSize;
  }
  DCHECK_LE(length, Log::kMessageBufferSize);
  AppendStringPart(log_->format_buffer_, length);
}


void Log::MessageBuilder::AppendDoubleQuotedString(const char* string) {
  OFStream& os = log_->os_;
  // TODO(cbruni): unify escaping.
  os << '"';
  for (const char* p = string; *p != '\0'; p++) {
    if (*p == '"') os << '\\';
    os << *p;
  }
  os << '"';
}

void Log::MessageBuilder::AppendDoubleQuotedString(String* string) {
  OFStream& os = log_->os_;
  os << '"';
  // TODO(cbruni): unify escaping.
  AppendEscapedString(string);
  os << '"';
}

void Log::MessageBuilder::Append(String* string) {
  DisallowHeapAllocation no_gc;  // Ensure string stay valid.
  std::unique_ptr<char[]> characters =
      string->ToCString(DISALLOW_NULLS, ROBUST_STRING_TRAVERSAL);
  log_->os_ << characters.get();
}

void Log::MessageBuilder::AppendAddress(Address addr) {
  Append("0x%" V8PRIxPTR, reinterpret_cast<intptr_t>(addr));
}

void Log::MessageBuilder::AppendSymbolName(Symbol* symbol) {
  DCHECK(symbol);
  OFStream& os = log_->os_;
  os << "symbol(";
  if (!symbol->name()->IsUndefined(symbol->GetIsolate())) {
    os << "\"";
    AppendDetailed(String::cast(symbol->name()), false);
    os << "\" ";
  }
  os << "hash " << std::hex << symbol->Hash() << std::dec << ")";
}


void Log::MessageBuilder::AppendDetailed(String* str, bool show_impl_info) {
  if (str == nullptr) return;
  DisallowHeapAllocation no_gc;  // Ensure string stay valid.
  OFStream& os = log_->os_;
  int len = str->length();
  if (len > 0x1000) len = 0x1000;
  if (show_impl_info) {
    os << (str->IsOneByteRepresentation() ? 'a' : '2');
    if (StringShape(str).IsExternal()) os << 'e';
    if (StringShape(str).IsInternalized()) os << '#';
    os << ':' << str->length() << ':';
  }
  AppendEscapedString(str, len);
}

void Log::MessageBuilder::AppendEscapedString(String* str) {
  if (str == nullptr) return;
  int len = str->length();
  AppendEscapedString(str, len);
}

void Log::MessageBuilder::AppendEscapedString(String* str, int len) {
  DCHECK_LE(len, str->length());
  DisallowHeapAllocation no_gc;  // Ensure string stay valid.
  OFStream& os = log_->os_;
  // TODO(cbruni): unify escaping.
  for (int i = 0; i < len; i++) {
    uc32 c = str->Get(i);
    if (c >= 32 && c <= 126) {
      if (c == '\"') {
        os << "\"\"";
      } else if (c == '\\') {
        os << "\\\\";
      } else if (c == ',') {
        os << "\\,";
      } else {
        os << static_cast<char>(c);
      }
    } else if (c > 0xff) {
      Append("\\u%04x", c);
    } else {
      DCHECK(c < 32 || (c > 126 && c <= 0xff));
      Append("\\x%02x", c);
    }
  }
}

void Log::MessageBuilder::AppendStringPart(const char* str, int len) {
  log_->os_.write(str, len);
}

void Log::MessageBuilder::WriteToLogFile() { log_->os_ << std::endl; }

}  // namespace internal
}  // namespace v8
