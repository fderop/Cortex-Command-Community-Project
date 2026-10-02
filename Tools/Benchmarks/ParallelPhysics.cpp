#include "StandardIncludes.h"
#include "SLTerrain.h"
#include "SettingsMan.h"
#include "LuaMan.h"
#include "ThreadMan.h"
#include "AtomGroup.h"
#include "Material.h"
#include <condition_variable>
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
    AtomFixture(MOSRotating* owner, Material* material, int count) {
        m_OwnerMOSR = owner;
        m_Material = material;
        for (int i = 0; i < count; ++i) {
            float angle = i * 2.0F * c_PI / count;
            AddAtom(new Atom(Vector(4 * std::cos(angle), 4 * std::sin(angle)), material, owner));
        }
    }
};
uint64_t hashBits(uint64_t hash, uint64_t value) { return (hash ^ value) * 1099511628211ULL; }
uint64_t hashFloat(uint64_t hash, float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return hashBits(hash, bits);
}
struct DebrisFixture : MOSRotating {
    DebrisFixture(BITMAP* bitmap, Material* material, MOID id, int atoms, bool collisions) {
        m_aSprite = {bitmap};
        m_SpriteOffset = Vector(-4, -4);
        m_SpriteRadius = 6;
        m_MOID = m_RootMOID = id;
        m_Mass = 2.0F + (id % 7) * 0.1F;
        m_HitsMOs = m_GetsHitByMOs = collisions;
        m_pAtomGroup = new AtomFixture(this, material, atoms);
    }
    void reset(int body, int tick) {
        m_Pos = Vector(32.25F + (body % 40) * 11, 32.25F + (body % 16) * 11);
        m_Vel = Vector(3.0F + ((body + tick) % 28), ((body * 3 + tick) % 11 - 5) * 0.4F);
        m_Rotation.SetRadAngle((body % 13) * 0.05F);
        m_AngularVel = ((body + tick) % 13 - 6) * 0.5F;
        m_TravelImpulse.Reset();
        m_MOIDHit = g_NoMOID;
    }
    void travel() { m_pAtomGroup->Travel(1.0F / 60.0F); }
    uint64_t stateHash() const {
        uint64_t hash = 1469598103934665603ULL;
        for (float value : {m_Pos.m_X, m_Pos.m_Y, m_Vel.m_X, m_Vel.m_Y,
                           m_Rotation.m_Rotation, m_AngularVel, m_TravelImpulse.m_X, m_TravelImpulse.m_Y})
            hash = hashFloat(hash, value);
        hash = hashBits(hash, m_DidWrap);
        hash = hashBits(hash, m_MOIDHit);
        for (const Atom* atom : m_pAtomGroup->GetAtomList()) {
            hash = hashFloat(hash, atom->GetCurrentPos().m_X);
            hash = hashFloat(hash, atom->GetCurrentPos().m_Y);
        }
        for (const auto& impulse : m_ImpulseForces) {
            for (float value : {impulse.first.m_X, impulse.first.m_Y, impulse.second.m_X, impulse.second.m_Y})
                hash = hashFloat(hash, value);
        }
        return hashBits(hash, m_ImpulseForces.size());
    }
};
static_assert(sizeof(DebrisFixture) == sizeof(MOSRotating));

// Reused workers own fixed, disjoint body ranges. Dispatch and completion are timed.
struct Workers {
    std::mutex mutex;
    std::condition_variable workReady, completed;
    std::vector<std::thread> threads;
    const std::vector<std::unique_ptr<DebrisFixture>>& bodies;
    int epoch = 0, pending = 0, tick = 0;
    bool stop = false, empty = false;
    Workers(int count, const std::vector<std::unique_ptr<DebrisFixture>>& objects) : bodies(objects) {
        for (int worker = 0; worker < count; ++worker) {
            threads.emplace_back([this, worker, count] {
                int seen = 0;
                std::unique_lock lock(mutex);
                while (true) {
                    workReady.wait(lock, [&] { return stop || seen != epoch; });
                    if (stop) return;
                    seen = epoch;
                    int currentTick = tick;
                    bool noWork = empty;
                    lock.unlock();
                    if (!noWork) {
                        for (size_t i = bodies.size() * worker / count; i < bodies.size() * (worker + 1) / count; ++i) {
                            bodies[i]->reset(i, currentTick);
                            bodies[i]->travel();
                        }
                    }
                    lock.lock();
                    if (--pending == 0) completed.notify_one();
                }
            });
        }
    }
    void run(int currentTick, bool noWork = false) {
        std::unique_lock lock(mutex);
        tick = currentTick;
        empty = noWork;
        pending = threads.size();
        ++epoch;
        workReady.notify_all();
        completed.wait(lock, [&] { return pending == 0; });
    }
    ~Workers() {
        {
            std::lock_guard lock(mutex);
            stop = true;
        }
        workReady.notify_all();
        for (auto& thread : threads) thread.join();
    }
};

int main(int argc, char** argv) {
    int workerCount = std::stoi(argv[1]);
    int atomCount = std::stoi(argv[2]);
    int bodyCount = argc > 3 ? std::stoi(argv[3]) : 2000;
    install_allegro(SYSTEM_NONE, &errno, atexit);
    ThreadMan::Construct(); TimerMan::Construct(); SettingsMan::Construct();
    SceneMan::Construct(); MovableMan::Construct(); LuaMan::Construct(); ActivityMan::Construct();
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
    BITMAP* bitmap = create_bitmap_ex(8, 8, 8);
    clear_to_color(bitmap, 21);
    std::vector<std::unique_ptr<DebrisFixture>> bodies;
    for (int i = 0; i < bodyCount; ++i)
        bodies.push_back(std::make_unique<DebrisFixture>(bitmap, &material, i + 1, atomCount, false));
    {
        Workers workers(workerCount, bodies);
        auto run = [&](int tick) {
            if (workerCount) workers.run(tick);
            else for (int i = 0; i < bodyCount; ++i) { bodies[i]->reset(i, tick); bodies[i]->travel(); }
        };
        std::vector<uint64_t> hashes(bodyCount, 1469598103934665603ULL);
        for (int tick = 0; tick < 240; ++tick) {
            run(tick);
            for (int i = 0; i < bodyCount; ++i) hashes[i] = hashBits(hashes[i], bodies[i]->stateHash());
        }
        for (int i = 0; i < bodyCount; ++i)
            std::cout << "kind=hash body=" << i << " hash=" << hashes[i] << '\n';
        for (int sample = 0; sample < 7; ++sample) {
            for (int tick = 0; tick < 60; ++tick) run(tick);
            auto start = std::chrono::steady_clock::now();
            for (int tick = 60; tick < 240; ++tick) run(tick);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "kind=timing sample=" << sample << " bodies=" << bodyCount << " atoms=" << atomCount
                      << " workers=" << workerCount << " batches=180 ms=" << std::setprecision(9) << ms << '\n';
        }
        if (workerCount) {
            for (int i = 0; i < 60; ++i) workers.run(i, true);
            auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 2000; ++i) workers.run(i, true);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            std::cout << "kind=dispatch workers=" << workerCount << " batches=2000 ms=" << ms << '\n';
        }
    }
    bodies.clear();
    // Native response, without a CollideAtPoint override. Both schedules are serial.
    for (int reverse = 0; reverse < 2; ++reverse) {
        DebrisFixture left(bitmap, &material, 1, 16, true), right(bitmap, &material, 2, 16, true);
        left.SetPos(Vector(120.25F, 128.25F)); left.SetVel(Vector(30, 0));
        right.SetPos(Vector(136.25F, 128.25F)); right.SetVel(Vector(-10, 0));
        index = {nullptr, &left, &right};
        grid.Reset();
        grid.Add(IntRect(100, 110, 160, 150), left);
        grid.Add(IntRect(100, 110, 160, 150), right);
        for (auto* body : reverse ? std::array<DebrisFixture*, 2>{&right, &left} : std::array<DebrisFixture*, 2>{&left, &right}) {
            body->PreTravel(); body->travel(); body->MovableObject::PostTravel(); body->NewFrame();
            std::cout << "kind=collision-step order=" << (reverse ? "reverse" : "forward") << " body=" << body->GetID()
                      << " x=" << body->GetPos().m_X << " vx=" << body->GetVel().m_X
                      << " left_queue=" << left.GetImpulses().size() << " right_queue=" << right.GetImpulses().size() << '\n';
        }
        for (auto* body : {&left, &right}) {
            std::cout << "kind=collision order=" << (reverse ? "reverse" : "forward") << " body=" << body->GetID()
                      << " hash=" << body->stateHash() << " x=" << body->GetPos().m_X << " vx=" << body->GetVel().m_X
                      << " impulses=" << body->GetImpulses().size() << '\n';
            for (const auto& impulse : body->GetImpulses())
                std::cout << "kind=impulse order=" << (reverse ? "reverse" : "forward") << " body=" << body->GetID()
                          << " x=" << impulse.first.m_X << " y=" << impulse.first.m_Y << '\n';
        }
        index.clear();
    }
    g_SceneMan.*SceneAccess::scene() = nullptr;
    materials.fill(nullptr);
    destroy_bitmap(bitmap);
}
