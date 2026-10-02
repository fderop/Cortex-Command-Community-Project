#pragma once
#include <cstddef>
#include <cstdint>

struct KernelSprite {
    float radius, offsetX, offsetY, e00, e10, e01, e11;
    uint32_t flipX, flipY, hflip;
    int32_t width, height;
    const uint8_t* const* rows;
    uint32_t mask;
};
struct KernelQuery { const KernelSprite* sprite; float dx, dy; };
extern "C" {
uint8_t cpp_hit(const KernelSprite*, float, float);
uint8_t clang_hit(const KernelSprite*, float, float);
uint8_t rust_hit(const KernelSprite*, float, float);
void cpp_batch(const KernelQuery*, uint8_t*, size_t);
void clang_batch(const KernelQuery*, uint8_t*, size_t);
void rust_batch(const KernelQuery*, uint8_t*, size_t);
}
