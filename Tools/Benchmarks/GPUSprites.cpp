#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "PresetMan.h"
#include "ConsoleMan.h"
#include "FrameMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include "GLResourceMan.h"
#include "BigTexture.h"
#include "Draw.h"
#include "System.h"
#include "raylib/rlgl.h"
#include "glad/gl.h"
#include <SDL3/SDL.h>
#include <iomanip>
using namespace RTE;

constexpr int side = 512;
constexpr int background = 11;
struct SceneAccess : SceneMan {
    static Scene* SceneMan::* currentScene() { return &SceneAccess::m_pCurrentScene; }
};
struct TerrainFixture : SLTerrain {
    TerrainFixture(const std::string& path) {
        m_BitmapFile = ContentFile(path.c_str()); m_WrapX = false; m_WrapY = false; Create();
    }
};
static_assert(sizeof(TerrainFixture) == sizeof(SLTerrain));
struct SpriteFixture : MOSRotating {
    SpriteFixture(BITMAP* source, int i) {
        m_aSprite = {source}; m_FrameCount = 1;
        m_pFlipBitmap = create_bitmap_ex(8, source->w, source->h);
        m_pTempBitmap = create_bitmap_ex(8, source->w * 2, source->h * 2);
        m_SpriteOffset = Vector(-source->w * 0.5F + 0.25F, -source->h * 0.5F - 0.75F);
        m_SpriteRadius = std::hypot(source->w, source->h); m_SpriteDiameter = m_SpriteRadius * 2;
        configure(i % 2, i * 0.137F, i % 3 == 0 ? 0.5F : i % 3 == 1 ? 1.0F : 1.7F,
                  Vector(48 + i % 8 * 56, 48 + i / 8 % 8 * 56));
    }
    ~SpriteFixture() override { destroy_bitmap(m_pTempBitmap); m_pTempBitmap = nullptr; }
    void configure(bool flip, float angle, float scale, Vector position) {
        m_HFlipped = flip; m_Rotation.SetRadAngle(angle); m_Scale = scale; m_Pos = position;
    }
    Vector pivot() const {
        return Vector(m_HFlipped ? m_aSprite[0]->w + m_SpriteOffset.GetFloorIntX() : -m_SpriteOffset.GetFloorIntX(),
                      -m_SpriteOffset.GetFloorIntY());
    }
    void drawGPU() const {
        BITMAP* source = m_aSprite[0];
        Vector origin = pivot() * m_Scale;
        Vector position = m_Pos.GetRounded();
        DrawTexturePro(source, {0, 0, float(source->w) * (m_HFlipped ? -1 : 1), float(source->h)},
                       {position.m_X, position.m_Y, source->w * m_Scale, source->h * m_Scale},
                       origin, m_Rotation.GetRadAngle(), {255, 255, 255, 255});
    }
    // Independent inverse transform at pixel centers. This is a float raster oracle,
    // not a replacement for Allegro's fixed-point scan conversion.
    void drawOracle(std::vector<unsigned char>& pixels, std::vector<unsigned char>& boundaries) const {
        BITMAP* source = m_aSprite[0];
        Vector origin = pivot(), position = m_Pos.GetRounded();
        float c = std::cos(m_Rotation.GetRadAngle()), s = std::sin(m_Rotation.GetRadAngle());
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            float dx = x + 0.5F - position.m_X, dy = y + 0.5F - position.m_Y;
            float u = (dx * c - dy * s) / m_Scale + origin.m_X;
            float v = (dx * s + dy * c) / m_Scale + origin.m_Y;
            if (u >= -0.001F && u <= source->w + 0.001F && v >= -0.001F && v <= source->h + 0.001F &&
                (std::abs(u - std::round(u)) < 0.001F || std::abs(v - std::round(v)) < 0.001F))
                boundaries[y * side + x] = 1;
            if (u >= 0 && u < source->w && v >= 0 && v < source->h) {
                int sx = m_HFlipped ? source->w - 1 - int(u) : int(u);
                int color = source->line[int(v)][sx];
                if (color) pixels[y * side + x] = color;
            }
        }
    }
};
static_assert(sizeof(SpriteFixture) == sizeof(MOSRotating));
BITMAP* pattern(int size, int seed) {
    BITMAP* bitmap = create_bitmap_ex(8, size, size);
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x)
        bitmap->line[y][x] = (x * 17 + y * 13 + seed) % 7 == 0 || x < y / 3 ? 0 : 20 + (x + y + seed) % 200;
    return bitmap;
}
double elapsed(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void clearGPU() {
    glClearColor(background / 255.0F, background / 255.0F, background / 255.0F, 1);
    glClear(GL_COLOR_BUFFER_BIT);
}
std::vector<unsigned char> readGPU() {
    std::vector<unsigned char> rgba(side * side * 4), pixels(side * side);
    glReadPixels(0, 0, side, side, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
        pixels[y * side + x] = rgba[((side - y - 1) * side + x) * 4];
    return pixels;
}
uint64_t hash(const std::vector<unsigned char>& pixels) {
    uint64_t result = 1469598103934665603ULL;
    for (unsigned char pixel : pixels) result = (result ^ pixel) * 1099511628211ULL;
    return result;
}
void compare(const std::string& name, BITMAP* cpu, const std::vector<unsigned char>& oracle, const std::vector<unsigned char>& boundaries) {
    auto gpu = readGPU();
    int mismatch = 0, maskMismatch = 0, oracleMismatch = 0, stableMismatch = 0, cpuPixels = 0, gpuPixels = 0;
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        int offset = y * side + x, c = cpu->line[y][x], g = gpu[offset];
        mismatch += c != g; maskMismatch += (c == background) != (g == background);
        oracleMismatch += oracle[offset] != g;
        stableMismatch += oracle[offset] != g && !boundaries[offset];
        cpuPixels += c != background; gpuPixels += g != background;
    }
    std::cout << "correctness case=" << name << " cpu_gpu_diff=" << mismatch << " mask_diff=" << maskMismatch
              << " float_oracle_gpu_diff=" << oracleMismatch << " nonboundary_oracle_gpu_diff=" << stableMismatch << " cpu_pixels=" << cpuPixels
              << " gpu_pixels=" << gpuPixels << " gpu_hash=" << hash(gpu) << '\n';
}
int main() {
    std::cout << std::unitbuf << std::fixed << std::setprecision(6);
    install_allegro(SYSTEM_NONE, &errno, atexit);
    System::EnableFilePathCaseSensitivity(false);
    ThreadMan::Construct(); TimerMan::Construct(); SettingsMan::Construct(); PresetMan::Construct();
    SceneMan::Construct(); MovableMan::Construct(); LuaMan::Construct(); ConsoleMan::Construct(); FrameMan::Construct();
    GLResourceMan::Construct();
    SDL_Init(SDL_INIT_VIDEO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("GPU sprite experiment", side, side, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress); rlLoadExtensions((void*)SDL_GL_GetProcAddress);
    rlglInit(side, side); rlViewport(0, 0, side, side);
    rlMatrixMode(RL_PROJECTION); rlLoadIdentity(); rlOrtho(0, side, side, 0, -1, 1);
    rlMatrixMode(RL_MODELVIEW); rlLoadIdentity();
    std::cout << "renderer=" << glGetString(GL_RENDERER) << " version=" << glGetString(GL_VERSION) << '\n';
    GLint subpixelBits = 0; glGetIntegerv(GL_SUBPIXEL_BITS, &subpixelBits);
    std::cout << "raster_subpixel_bits=" << subpixelBits << '\n';
    const char* fragment = R"(#version 330 core
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
void main() {
    float index = texture(texture0, fragTexCoord).r;
    if (index < 0.5 / 255.0) discard;
    finalColor = vec4(index, index, index, 1.0);
})";
    unsigned int shader = rlLoadShaderCode(nullptr, fragment);
    int locations[RL_SHADER_LOC_COUNT];
    std::fill(std::begin(locations), std::end(locations), -1);
    locations[RL_SHADER_LOC_MATRIX_MVP] = rlGetLocationUniform(shader, "mvp");
    locations[RL_SHADER_LOC_MAP_DIFFUSE] = rlGetLocationUniform(shader, "texture0");
    rlSetShader(shader, locations);
    glDisable(GL_BLEND); glDisable(GL_DITHER);
    PALETTE palette{};
    std::string fixturePath = "Mods/GPUSpritesBenchmark-" + std::to_string(getpid());
    std::filesystem::create_directories(fixturePath);
    std::string terrainPath = fixturePath + "/terrain.bmp";
    BITMAP* terrain = pattern(side, 0); save_bitmap(terrainPath.c_str(), terrain, palette); destroy_bitmap(terrain);
    Scene scene; scene.Create(new TerrainFixture(terrainPath));
    g_SceneMan.*SceneAccess::currentScene() = &scene;
    BITMAP* target = create_bitmap_ex(8, side, side);
    {
        BITMAP* source = pattern(24, 0);
        SpriteFixture sprite(source, 0);
        for (bool flip : {false, true}) for (float angle : {0.0F, 0.13F, -1.27F, 3.14159265F})
            for (float scale : {0.5F, 1.0F, 1.7F}) {
                sprite.configure(flip, angle, scale, Vector(200.75F, 201.25F));
                clear_to_color(target, background); sprite.Draw(target, Vector(), g_DrawColor, true);
                std::vector<unsigned char> oracle(side * side, background), boundaries(side * side); sprite.drawOracle(oracle, boundaries);
                clearGPU(); sprite.drawGPU(); rlDrawRenderBatchActive(); glFinish();
                compare("flip" + std::to_string(flip) + "_a" + std::to_string(angle) + "_s" + std::to_string(scale), target, oracle, boundaries);
            }
        SpriteFixture second(source, 0);
        sprite.configure(true, 0.27F, 1.7F, Vector(197, 198));
        second.configure(false, -0.62F, 0.5F, Vector(201, 204));
        clear_to_color(target, background); sprite.Draw(target, Vector(), g_DrawColor, true); second.Draw(target, Vector(), g_DrawColor, true);
        std::vector<unsigned char> oracle(side * side, background), boundaries(side * side); sprite.drawOracle(oracle, boundaries); second.drawOracle(oracle, boundaries);
        clearGPU(); sprite.drawGPU(); second.drawGPU(); rlDrawRenderBatchActive(); glFinish(); compare("ordered_overlap", target, oracle, boundaries);
        destroy_bitmap(source);
    }
    {
        BigTexture uploaded(target);
        int cases = 0;
        for (auto [size, unique] : {std::pair{12, 1}, {24, 8}, {64, 8}, {128, 8}, {24, 512}, {64, 512}, {-1, 1}, {-2, 1}}) {
            std::vector<BITMAP*> sources;
            std::string name = size > 0 ? "pattern" : size == -1 ? "weapon_gib" : "rocket_hull";
            for (int i = 0; i < unique; ++i) sources.push_back(size > 0 ? pattern(size, i) :
                ContentFile(size == -1 ? "Base.rte/Devices/Shared/Gibs/WeaponGibA.png" : "Base.rte/Craft/Rockets/MK2/Gibs/RocketAHullGibBigA.png").GetAsBitmap());
            std::vector<std::unique_ptr<SpriteFixture>> objects;
            for (int i = 0; i < 512; ++i) objects.push_back(std::make_unique<SpriteFixture>(sources[i % unique], i));
            auto cpu = [&] { clear_to_color(target, background); for (auto& object : objects) object->Draw(target, Vector(), g_DrawColor, true); };
            auto gpu = [&] { clearGPU(); for (auto& object : objects) object->drawGPU(); rlDrawRenderBatchActive(); glFinish(); };
            auto cpuUpload = [&] { cpu(); uploaded.Update(Box(Vector(), side, side)); clearGPU(); uploaded.Draw({0, 0, side, side}, {0, 0, side, side}); rlDrawRenderBatchActive(); glFinish(); };
            glFinish(); auto start = std::chrono::steady_clock::now(); cpu(); double coldCPU = elapsed(start);
            glFinish(); start = std::chrono::steady_clock::now(); gpu(); double coldGPU = elapsed(start);
            std::cout << "cold case=" << cases << " name=" << name << " width=" << sources[0]->w << " height=" << sources[0]->h
                      << " unique=" << unique << " draws=512 cpu_ms=" << coldCPU << " gpu_upload_draw_finish_ms=" << coldGPU << '\n';
            cpuUpload(); gpu();
            for (int sample = 0; sample < 7; ++sample) {
                // Rotate measurement order to reduce systematic temperature bias.
                double times[3];
                for (int order = 0; order < 3; ++order) {
                    int mode = (sample + order) % 3; glFinish(); start = std::chrono::steady_clock::now();
                    for (int repeat = 0; repeat < 8; ++repeat) {
                        if (mode == 0) cpu(); else if (mode == 1) gpu(); else cpuUpload();
                    }
                    times[mode] = elapsed(start) / 8;
                }
                std::cout << "steady case=" << cases << " sample=" << sample << " draws=512 cpu_ms=" << times[0]
                          << " gpu_draw_finish_ms=" << times[1] << " cpu_upload_draw_finish_ms=" << times[2] << '\n';
            }
            // Read actual results after timing, outside the timing interval.
            cpu(); std::vector<unsigned char> oracle(side * side, background), boundaries(side * side);
            for (auto& object : objects) object->drawOracle(oracle, boundaries);
            gpu(); compare(name + "_batch_" + std::to_string(cases), target, oracle, boundaries);
            cpuUpload();
            for (int y = 0; y < side; ++y) std::memcpy(oracle.data() + y * side, target->line[y], side);
            std::fill(boundaries.begin(), boundaries.end(), 0);
            compare("cpu_upload_display_" + std::to_string(cases), target, oracle, boundaries);
            objects.clear();
            if (size > 0) for (BITMAP* source : sources) destroy_bitmap(source);
            ++cases;
        }
        for (auto buffer : uploaded.m_UploadBuffers) glDeleteBuffers(1, &buffer);
    }
    std::cout << "final_gl_error=" << glGetError() << '\n';
    g_GLResourceMan.Destroy(); rlUnloadShaderProgram(shader); rlglClose();
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    ContentFile::FreeAllLoaded(); destroy_bitmap(target);
    g_SceneMan.*SceneAccess::currentScene() = nullptr;
    std::filesystem::remove_all(fixturePath);
}
