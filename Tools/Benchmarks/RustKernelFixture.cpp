#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include "RustKernel.h"
#include <iomanip>
using namespace RTE;
using Hit = uint8_t (*)(const KernelSprite*, float, float);
using Batch = void (*)(const KernelQuery*, uint8_t*, size_t);
Hit selected = nullptr;
struct SceneAccess : SceneMan {
    static Scene* SceneMan::* currentScene() { return &SceneAccess::m_pCurrentScene; }
};
struct TerrainFixture : SLTerrain {
    TerrainFixture() { m_WrapX = false; m_WrapY = false; }
};
struct SpriteFixture : MOSRotating {
    SpriteFixture(BITMAP* bitmap, int i) {
        m_aSprite = {bitmap};
        m_SpriteOffset = Vector(-31.25F, -30.75F);
        m_SpriteRadius = 46.0F;
        m_Pos = Vector(512.25F + (i % 16) * 0.5F, 256.75F + (i / 16) * 0.5F);
        m_HFlipped = i % 2;
        setRotation(i, true);
    }
    void setRotation(int tick, bool warm) {
        m_Rotation.SetRadAngle(static_cast<float>((tick % 257) - 128) * 0.0317F);
        m_Rotation.SetXFlipped(tick % 3 == 0);
        m_Rotation.SetYFlipped(tick % 5 == 0);
        if (warm) m_Rotation * Vector(1, 2);
    }
    KernelSprite state() const {
        const Matrix* r = &m_Rotation;
        float e00 = r->m_Elements[0][0], e10 = r->m_Elements[1][0];
        float e01 = r->m_Elements[0][1], e11 = r->m_Elements[1][1];
        if (!r->m_ElementsUpdated) {
            e00 = e11 = std::cos(-r->m_Rotation);
            e10 = std::sin(-r->m_Rotation); e01 = -e10;
        }
        BITMAP* b = m_aSprite[m_Frame];
        return {m_SpriteRadius, m_SpriteOffset.m_X, m_SpriteOffset.m_Y,
                e00, e10, e01, e11,
                static_cast<uint32_t>(r->m_Flipped[0]), static_cast<uint32_t>(r->m_Flipped[1]),
                static_cast<uint32_t>(m_HFlipped), b->w, b->h,
                reinterpret_cast<const uint8_t* const*>(b->line), ColorKeys::g_MaskColor};
    }
    Vector distance(int x, int y) const {
        return g_SceneMan.ShortestDistance(m_Pos, Vector(static_cast<float>(x), static_cast<float>(y)));
    }
    bool HitTestAtPixel(int x, int y, bool validOnly = true) const override {
        if (!selected) return MOSprite::HitTestAtPixel(x, y, validOnly);
        if (validOnly && (!GetsHitByMOs() || GetRootParent()->GetTraveling())) return false;
        Vector d = distance(x, y);
        KernelSprite s = state();
        return selected(&s, d.m_X, d.m_Y);
    }
};
static_assert(sizeof(SpriteFixture) == sizeof(MOSRotating));
static_assert(sizeof(TerrainFixture) == sizeof(SLTerrain));
static_assert(sizeof(KernelSprite) == 64);
static_assert(sizeof(KernelQuery) == 16);
uint64_t hashValue(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }
int main(int argc, char** argv) {
    const int samples = argc > 1 ? std::stoi(argv[1]) : 7;
    install_allegro(SYSTEM_NONE, &errno, atexit);
    ThreadMan::Construct(); TimerMan::Construct(); SettingsMan::Construct();
    SceneMan::Construct(); MovableMan::Construct(); LuaMan::Construct();
    Scene scene; scene.Create(new TerrainFixture());
    g_SceneMan.*SceneAccess::currentScene() = &scene;
    BITMAP* bitmap = create_bitmap_ex(8, 64, 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
        _putpixel(bitmap, x, y, ((x * 17 + y * 13) % 7 == 0 || x < y / 3) ? ColorKeys::g_MaskColor : 21);
    std::vector<std::unique_ptr<SpriteFixture>> objects;
    for (int i = 0; i < 512; ++i) objects.push_back(std::make_unique<SpriteFixture>(bitmap, i));
    std::array<Hit, 4> hits{nullptr, cpp_hit, clang_hit, rust_hit};
    std::array<const char*, 4> names{"engine", "gcc", "clang", "rust"};
    std::array<Batch, 3> batches{cpp_batch, clang_batch, rust_batch};
    uint64_t hash = 1469598103934665603ULL, checks = 0;
    for (int phase = 0; phase < 4; ++phase) for (int i = 0; i < 512; ++i) {
        objects[i]->setRotation(i + phase * 87, phase % 2);
        for (int y = 203; y < 323; ++y) for (int x = 459; x < 579; ++x) {
            const bool reference = objects[i]->MOSprite::HitTestAtPixel(x, y, false);
            Vector d = objects[i]->distance(x, y);
            const KernelSprite s = objects[i]->state();
            for (int k = 1; k < 4; ++k) if (hits[k](&s, d.m_X, d.m_Y) != reference) {
                std::cerr << "mismatch phase=" << phase << " object=" << i << " x=" << x << " y=" << y << " mode=" << names[k] << '\n'; return 2;
            }
            hash = hashValue(hash, reference); ++checks;
        }
    }
    std::cout << "mode=correctness queries=" << checks << " behavior_hash=" << hash << '\n';
    const size_t count = 128 * 512;
    std::vector<KernelSprite> states(512);
    std::vector<KernelQuery> queries(count);
    std::vector<uint8_t> output(count), reference(count);
    const auto pack = [&] {
        for (int i = 0; i < 512; ++i) states[i] = objects[i]->state();
        for (int q = 0; q < 128; ++q) for (int i = 0; i < 512; ++i) {
            Vector d = objects[i]->distance(496 + q % 48, 240 + q / 4);
            queries[q * 512 + i] = {&states[i], d.m_X, d.m_Y};
        }
    };
    for (int i = 0; i < 512; ++i) objects[i]->setRotation(i, true);
    pack();
    for (size_t j = 0; j < count; ++j) reference[j] = cpp_hit(queries[j].sprite, queries[j].dx, queries[j].dy);
    for (Batch batch : batches) {
        batch(queries.data(), output.data(), count);
        if (output != reference) return 3;
    }
    std::cout << "mode=batch-correctness queries=" << count << " hits=" << std::accumulate(reference.begin(), reference.end(), uint64_t{0}) << '\n';
    for (int sample = -1; sample < samples; ++sample) {
        for (int turn = 0; turn < 4; ++turn) {
            int k = (turn + sample + 1) % 4; selected = hits[k];
            uint64_t total = 0;
            auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 64; ++repeat) for (int q = 0; q < 128; ++q) for (const auto& object : objects)
                total += object->HitTestAtPixel(496 + q % 48, 240 + q / 4, false);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (sample >= 0) std::cout << "mode=adapter language=" << names[k] << " sample=" << sample << " ms=" << std::fixed << std::setprecision(3) << ms << " queries=" << 64 * count << " hits=" << total << '\n';
        }
        for (bool repack : {false, true}) for (int turn = 0; turn < 3; ++turn) {
            int k = (turn + sample + 1) % 3;
            uint64_t total = 0;
            auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 64; ++repeat) {
                if (repack) pack();
                batches[k](queries.data(), output.data(), count);
                total += std::accumulate(output.begin(), output.end(), uint64_t{0});
            }
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (sample >= 0) std::cout << "mode=" << (repack ? "pack-batch" : "batch") << " language=" << names[k + 1] << " sample=" << sample << " ms=" << std::fixed << std::setprecision(3) << ms << " queries=" << 64 * count << " hits=" << total << '\n';
        }
        for (int turn = 0; turn < 3; ++turn) {
            int k = (turn + sample + 1) % 3;
            uint64_t total = 0;
            auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 64; ++repeat) for (const KernelQuery& q : queries)
                total += hits[k + 1](q.sprite, q.dx, q.dy);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (sample >= 0) std::cout << "mode=scalar language=" << names[k + 1] << " sample=" << sample << " ms=" << std::fixed << std::setprecision(3) << ms << " queries=" << 64 * count << " hits=" << total << '\n';
        }
    }
    objects.clear(); destroy_bitmap(bitmap); g_SceneMan.*SceneAccess::currentScene() = nullptr;
}
