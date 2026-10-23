#include <Arduino.h>
#include <HTTPClient.h>
#include "http_buffer.h"

// 12 KiB permiten recibir /positions/30 y la respuesta habitual de /above,
// dejando 4 KiB adicionales libres para las operaciones RSA de mbedTLS.
static constexpr size_t kHttpBufferSize = 12UL * 1024UL;
static char gHttpBuffer[kHttpBufferSize];

class FixedBufferStream : public Stream {
 public:
  void reset() {
    length_ = 0;
    overflow_ = false;
    gHttpBuffer[0] = '\0';
  }

  size_t write(uint8_t value) override {
    return write(&value, 1);
  }

  size_t write(const uint8_t* data, size_t size) override {
    const size_t available = kHttpBufferSize - 1 - length_;
    const size_t copied = min(size, available);
    if (copied > 0) {
      memcpy(gHttpBuffer + length_, data, copied);
      length_ += copied;
      gHttpBuffer[length_] = '\0';
    }
    if (copied != size) overflow_ = true;
    // Informar que se consumió la entrada para que HTTPClient pueda terminar.
    return size;
  }

  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}

  size_t length() const { return length_; }
  bool overflow() const { return overflow_; }

 private:
  size_t length_ = 0;
  bool overflow_ = false;
};

static FixedBufferStream gSink;

bool readHttpBody(
    HTTPClient& http,
    const char*& data,
    size_t& length,
    bool allowTruncated) {
  gSink.reset();
  const int written = http.writeToStream(&gSink);
  data = gHttpBuffer;
  length = gSink.length();
  return written >= 0 && length > 0 &&
         (allowTruncated || !gSink.overflow());
}

size_t getHttpBufferCapacity() {
  return kHttpBufferSize - 1;
}
