static const int kBufferSize = 50;

static double ComposeDouble(char* buffer, int sign, int length, int point) {
  int k = point - length;
  // Integrate exponent into buffer.
  buffer[length] = 'e';
  snprintf(&buffer[length+1], kBufferSize - length - 1, "%d", k);
  double result;
  sscanf(buffer, "%lf", &result);  // NOLINT
  if (sign) {
    result *= -1;
  }
  return result;
}


static bool IsCorrect(double v, char* buffer, int sign, int length, int point) {
  return v == ComposeDouble(buffer, sign, length, point);
}


// The precision of long doubles is not enough to ensure the correct rounding.
static bool IsRounded(double v, char* buffer, int sign, int length, int point) {
  // We don't test when v is 0.
  if (v == 0) return true;

  // Simplify things by working with positive numbers.
  if (v < 0) v = -v;
  char correct_buffer[100];
  snprintf(correct_buffer, sizeof(correct_buffer), "%.90e", v);
  // Get rid of the '.'
  correct_buffer[1] = correct_buffer[0];
  char* correct_str = &correct_buffer[1];

  int i = 0;
  while (true) {
    if (correct_str[i] == '\0' || correct_str[i] == 'e') {
      // We should never need all digits.
      return false;
    }

    if (buffer[i] == '\0' || buffer[i] == 'e') {
      // Verify that the remaining correct digits are small enough.
      if (correct_str[i] < '5') return true;
      return false;  // For simplicity we assume that '5' is rounded up.
    }

    if (buffer[i] != correct_str[i]) {
      if (buffer[i] < correct_str[i]) return false;
      if (buffer[i] - correct_str[i] != 1) return false;
      if (correct_str[i+1] < '5') return false;
      return true;
    }

    // Both characters are equal
    i++;
  }
}


static bool IsShortest(double v,
                       char* buffer,
                       int sign,
                       int length,
                       int point) {
  // Now test if a shorter version would still yield the same result.
  // Not an exhaustive test, but better than nothing.

  if (length == 1) return true;

  char last_digit = buffer[length - 1];

  if (buffer[length - 1] == '0') return false;

  if (v == ComposeDouble(buffer, sign, length - 1, point)) {
    return false;
  }

  bool result = true;
  if (buffer[length-2] != '9') {
    buffer[length - 2]++;
    double changed_value = ComposeDouble(buffer, sign, length-1, point);
    if (v == changed_value) {
      printf("ROUNDED FAILED DEBUG: %s\n", buffer);
      result = false;
    }
    buffer[length - 2]--;
  }
  buffer[length - 1] = last_digit;
  return result;
}
