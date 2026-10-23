#pragma once
#include <Arduino.h>

template <size_t Capacity>
class FixedText {
 public:
  FixedText() { data_[0] = '\0'; }
  FixedText(const char* value) { assign(value); }
  FixedText(const String& value) { assign(value.c_str()); }

  FixedText& operator=(const char* value) {
    assign(value);
    return *this;
  }

  FixedText& operator=(const String& value) {
    assign(value.c_str());
    return *this;
  }

  const char* c_str() const { return data_; }
  operator const char*() const { return data_; }
  size_t length() const { return strlen(data_); }
  bool empty() const { return data_[0] == '\0'; }
  size_t capacity() const { return Capacity; }

  bool append(const char* value) {
    if (value == nullptr) return true;
    const size_t before = length();
    strlcat(data_, value, Capacity);
    return before + strlen(value) < Capacity;
  }

  bool equalsIgnoreCase(const char* value) const {
    return value != nullptr && strcasecmp(data_, value) == 0;
  }

 private:
  void assign(const char* value) {
    strlcpy(data_, value != nullptr ? value : "", Capacity);
  }

  char data_[Capacity];
};
