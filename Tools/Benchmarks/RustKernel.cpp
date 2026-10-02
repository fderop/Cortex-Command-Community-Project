#include "RustKernel.h"
#include <cmath>
#ifndef KERNEL_PREFIX
#define KERNEL_PREFIX cpp
#endif
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
static inline uint8_t hit(const KernelSprite& s, float dx, float dy) {
    if (std::fma(dx, dx, dy * dy) > s.radius * s.radius) return 0;
    float x = std::fma(s.e00, dx, s.e10 * dy);
    float y = std::fma(s.e01, dx, s.e11 * dy);
    x = s.flipX ? -x : x;
    y = s.flipY ? -y : y;
    x = s.hflip ? -x : x;
    int32_t ix = static_cast<int32_t>(std::floor(x - s.offsetX));
    int32_t iy = static_cast<int32_t>(std::floor(y - s.offsetY));
    return ix >= 0 && iy >= 0 && ix < s.width && iy < s.height && s.rows[iy][ix] != s.mask;
}
extern "C" uint8_t JOIN(KERNEL_PREFIX,_hit)(const KernelSprite* s, float dx, float dy) {
    return hit(*s, dx, dy);
}
extern "C" void JOIN(KERNEL_PREFIX,_batch)(const KernelQuery* queries, uint8_t* output, size_t count) {
    for (size_t i = 0; i < count; ++i) output[i] = hit(*queries[i].sprite, queries[i].dx, queries[i].dy);
}
