#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "ConsoleMan.h"
#include "FrameMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include "System.h"
#include "Attachable.h"
#include <iomanip>

using namespace RTE;

struct SceneAccess : SceneMan {
    static Scene* SceneMan::* currentScene() { return &SceneAccess::m_pCurrentScene; }
};
struct TerrainFixture : SLTerrain {
    explicit TerrainFixture(const std::string& path) {
        m_BitmapFile = ContentFile(path.c_str());
        m_WrapX = true;
        m_WrapY = false;
        Create();
    }
};
Material fixtureMaterial;
template<class Base> struct SpriteFixture : Base {
    explicit SpriteFixture(BITMAP* frame) {
        this->m_aSprite = {frame};
        this->m_FrameCount = 1;
        this->m_pFlipBitmap = create_bitmap_ex(8, frame->w, frame->h);
        this->m_pTempBitmap = create_bitmap_ex(8, frame->w * 2, frame->h * 2);
        this->m_SpriteOffset = Vector(-frame->w * 0.5F + 0.25F, -frame->h * 0.5F - 0.75F);
        this->m_SpriteRadius = std::hypot(frame->w * 0.5F + 0.25F, frame->h * 0.5F + 0.75F);
        this->m_SpriteDiameter = this->m_SpriteRadius * 2;
        this->m_HFlipped = false;
    }
    ~SpriteFixture() override {
        destroy_bitmap(this->m_pTempBitmap);
        this->m_pTempBitmap = nullptr;
    }
    const Material* GetMaterial() const override { return &fixtureMaterial; }
    void configure(Vector position, bool flip = false, float angle = 0, float scale = 1) {
        this->m_Pos = position;
        this->m_HFlipped = flip;
        this->m_Rotation.SetRadAngle(angle);
        this->m_Scale = scale;
    }
};
using Sprite = SpriteFixture<MOSRotating>;
using Child = SpriteFixture<Attachable>;
static_assert(sizeof(Sprite) == sizeof(MOSRotating));
static_assert(sizeof(Child) == sizeof(Attachable));

struct View { Vector offset; int width = 256; int height = 256; };

// Test-only candidate: skip the whole object if its radius misses every current view.
bool visible(const MOSRotating& object, const std::vector<View>& views, bool wrap = true, bool bodyOnly = false) {
    float radius = (bodyOnly ? object.MOSprite::GetRadius() : object.GetRadius()) * std::max(1.0F, object.GetScale()) + 2;
    Vector position = object.GetPos();
    for (const View& view : views) {
        for (int copy = wrap ? -1 : 0; copy <= (wrap ? 1 : 0); ++copy) {
            float x = position.m_X + copy * g_SceneMan.GetSceneWidth();
            if (x + radius >= view.offset.m_X && x - radius < view.offset.m_X + view.width &&
                position.m_Y + radius >= view.offset.m_Y && position.m_Y - radius < view.offset.m_Y + view.height) return true;
        }
    }
    return false;
}
void drawObjects(const std::vector<Sprite*>& objects, BITMAP* target, const std::vector<View>& views, bool cull,
                 bool wrap = true, bool bodyOnly = false) {
    for (const Sprite* object : objects) {
        if (!cull || visible(*object, views, wrap, bodyOnly)) object->Draw(target, Vector(), g_DrawColor, true);
    }
}
uint64_t hashBitmap(BITMAP* bitmap) {
    uint64_t hash = 1469598103934665603ULL;
    for (int y = 0; y < bitmap->h; ++y) for (int x = 0; x < bitmap->w; ++x)
        hash = (hash ^ bitmap->line[y][x]) * 1099511628211ULL;
    return hash;
}
BITMAP* readView(BITMAP* scene, const View& view) {
    BITMAP* result = create_bitmap_ex(8, view.width, view.height);
    for (int y = 0; y < view.height; ++y) for (int x = 0; x < view.width; ++x) {
        int worldX = (x + view.offset.GetFloorIntX() + scene->w) % scene->w;
        _putpixel(result, x, y, getpixel(scene, worldX, y + view.offset.GetFloorIntY()));
    }
    return result;
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf << std::fixed << std::setprecision(6);
    install_allegro(SYSTEM_NONE, &errno, atexit);
    System::EnableFilePathCaseSensitivity(false);
    ThreadMan::Construct(); TimerMan::Construct(); SettingsMan::Construct(); PresetMan::Construct();
    SceneMan::Construct(); MovableMan::Construct(); LuaMan::Construct(); ConsoleMan::Construct(); FrameMan::Construct();
    fixtureMaterial.SetIndex(37);
    PALETTE palette{};
    for (int i = 0; i < 256; ++i) palette[i].r = palette[i].g = palette[i].b = i % 64;
    std::string directory = "Mods/CullingBenchmark-" + std::to_string(getpid());
    std::filesystem::create_directories(directory);
    auto loadBitmap = [&](int width, int height, const std::string& name) {
        std::string path = directory + "/" + name + ".bmp";
        BITMAP* bitmap = create_bitmap_ex(8, width, height);
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
            _putpixel(bitmap, x, y, (x * 17 + y * 13) % 7 == 0 ? 0 : 20 + (x + y) % 200);
        save_bitmap(path.c_str(), bitmap, palette);
        destroy_bitmap(bitmap);
        return ContentFile(path.c_str()).GetAsBitmap();
    };
    loadBitmap(2048, 512, "terrain");
    Scene scene;
    scene.Create(new TerrainFixture(directory + "/terrain.bmp"));
    g_SceneMan.*SceneAccess::currentScene() = &scene;
    BITMAP* reference = create_bitmap_ex(8, 2048, 512);
    BITMAP* candidate = create_bitmap_ex(8, 2048, 512);
    BITMAP* frame = loadBitmap(24, 20, "sprite");
    int failures = 0;
    auto compare = [&](const std::string& name, const std::vector<Sprite*>& objects, const std::vector<View>& oldViews,
                       const View& finalView, bool expectEqual, bool wrap = true, bool bodyOnly = false) {
        clear_to_color(reference, 0); clear_to_color(candidate, 0);
        drawObjects(objects, reference, oldViews, false);
        drawObjects(objects, candidate, oldViews, true, wrap, bodyOnly);
        BITMAP* expected = readView(reference, finalView);
        BITMAP* actual = readView(candidate, finalView);
        int different = 0, referencePixels = 0;
        for (int y = 0; y < expected->h; ++y) for (int x = 0; x < expected->w; ++x) {
            different += expected->line[y][x] != actual->line[y][x];
            referencePixels += expected->line[y][x] != 0;
        }
        failures += ((different == 0) != expectEqual) || referencePixels == 0;
        std::cout << "mode=correctness case=" << name << " different_pixels=" << different
                  << " reference_pixels=" << referencePixels << " expected_equal=" << expectEqual
                  << " reference_hash=" << hashBitmap(expected) << " candidate_hash=" << hashBitmap(actual) << '\n';
        if (different) {
            save_bitmap((directory + "/" + name + "-baseline.bmp").c_str(), expected, palette);
            save_bitmap((directory + "/" + name + "-culled.bmp").c_str(), actual, palette);
        }
        destroy_bitmap(expected); destroy_bitmap(actual);
    };
    if (std::string(argv[1]) == "correctness") {
        Sprite sprite(frame);
        int index = 0;
        for (bool flip : {false, true}) for (float angle : {0.0F, 0.37F, -1.27F})
            for (float scale : {0.5F, 1.0F, 1.7F}) for (Vector position : {Vector(4, 100), Vector(252, 100), Vector(2044, 100)}) {
                sprite.configure(position, flip, angle, scale);
                compare("stationary" + std::to_string(index++), {&sprite}, {{Vector()}}, {Vector()}, true);
            }
        sprite.configure(Vector(4, 100));
        compare("scene_seam", {&sprite}, {{Vector(1920, 0)}}, {Vector(1920, 0)}, true);
        compare("scene_seam_without_wrap", {&sprite}, {{Vector(1920, 0)}}, {Vector(1920, 0)}, false, false);
        sprite.configure(Vector(290, 100));
        compare("camera_pan", {&sprite}, {{Vector()}}, {Vector(64, 0)}, false);
        sprite.configure(Vector(1200, 100));
        compare("camera_jump", {&sprite}, {{Vector()}}, {Vector(1088, 0)}, false);
        compare("split_screen", {&sprite}, {{Vector()}, {Vector(1088, 0)}}, {Vector(1088, 0)}, true);
        compare("split_screen_first_only", {&sprite}, {{Vector()}}, {Vector(1088, 0)}, false);
        sprite.configure(Vector(360, 100));
        auto* child = new Child(frame);
        sprite.AddAttachable(child, Vector(-140, 0));
        std::cout << "mode=attachment radius=" << sprite.GetRadius() << " child_x=" << child->GetPos().m_X << '\n';
        for (bool after : {false, true}) {
            child->SetDrawnAfterParent(after);
            compare(after ? "attachment_cached_radius_after" : "attachment_cached_radius_before", {&sprite}, {{Vector()}}, {Vector()}, false);
            compare(after ? "body_bound_after" : "body_bound_before", {&sprite}, {{Vector()}}, {Vector()}, false, true, true);
        }
        std::cout << "mode=result failures=" << failures << " image_directory=" << directory << '\n';
    } else {
        bool cull = std::string(argv[1]) == "culled";
        int repeats = std::stoi(argv[2]);
        for (int size : {12, 64, 128}) for (bool spread : {false, true}) {
            BITMAP* source = loadBitmap(size, size, "size" + std::to_string(size));
            std::vector<std::unique_ptr<Sprite>> owners;
            std::vector<Sprite*> objects;
            int selected = 0;
            for (int i = 0; i < 512; ++i) {
                auto object = std::make_unique<Sprite>(source);
                Vector position(spread ? 128 + i % 8 * 256 : 64 + i % 8 * 16, 64 + i / 8 % 8 * 16);
                object->configure(position, i % 2, i * 0.137F);
                selected += visible(*object, {{Vector()}});
                objects.push_back(object.get()); owners.push_back(std::move(object));
            }
            clear_to_color(candidate, 0);
            drawObjects(objects, candidate, {{Vector()}}, cull);
            auto start = std::chrono::steady_clock::now();
            for (int repeat = 0; repeat < repeats; ++repeat) drawObjects(objects, candidate, {{Vector()}}, cull);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            BITMAP* viewport = readView(candidate, {Vector()});
            std::cout << "mode=timing size=" << size << " spread=" << spread << " variant=" << argv[1]
                      << " objects=512 selected=" << selected << " repeats=" << repeats << " ms=" << ms
                      << " viewport_hash=" << hashBitmap(viewport) << " scene_hash=" << hashBitmap(candidate) << '\n';
            destroy_bitmap(viewport);
        }
        std::filesystem::remove_all(directory);
    }
    ContentFile::FreeAllLoaded();
    destroy_bitmap(reference); destroy_bitmap(candidate);
    g_SceneMan.*SceneAccess::currentScene() = nullptr;
    return failures != 0;
}
