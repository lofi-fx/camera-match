#pragma once
#include "Match.h"
#include <string>
namespace cm {
struct Persistent {
  bool hasHero = false, hasTarget = false;
  Capture hero{}, target{};
  Solution solution{};
};
std::string serialize(const Persistent &s);
bool deserialize(const std::string &text, Persistent &out);
uint64_t fingerprint(const Capture &c);
} // namespace cm
