#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "ConsoleMan.h"
#include "FrameMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include "GLResourceMan.h"
#include "System.h"
#include "raylib/rlgl.h"
#include "glad/gl.h"
#include <SDL3/SDL.h>
#include <iomanip>
using namespace RTE;

struct SceneAccess : SceneMan {
    static Scene* SceneMan::* currentScene() { return &SceneAccess::m_pCurrentScene; }
};
struct TerrainFixture : SLTerrain {
    TerrainFixture(const std::string& path) {
        m_BitmapFile = ContentFile(path.c_str()); m_WrapX = true; m_WrapY = false; Create();
    }
};
Material fixtureMaterial;
struct SpriteFixture : MOSRotating {
    SpriteFixture(const std::vector<BITMAP*>& frames, int i) {
        m_aSprite = frames;
        m_FrameCount = frames.size();
        int width = frames[0]->w, height = frames[0]->h;
        m_pFlipBitmap = create_bitmap_ex(8, width, height);
        m_pTempBitmap = create_bitmap_ex(8, width * 2, height * 2);
        m_SpriteOffset = Vector(-width * 0.5F + 0.25F, -height * 0.5F - 0.75F);
        m_SpriteRadius = std::hypot(width, height);
        m_SpriteDiameter = m_SpriteRadius * 2;
        m_Pos = Vector(80 + i % 8 * 48, 80 + i / 8 % 8 * 48);
        m_HFlipped = true;
        m_Rotation.SetRadAngle(i * 0.137F);
    }
    ~SpriteFixture() override { destroy_bitmap(m_pTempBitmap); m_pTempBitmap = nullptr; }
    Material const* GetMaterial() const override { return &fixtureMaterial; }
    void configure(int frame, bool flip, float angle, float scale, Vector position) {
        m_Frame = frame; m_HFlipped = flip; m_Rotation.SetRadAngle(angle); m_Scale = scale; m_Pos = position;
    }
    void overwriteScratch() { clear_to_color(m_pFlipBitmap, 173); }
    void flash() { m_FlashWhiteTimer.Reset(); m_FlashWhiteTimer.SetRealTimeLimitMS(10000); }
    void stopFlash() { m_FlashWhiteTimer.SetRealTimeLimitMS(0); }
    void copySprite(const SpriteFixture& reference) { MOSprite::Create(reference); }
};
static_assert(sizeof(SpriteFixture) == sizeof(MOSRotating));
uint64_t hashBytes(uint64_t hash, const unsigned char* bytes, size_t size) {
    for (size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
    return hash;
}
uint64_t hashBitmap(uint64_t hash, BITMAP* bitmap) {
    for (int y = 0; y < bitmap->h; ++y) hash = hashBytes(hash, bitmap->line[y], bitmap->w);
    return hash;
}
BITMAP* pattern(int width, int height, int seed) {
    BITMAP* bitmap = create_bitmap_ex(8, width, height);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        _putpixel(bitmap, x, y, (x * 17 + y * 13 + seed) % 7 == 0 || x < y / 3 ? 0 : 20 + (x + y + seed) % 200);
    return bitmap;
}
double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    install_allegro(SYSTEM_NONE, &errno, atexit);
    System::EnableFilePathCaseSensitivity(false);
    ThreadMan::Construct(); TimerMan::Construct(); SettingsMan::Construct(); PresetMan::Construct();
    SceneMan::Construct(); MovableMan::Construct(); LuaMan::Construct(); ConsoleMan::Construct(); FrameMan::Construct();
    fixtureMaterial.SetIndex(37);
    PALETTE palette{};
    for (int i = 0; i < 256; ++i) palette[i].r = palette[i].g = palette[i].b = i % 64;
    std::string fixturePath = "Mods/FlippedSpritesBenchmark-" + std::to_string(getpid());
    std::filesystem::create_directories(fixturePath);
    std::vector<std::string> paths;
    auto loadPattern = [&](int width, int height, int seed) {
        std::string path = fixturePath + "/" + std::to_string(paths.size()) + ".bmp";
        BITMAP* bitmap = pattern(width, height, seed);
        save_bitmap(path.c_str(), bitmap, palette); destroy_bitmap(bitmap);
        paths.push_back(path);
        return ContentFile(path.c_str()).GetAsBitmap();
    };
    std::string terrainPath = fixturePath + "/terrain.bmp";
    BITMAP* terrainBitmap = pattern(512, 512, 0);
    save_bitmap(terrainPath.c_str(), terrainBitmap, palette); destroy_bitmap(terrainBitmap);
    Scene scene;
    scene.Create(new TerrainFixture(terrainPath));
    std::filesystem::remove(terrainPath);
    g_SceneMan.*SceneAccess::currentScene() = &scene;
    BITMAP* target = create_bitmap_ex(8, 512, 512);
    std::vector<BITMAP*> sources;
    for (int seed = 0; seed < 3; ++seed) sources.push_back(loadPattern(24 + seed % 2 * 8, 20 + seed % 2 * 8, seed));
    uint64_t hash = 1469598103934665603ULL;
    int cases = 0;
    {
        SpriteFixture sprite(sources, 0);
        for (int frame = 0; frame < 3; ++frame) for (bool flip : {false, true})
            for (float angle : {0.0F, 0.13F, -1.27F, 3.14159265F}) for (float scale : {0.5F, 1.0F, 1.7F})
                for (Vector camera : {Vector(), Vector(-50, 0), Vector(300, 0)})
                    for (DrawMode mode : {g_DrawColor, g_DrawWhite, g_DrawMaterial, g_DrawDoor, g_DrawColor}) {
                        sprite.configure(frame, flip, angle, scale, Vector(cases % 2 ? 5 : 507, 200.75F));
                        sprite.overwriteScratch();
                        clear_to_color(target, 11);
                        sprite.Draw(target, camera, mode, true);
                        hash = hashBitmap(hash, target); ++cases;
                    }
        sprite.configure(0, true, 0.23F, 1, Vector(200, 200));
        for (int edit = 0; edit < 4; ++edit) {
            if (edit) sprite.SetSpritePixelIndex(8 + edit, 10, 0, 43 + edit, -1, false);
            clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
            hash = hashBitmap(hash, target); ++cases;
        }
        sprite.SetAllSpritePixelIndexes(0, 63, 0, false);
        clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
        hash = hashBitmap(hash, target); ++cases;
        SpriteFixture clone(sources, 0);
        clone.copySprite(sprite);
        for (int edit = 0; edit < 3; ++edit) {
            sprite.SetSpritePixelIndex(11 + edit, 10, 0, 112 + edit, -1, false);
            clear_to_color(target, 11); clone.Draw(target, Vector(), g_DrawColor, true);
            hash = hashBitmap(hash, target); ++cases;
        }
        sprite.flash();
        clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
        hash = hashBitmap(hash, target); ++cases;
        sprite.stopFlash();
        clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
        hash = hashBitmap(hash, target); ++cases;
    }
    std::string path = fixturePath + "/reload.bmp";
    save_bitmap(path.c_str(), sources[0], palette);
    BITMAP* loaded = ContentFile(path.c_str()).GetAsBitmap();
    {
        SpriteFixture sprite({loaded}, 0);
        for (int reload = 0; reload < 3; ++reload) {
            if (reload) {
                BITMAP* replacement = pattern(24 + (reload == 2 ? 8 : 0), 20, 77 + reload);
                save_bitmap(path.c_str(), replacement, palette); destroy_bitmap(replacement);
                ContentFile::ReloadAllBitmaps();
                if (ContentFile(path.c_str()).GetAsBitmap() != loaded) return 2;
            }
            clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
            hash = hashBitmap(hash, target); ++cases;
        }
    }
    std::filesystem::remove(path);
    std::string memoryPath = fixturePath + "/memory.png";
    SDL_Surface* surface = SDL_CreateSurface(24, 20, SDL_PIXELFORMAT_INDEX8);
    for (int y = 0; y < 20; ++y) std::memcpy(static_cast<unsigned char*>(surface->pixels) + y * surface->pitch, sources[0]->line[y], 24);
    ContentFile::ManuallyLoadDataPNG(memoryPath, surface);
    ContentFile memoryFile(memoryPath.c_str());
    BITMAP* transferred = memoryFile.GetAsBitmap();
    {
        SpriteFixture sprite({transferred}, 0);
        clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
        hash = hashBitmap(hash, target); ++cases;
        if (memoryFile.GetAsBitmap(0, false) != transferred) return 4;
        for (int edit = 0; edit < 2; ++edit) {
            _putpixel(transferred, 5 + edit, 6, 78 + edit);
            clear_to_color(target, 11); sprite.Draw(target, Vector(), g_DrawColor, true);
            hash = hashBitmap(hash, target); ++cases;
        }
    }
    destroy_bitmap(transferred);
    std::cout << "behavior_hash=" << hash << " cases=" << cases << '\n';
    if (argc > 1 && std::string(argv[1]) == "--gpu") {
        GLResourceMan::Construct();
        g_FrameMan.SetTransTableFromPreset(TransparencyPreset::HalfTrans);
        SDL_Init(SDL_INIT_VIDEO);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_Window* window = SDL_CreateWindow("Draw benchmark", 512, 512, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        SDL_GLContext context = SDL_GL_CreateContext(window);
        gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress);
        rlLoadExtensions((void*)SDL_GL_GetProcAddress);
        rlglInit(512, 512); rlViewport(0, 0, 512, 512);
        rlMatrixMode(RL_PROJECTION); rlLoadIdentity(); rlOrtho(0, 512, 512, 0, -1, 1);
        rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
        uint64_t gpuHash = 1469598103934665603ULL;
        SpriteFixture sprite({sources[0]}, 0);
        for (bool flip : {false, true}) for (float angle : {0.0F, 0.17F, -0.95F}) {
            sprite.configure(0, flip, angle, 1, Vector(200, 200));
            glClearColor(0.17F, 0.23F, 0.31F, 1); glClear(GL_COLOR_BUFFER_BIT);
            sprite.Draw(target, Vector(), g_DrawTrans, true);
            rlDrawRenderBatchActive();
            std::vector<unsigned char> pixels(512 * 512 * 4);
            glReadPixels(0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            int drawnPixels = 0;
            for (size_t i = 0; i < pixels.size(); i += 4) drawnPixels += std::memcmp(pixels.data(), pixels.data() + i, 4) != 0;
            if (!drawnPixels) return 3;
            std::cout << "gpu_flip=" << flip << " angle=" << angle << " drawn_pixels=" << drawnPixels << '\n';
            gpuHash = hashBytes(gpuHash, pixels.data(), pixels.size());
        }
        std::cout << "gpu_hash=" << gpuHash << " cases=6\n";
        g_GLResourceMan.Destroy(); rlglClose(); SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    } else if (argc == 1) {
        for (auto [size, unique] : {std::pair{12, 1}, {24, 8}, {64, 8}, {128, 8}, {24, 512}, {64, 512}}) {
            std::vector<BITMAP*> frames;
            for (int i = 0; i < unique; ++i) frames.push_back(loadPattern(size, size, i));
            std::vector<std::unique_ptr<SpriteFixture>> objects;
            for (int i = 0; i < 512; ++i) objects.push_back(std::make_unique<SpriteFixture>(std::vector{frames[i % unique]}, i));
            auto start = std::chrono::steady_clock::now();
            for (const auto& object : objects) object->Draw(target, Vector(), g_DrawColor, true);
            std::cout << "size=" << size << " unique=" << unique << " cold_ms=" << milliseconds(start) << " draws=512\n";
            for (int sample = -1; sample < 5; ++sample) {
                start = std::chrono::steady_clock::now();
                for (int repeat = 0; repeat < 128; ++repeat) for (const auto& object : objects) object->Draw(target, Vector(), g_DrawColor, true);
                if (sample >= 0) std::cout << "size=" << size << " unique=" << unique << " sample=" << sample << " ms=" << milliseconds(start) << " draws=65536\n";
            }
            std::cout << "size=" << size << " unique=" << unique << " pixels_hash=" << hashBitmap(1469598103934665603ULL, target) << '\n';
        }
    }
    ContentFile::FreeAllLoaded();
    std::filesystem::remove_all(fixturePath);
    destroy_bitmap(target);
    g_SceneMan.*SceneAccess::currentScene() = nullptr;
}
