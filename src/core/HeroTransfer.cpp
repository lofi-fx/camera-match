#include "HeroTransfer.h"

namespace cm {
void HeroTransfer::publish(const Capture &hero) {
  std::lock_guard<std::mutex> lock(mutex_);
  latest_ = hero;
}

std::optional<Capture> HeroTransfer::latest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return latest_;
}
} // namespace cm
