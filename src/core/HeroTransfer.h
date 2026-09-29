#pragma once
#include "Match.h"
#include <mutex>
#include <optional>

namespace cm {
// A session-only handoff. Rendering uses the copied project payload, never this
// slot.
class HeroTransfer {
public:
  void publish(const Capture &hero);
  std::optional<Capture> latest() const;

private:
  mutable std::mutex mutex_;
  std::optional<Capture> latest_;
};
} // namespace cm
