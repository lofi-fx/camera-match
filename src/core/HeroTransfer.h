#pragma once
#include "Match.h"
#include <map>
#include <mutex>
#include <optional>
#include <vector>

namespace cm {
// Named session handoff. Rendering uses each node's copied project payload.
class HeroTransfer {
public:
  void publish(const Capture &hero);
  std::optional<Capture> find(const std::string &name) const;
  std::vector<std::string> names() const;

private:
  mutable std::mutex mutex_;
  std::map<std::string, Capture> heroes_;
};
} // namespace cm
