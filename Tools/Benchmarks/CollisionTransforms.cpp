#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include <iomanip>
using namespace RTE;

struct SceneAccess : SceneMan {
    static Scene* SceneMan::* currentScene() { return &SceneAccess::m_pCurrentScene; }
};
struct TerrainFixture : SLTerrain {
    TerrainFixture() { m_WrapX = false; m_WrapY = false; }
};
struct DebrisFixture : MOSRotating {
    DebrisFixture(BITMAP* bitmap, int i) {
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
        if (warm) { auto offset = m_Rotation * Vector(1, 2); (void)offset; }
    }
    const Matrix& rotation() const { return m_Rotation; }
};
uint64_t hashValue(uint64_t hash, uint64_t v) { return (hash ^ v) * 1099511628211ULL; }
int main() {
    install_allegro(SYSTEM_NONE, &errno, atexit);
    ThreadMan::Construct();
    TimerMan::Construct();
    SettingsMan::Construct();
    SceneMan::Construct();
    MovableMan::Construct();
    LuaMan::Construct();
    Scene scene;
    scene.Create(new TerrainFixture());
    g_SceneMan.*SceneAccess::currentScene() = &scene;
    BITMAP* bitmap = create_bitmap_ex(8, 64, 64);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
        _putpixel(bitmap, x, y, ((x * 17 + y * 13) % 7 == 0 || x < y / 3) ? ColorKeys::g_MaskColor : 21);
    std::vector<std::unique_ptr<DebrisFixture>> objects;
    for (int i = 0; i < 512; ++i) objects.push_back(std::make_unique<DebrisFixture>(bitmap, i));
    uint64_t hash = 1469598103934665603ULL;
    for (int phase = 0; phase < 4; ++phase) {
        for (int i = 0; i < 512; ++i) {
            objects[i]->setRotation(i + phase * 87, phase % 2);
            Matrix before(objects[i]->rotation());
            for (int y = 203; y < 323; ++y) for (int x = 459; x < 579; ++x)
                hash = hashValue(hash, objects[i]->HitTestAtPixel(x, y, false));
            const Matrix& after = objects[i]->rotation();
            if (before.m_Rotation != after.m_Rotation || before.m_Flipped[0] != after.m_Flipped[0] || before.m_Flipped[1] != after.m_Flipped[1]) return 2;
            // Check source cache state without taking a Matrix copy (baseline copies invalidate it).
            if (after.m_ElementsUpdated != static_cast<bool>(phase % 2)) return 3;
        }
    }
    std::cout << "behavior_hash=" << hash << " checks=" << 4ULL * 512 * 120 * 120 << '\n';
    // Copies and vector/matrix const operators for dirty and clean matrices.
    uint64_t matrixHash = 1469598103934665603ULL;
    for (int i = -512; i < 512; ++i) for (int flips = 0; flips < 4; ++flips) for (int warm = 0; warm < 2; ++warm) {
        Matrix source; source.SetRadAngle(i * 0.0127F);
        source.SetXFlipped(flips & 1); source.SetYFlipped(flips & 2);
        if (warm) { source * Vector(1, 2); }
        Matrix copied(source);
        for (int j = -8; j < 8; ++j) {
            Vector input(j * 0.37F, j * -0.73F);
            Vector forward = input * copied, backward = input / copied;
            for (float f : {forward.m_X, forward.m_Y, backward.m_X, backward.m_Y}) {
                uint32_t bits; std::memcpy(&bits, &f, sizeof(bits)); matrixHash = hashValue(matrixHash, bits);
            }
        }
        if (source.m_ElementsUpdated != static_cast<bool>(warm)) return 4;
    }
    std::cout << "matrix_hash=" << matrixHash << '\n';
    for (bool warm : {false, true}) {
        for (int i = 0; i < 512; ++i) objects[i]->setRotation(i, warm);
        uint64_t hits = 0;
        for (int sample = -1; sample < 7; ++sample) {
            auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < 128; ++repeat) for (int q = 0; q < 128; ++q) for (const auto& object : objects)
                hits += object->HitTestAtPixel(496 + q % 48, 240 + q / 4, false);
            double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            if (sample >= 0) std::cout << "warm=" << warm << " sample=" << sample << " ms=" << std::fixed << std::setprecision(3) << elapsed << " queries=" << 128ULL * 128 * 512 << " hits=" << hits << '\n';
        }
    }
    std::vector<std::thread> workers;
    std::array<uint64_t, 4> threadHits{};
    for (int t = 0; t < 4; ++t) workers.emplace_back([&, t] {
        for (int repeat = 0; repeat < 32; ++repeat) for (const auto& object : objects) for (int q = 0; q < 128; ++q)
            threadHits[t] += object->HitTestAtPixel(496 + q % 48, 240 + q / 4, false);
    });
    for (auto& thread : workers) thread.join();
    if (!std::all_of(threadHits.begin(), threadHits.end(), [&](uint64_t v) { return v == threadHits[0]; })) return 5;
    std::cout << "concurrent_hits=" << threadHits[0] << " per_thread_queries=" << 32ULL * 512 * 128 << '\n';
    objects.clear();
    destroy_bitmap(bitmap);
    g_SceneMan.*SceneAccess::currentScene() = nullptr;
}
