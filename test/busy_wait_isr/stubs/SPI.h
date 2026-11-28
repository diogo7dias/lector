#pragma once
#include <cstdint>

constexpr int MSBFIRST = 1;
constexpr int SPI_MODE0 = 0;

struct SPISettings {
  SPISettings() = default;
  SPISettings(uint32_t, int, int) {}
};

struct FakeSPI {
  void begin(int, int, int, int) {}
  void beginTransaction(const SPISettings&) {}
  void endTransaction() {}
  void transfer(uint8_t) {}
  void writeBytes(const uint8_t*, uint16_t) {}
};
inline FakeSPI SPI;
