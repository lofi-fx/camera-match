#include "HeroTransfer.h"

namespace cm {
void HeroTransfer::publish(const Capture &hero) {
  std::lock_guard<std::mutex> lock(mutex_);
  heroes_[hero.name] = hero;
}

std::optional<Capture> HeroTransfer::find(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto found = heroes_.find(name);
  return found == heroes_.end() ? std::nullopt
                               : std::optional<Capture>(found->second);
}

std::vector<std::string> HeroTransfer::names() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> result;
  for (const auto &entry : heroes_)
    result.push_back(entry.first);
  return result;
}
} // namespace cm
