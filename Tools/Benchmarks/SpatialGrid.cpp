#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include <iomanip>

using namespace RTE;

struct ActivitySlot {
    using type = std::unique_ptr<Activity> ActivityMan::*;
    friend type fixtureMember(ActivitySlot);
};
struct TerrainWidth {
    using type = int SLTerrain::*;
    friend type fixtureMember(TerrainWidth);
};
struct TerrainHeight {
    using type = int SLTerrain::*;
    friend type fixtureMember(TerrainHeight);
};
template<class Tag, typename Tag::type Member> struct FixtureMember {
    friend typename Tag::type fixtureMember(Tag) { return Member; }
};
template struct FixtureMember<ActivitySlot, &ActivityMan::m_Activity>;
template struct FixtureMember<TerrainWidth, &SLTerrain::m_Width>;
template struct FixtureMember<TerrainHeight, &SLTerrain::m_Height>;
struct SceneAccess : SceneMan {
    static Scene* SceneMan::* scene() { return &SceneAccess::m_pCurrentScene; }
    static SpatialPartitionGrid SceneMan::* grid() { return &SceneAccess::m_MOIDsGrid; }
};
struct MovableAccess : MovableMan {
    static std::vector<MovableObject*> MovableMan::* index() { return &MovableAccess::m_MOIDIndex; }
};
struct ActivityFixture : Activity {
    void teams(bool all) {
        for (int team = 0; team < MaxTeamCount; ++team) m_TeamActive[team] = all || team == 0;
    }
};
struct TerrainFixture : SLTerrain {
    TerrainFixture(int width, int height, bool wrap) {
        this->*fixtureMember(TerrainWidth{}) = width;
        this->*fixtureMember(TerrainHeight{}) = height;
        m_WrapX = m_WrapY = wrap;
        m_MainBitmap = create_bitmap_ex(8, width, height);
        m_MainBitmapOwned = true;
        clear_to_color(m_MainBitmap, g_MaterialAir);
    }
};
struct SpriteFixture : MOSRotating {
    SpriteFixture(BITMAP* bitmap, int id, Vector pos, float scale) {
        m_aSprite = {bitmap};
        m_SpriteOffset = Vector(-4, -4);
        m_SpriteRadius = 6;
        m_Pos = pos;
        m_MOID = m_RootMOID = id;
        m_GetsHitByMOs = id % 3 != 0;
        m_Team = id % 5 - 1;
        m_IgnoresTeamHits = id % 2 == 0;
        m_Scale = scale;
        m_HFlipped = id % 2 == 0;
        m_Rotation.SetRadAngle((id % 4) * 0.2F);
    }
};
static_assert(sizeof(SpriteFixture) == sizeof(MOSRotating));
static_assert(sizeof(ActivityFixture) == sizeof(Activity));
static_assert(sizeof(TerrainFixture) == sizeof(SLTerrain));

uint64_t hash = 1469598103934665603ULL;
void record(uint64_t value) { hash = (hash ^ value) * 1099511628211ULL; }
int cellId(int x, int y, int width, int height) {
    int cx = (x / 20) % (width / 20), cy = (y / 20) % (height / 20);
    if (cx < 0) cx += width / 20;
    if (cy < 0) cy += height / 20;
    return cy * (width / 20) + cx;
}

int main(int argc, char** argv) {
    const int calls = argc > 1 ? std::stoi(argv[1]) : 300000;
    install_allegro(SYSTEM_NONE, &errno, atexit);
    ThreadMan::Construct(); TimerMan::Construct(); SettingsMan::Construct();
    SceneMan::Construct(); MovableMan::Construct(); LuaMan::Construct(); ActivityMan::Construct();
    auto* activity = new ActivityFixture();
    (g_ActivityMan.*fixtureMember(ActivitySlot{})).reset(activity);
    BITMAP* bitmap = create_bitmap_ex(8, 8, 8);
    clear_to_color(bitmap, 21);
    putpixel(bitmap, 2, 3, ColorKeys::g_MaskColor);
    auto& grid = g_SceneMan.*SceneAccess::grid();
    auto& index = g_MovableMan.*MovableAccess::index();
    for (bool wrap : {false, true}) for (bool allTeams : {false, true}) {
        const int width = wrap ? 523 : 520, height = wrap ? 267 : 260;
        Scene scene;
        scene.Create(new TerrainFixture(width, height, wrap));
        g_SceneMan.*SceneAccess::scene() = &scene;
        grid.Create(width, height, 20);
        activity->teams(true);
        grid.Reset();
        activity->teams(allTeams);
        index.assign(257, nullptr);
        std::vector<std::unique_ptr<SpriteFixture>> objects;
        std::vector<IntRect> rectangles;
        std::array<std::vector<std::vector<int>>, 5> expected, physics;
        for (int team = 0; team < 5; ++team) {
            expected[team].resize((width / 20) * (height / 20));
            physics[team].resize((width / 20) * (height / 20));
        }
        auto add = [&](const IntRect& rectangle, SpriteFixture& mo) {
            grid.Add(rectangle, mo);
            for (int team = -1; team < 4; ++team) {
                if ((team != -1 && !activity->TeamActive(team)) ||
                    (team != -1 && mo.IgnoresTeamHits() && mo.GetTeam() == team)) continue;
                for (int x = rectangle.m_Left / 20; x <= rectangle.m_Right / 20; ++x)
                    for (int y = rectangle.m_Top / 20; y <= rectangle.m_Bottom / 20; ++y) {
                        const int cell = cellId(x * 20, y * 20, width, height);
                        expected[team + 1][cell].push_back(mo.GetID());
                        if (mo.GetsHitByMOs()) physics[team + 1][cell].push_back(mo.GetID());
                    }
            }
        };
        for (int id = 1; id <= 256; ++id) {
            Vector pos(240 + (id * 17) % 40, 120 + (id * 23) % 40);
            if (id <= 8) pos = Vector(id % 2 ? 1 : width - 2, id % 3 ? 1 : height - 2);
            const float scale = id % 13 == 0 ? 0 : id % 7 == 0 ? 0.25F : id % 11 == 0 ? 2 : 1;
            auto mo = std::make_unique<SpriteFixture>(bitmap, id, pos, scale);
            const int radius = static_cast<int>(std::ceil(mo->GetRadius() * scale));
            IntRect rect(pos.GetFloorIntX() - radius, pos.GetFloorIntY() - radius,
                         pos.GetFloorIntX() + radius, pos.GetFloorIntY() + radius);
            rectangles.push_back(rect);
            add(rect, *mo);
            if (id % 17 == 0) add(rect, *mo);
            if (id == 1) add(IntRect(-1080, -540, 1080, 540), *mo);
            index[id] = mo.get(); objects.push_back(std::move(mo));
        }
        for (int x = -1041; x <= 1043; x += 7) for (int y = -521; y <= 533; y += 11)
            for (int team = -2; team <= 4; ++team) for (bool hitOnly : {false, true}) {
                const int normalizedTeam = team < -1 || team >= 4 ? -1 : team;
                const auto& actual = grid.GetMOIDsAtPosition(x, y, team, hitOnly);
                const auto& reference = (hitOnly ? physics : expected)[normalizedTeam + 1][cellId(x, y, width, height)];
                if (actual != reference) { std::cerr << "Cell contents differ\n"; return 1; }
                record(actual.size()); for (int id : actual) record(id);
            }
        for (int x = -20; x <= width + 20; ++x) for (int y = -20; y <= height + 20; y += 3)
            for (int team = -1; team < 4; ++team) record(g_SceneMan.GetMOIDPixel(x, y, team));
        std::cout << "mode=correctness wrap=" << wrap << " all_teams=" << allTeams << " behavior_hash=" << hash << '\n';
        if (allTeams && !wrap) {
            grid.Reset();
            for (int i = 0; i < 256; ++i) grid.Add(rectangles[i], *objects[i]);
            for (const std::string mode : {"lookup", "sparse-lookup", "dense-pixel", "sparse-pixel", "seam-pixel", "build", "sparse-build"}) {
                auto run = [&] {
                    uint64_t total = 0;
                    if (mode.ends_with("build")) {
                        for (int tick = 0; tick < calls / 256; ++tick) {
                            grid.Reset();
                            for (int i = 0; i < (mode == "build" ? 256 : 8); ++i) grid.Add(rectangles[i], *objects[i]);
                        }
                    } else for (int i = 0; i < calls; ++i) {
                        int x = 235 + (i * 17) % 55, y = 115 + (i * 23) % 55;
                        if (mode.starts_with("sparse")) { x = 100 + i % 55; y = 70 + i % 55; }
                        if (mode.ends_with("lookup")) total += grid.GetMOIDsAtPosition(x, y, -1, true).size();
                        else if (mode == "seam-pixel") total += g_SceneMan.GetMOIDPixel((i % 3 - 1) * width + (i % 23 - 11), (i % 3 - 1) * height + (i % 17 - 8), -1);
                        else total += g_SceneMan.GetMOIDPixel(x, y, -1);
                    }
                    return total;
                };
                run();
                for (int sample = 0; sample < 7; ++sample) {
                    const auto start = std::chrono::steady_clock::now();
                    auto total = run();
                    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                    std::cout << "mode=" << mode << " sample=" << sample << " checksum=" << total << " ms=" << std::fixed << std::setprecision(3) << ms << '\n';
                }
            }
        }
        activity->teams(true); grid.Reset();
        for (int team = -1; team < 4; ++team) for (int x = 0; x < width; x += 20) for (int y = 0; y < height; y += 20)
            if (!grid.GetMOIDsAtPosition(x, y, team, false).empty() || !grid.GetMOIDsAtPosition(x, y, team, true).empty()) return 2;
        index.clear(); objects.clear();
        g_SceneMan.*SceneAccess::scene() = nullptr;
    }
    destroy_bitmap(bitmap);
}
