#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include "AtomGroup.h"
#include "Material.h"
#include <iomanip>
#include <new>

using namespace RTE;

// Count actual C++ heap requests, including aligned allocations.
thread_local bool countAllocations = false;
thread_local uint64_t allocations = 0;
thread_local uint64_t allocationBytes = 0;
void recordAllocation(size_t size) {
    if (countAllocations) { ++allocations; allocationBytes += size; }
}
void* operator new(size_t size) { recordAllocation(size); return std::malloc(size); }
void* operator new[](size_t size) { return ::operator new(size); }
void* operator new(size_t size, std::align_val_t alignment) {
    recordAllocation(size);
    void* result;
    posix_memalign(&result, static_cast<size_t>(alignment), size);
    return result;
}
void* operator new[](size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { std::free(p); }

// Explicit template instantiation permits access to private members for fixtures.
// Install an activity without starting the graphical game or loading content files.
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
    static std::array<Material*, c_PaletteEntriesNumber> SceneMan::* materials() { return &SceneAccess::m_apMatPalette; }
};
struct MovableAccess : MovableMan {
    static std::vector<MovableObject*> MovableMan::* index() { return &MovableAccess::m_MOIDIndex; }
};
struct TerrainFixture : SLTerrain {
    TerrainFixture() {
        this->*fixtureMember(TerrainWidth{}) = 520;
        this->*fixtureMember(TerrainHeight{}) = 260;
        m_WrapX = m_WrapY = false;
        m_MainBitmap = create_bitmap_ex(8, 520, 260);
        m_MainBitmapOwned = true;
        clear_to_color(m_MainBitmap, g_MaterialAir);
    }
};
struct MaterialFixture : Material {
    MaterialFixture() {
        m_Index = 1;
        m_Integrity = 1000000.0F;
        m_Restitution = 0.65F;
        m_Friction = 0.2F;
    }
};
struct AtomFixture : AtomGroup {
    AtomFixture(MOSRotating* owner, Material* material, int atomCount) {
        m_OwnerMOSR = owner;
        m_Material = material;
        for (int i = 0; i < atomCount; ++i)
            AddAtom(new Atom(Vector(5.0F, i - atomCount / 2.0F), material, owner));
    }
};

uint64_t hashBits(uint64_t hash, uint64_t value) { return (hash ^ value) * 1099511628211ULL; }
uint64_t hashFloat(uint64_t hash, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return hashBits(hash, bits);
}
uint64_t behaviorHash = 1469598103934665603ULL;
uint64_t responses = 0;
bool recordBehavior = false;

struct DebrisFixture : MOSRotating {
    DebrisFixture(BITMAP* bitmap, Material* material, MOID id, Vector position, int atoms) {
        m_aSprite = {bitmap};
        m_SpriteOffset = Vector(0, -bitmap->h / 2.0F);
        m_SpriteRadius = 40;
        m_Pos = position;
        m_MOID = m_RootMOID = id;
        m_Mass = 2.0F + (id % 7) * 0.1F;
        m_HitsMOs = m_GetsHitByMOs = true;
        m_pAtomGroup = new AtomFixture(this, material, atoms);
    }
    bool CollideAtPoint(HitData& hit) override {
        bool result = MOSRotating::CollideAtPoint(hit);
        if (recordBehavior) {
            ++responses;
            behaviorHash = hashBits(behaviorHash, GetID());
            behaviorHash = hashBits(behaviorHash, hit.Body[HITOR]->GetID());
            for (int body : {HITOR, HITEE}) {
                behaviorHash = hashFloat(behaviorHash, hit.ResImpulse[body].m_X);
                behaviorHash = hashFloat(behaviorHash, hit.ResImpulse[body].m_Y);
                behaviorHash = hashFloat(behaviorHash, hit.HitRadius[body].m_X);
                behaviorHash = hashFloat(behaviorHash, hit.HitRadius[body].m_Y);
                behaviorHash = hashFloat(behaviorHash, hit.ImpulseFactor[body]);
            }
        }
        return result;
    }
    void reset(int tick, bool hitsMOs) {
        m_Pos = Vector(120.25F, 128.25F + (tick % 3) * 0.1F);
        m_Vel = Vector(30.0F + (tick % 7), (tick % 5) * 0.05F);
        m_Rotation.SetRadAngle((tick % 3 - 1) * 0.001F);
        m_AngularVel = (tick % 5 - 2) * 0.02F;
        m_TravelImpulse.Reset();
        m_HitsMOs = hitsMOs;
        m_MOIDHit = g_NoMOID;
    }
    void travel(bool push) {
        float remaining;
        if (push) {
            Vector impulse = m_pAtomGroup->PushTravel(m_Pos, m_Vel, 100.0F, m_DidWrap, 0.02F);
            m_TravelImpulse = impulse;
            remaining = 0;
        } else {
            remaining = m_pAtomGroup->Travel(0.02F);
        }
        if (recordBehavior) {
            for (float value : {m_Pos.m_X, m_Pos.m_Y, m_Vel.m_X, m_Vel.m_Y,
                               m_Rotation.m_Rotation, m_AngularVel, m_TravelImpulse.m_X, m_TravelImpulse.m_Y, remaining})
                behaviorHash = hashFloat(behaviorHash, value);
            behaviorHash = hashBits(behaviorHash, m_DidWrap);
        }
    }
};
static_assert(sizeof(DebrisFixture) == sizeof(MOSRotating));

int main(int argc, char** argv) {
    const int calls = argc > 1 ? std::stoi(argv[1]) : 4096;
    install_allegro(SYSTEM_NONE, &errno, atexit);
    ThreadMan::Construct();
    TimerMan::Construct();
    SettingsMan::Construct();
    SceneMan::Construct();
    MovableMan::Construct();
    LuaMan::Construct();
    ActivityMan::Construct();
    (g_ActivityMan.*fixtureMember(ActivitySlot{})).reset(new Activity());
    auto* terrain = new TerrainFixture();
    Scene scene;
    scene.Create(terrain);
    g_SceneMan.*SceneAccess::scene() = &scene;
    auto& grid = g_SceneMan.*SceneAccess::grid();
    grid.Create(520, 260, 20);
    auto& index = g_MovableMan.*MovableAccess::index();
    MaterialFixture material;
    auto& materials = g_SceneMan.*SceneAccess::materials();
    materials[0] = materials[1] = &material;
    BITMAP* moverBitmap = create_bitmap_ex(8, 8, 64);
    clear_to_color(moverBitmap, 21);
    DebrisFixture mover(moverBitmap, &material, 200, Vector(120.25F, 128.25F), 64);
    for (const std::string mode : {"dense-1", "dense-8", "dense-32", "terrain", "air", "push-terrain", "push-air"}) {
        grid.Reset();
        index.assign(201, nullptr);
        index[200] = &mover;
        const bool dense = mode.starts_with("dense");
        const bool terrainOnly = mode.ends_with("terrain");
        const bool push = mode.starts_with("push");
        const int targets = dense ? std::stoi(mode.substr(6)) : 0;
        std::vector<std::unique_ptr<DebrisFixture>> debris;
        std::vector<Vector> targetPositions;
        BITMAP* targetBitmap = create_bitmap_ex(8, 8, dense ? 64 / targets : 64);
        clear_to_color(targetBitmap, 21);
        for (int i = 0; i < targets; ++i) {
            auto object = std::make_unique<DebrisFixture>(targetBitmap, &material, i + 1,
                Vector(132, 96 + (i + 0.5F) * (64 / targets)), 8);
            index[i + 1] = object.get();
            grid.Add(IntRect(132, 96 + i * (64 / targets), 140, 96 + (i + 1) * (64 / targets) - 1), *object);
            targetPositions.push_back(object->GetPos());
            debris.push_back(std::move(object));
        }
        clear_to_color(terrain->GetMaterialBitmap(), g_MaterialAir);
        if (terrainOnly) rectfill(terrain->GetMaterialBitmap(), 132, 64, 140, 192, 1);
        auto run = [&](int count) {
            for (int i = 0; i < count; ++i) {
                for (size_t target = 0; target < debris.size(); ++target) debris[target]->SetPos(targetPositions[target]);
                mover.reset(i, !push);
                mover.travel(push);
                for (auto& target : debris) {
                    if (recordBehavior) {
                        for (const auto& impulse : target->GetImpulses()) {
                            behaviorHash = hashFloat(behaviorHash, impulse.first.m_X);
                            behaviorHash = hashFloat(behaviorHash, impulse.first.m_Y);
                            behaviorHash = hashFloat(behaviorHash, impulse.second.m_X);
                            behaviorHash = hashFloat(behaviorHash, impulse.second.m_Y);
                        }
                    }
                    target->ClearImpulseForces();
                    target->SetHitWhatMOID(g_NoMOID);
                }
            }
        };
        recordBehavior = true;
        behaviorHash = 1469598103934665603ULL;
        responses = 0;
        run(256);
        recordBehavior = false;
        std::cout << "mode=" << mode << " behavior_hash=" << behaviorHash << " responses=" << responses << '\n';
        run(512);
        // Allocation counting is separate from timing to avoid counting overhead in samples.
        allocations = allocationBytes = 0;
        countAllocations = true;
        run(calls);
        countAllocations = false;
        std::cout << "mode=" << mode << " calls=" << calls << " allocations=" << allocations << " bytes=" << allocationBytes << '\n';
        for (int sample = 0; sample < 7; ++sample) {
            auto start = std::chrono::steady_clock::now();
            run(calls);
            double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "mode=" << mode << " sample=" << sample << " calls=" << calls << " ms=" << std::fixed << std::setprecision(3) << elapsed << '\n';
        }
        debris.clear();
        destroy_bitmap(targetBitmap);
    }
    g_SceneMan.*SceneAccess::scene() = nullptr;
    materials.fill(nullptr);
    index.clear();
    destroy_bitmap(moverBitmap);
}
