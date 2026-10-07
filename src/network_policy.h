#pragma once
#include <stddef.h>
#include <stdint.h>

namespace wakepolicy {
constexpr bool shouldUpdateDuckRecord(bool lookupSucceeded, bool addressMatches) {
  return lookupSucceeded && !addressMatches;
}
constexpr bool canRebasePcPrefix(uint64_t previousEsp, uint64_t currentEsp, uint64_t currentPc) {
  return previousEsp != 0 && currentEsp != 0 && previousEsp != currentEsp && currentPc == previousEsp;
}
constexpr bool isGlobalIpv6Prefix(uint16_t prefix) {
  return (prefix & 0xe000) == 0x2000;
}
constexpr bool isAlphaNumeric(char value) {
  return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9');
}
constexpr bool isLabelBody(const char *value, size_t length) {
  return length == 0 || ((isAlphaNumeric(*value) || *value == '-') && isLabelBody(value + 1, length - 1));
}
constexpr bool isDuckLabel(const char *value, size_t length) {
  return length > 0 && length <= 63 && isAlphaNumeric(value[0]) &&
         isAlphaNumeric(value[length - 1]) && isLabelBody(value, length);
}
constexpr bool isHex(char value) {
  return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}
constexpr bool isTokenBody(const char *value, size_t position) {
  return position == 36 || ((position == 8 || position == 13 || position == 18 || position == 23
    ? value[position] == '-' : isHex(value[position])) && isTokenBody(value, position + 1));
}
constexpr bool isDuckToken(const char *value, size_t length) {
  return length == 36 && isTokenBody(value, 0);
}
}