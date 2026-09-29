#pragma once
#include <cstddef>
namespace cm {
struct FloatImageView {
  void *data = nullptr;
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0, rowBytes = 0, components = 0;
  float *at(int x, int y) const {
    if (!data || !rowBytes || (components != 3 && components != 4) || x < x1 ||
        x >= x2 || y < y1 || y >= y2)
      return nullptr;
    return reinterpret_cast<float *>(
        static_cast<char *>(data) + ptrdiff_t(y - y1) * rowBytes +
        ptrdiff_t(x - x1) * components * sizeof(float));
  }
};
} // namespace cm
