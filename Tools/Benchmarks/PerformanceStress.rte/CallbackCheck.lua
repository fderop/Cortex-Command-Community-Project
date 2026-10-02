local probeScript = "PerformanceStress.rte/CollisionProbe.lua"
function PerformanceStress:StartActivity()
    self.ActivityState = Activity.RUNNING
    self.ticks = 0
    self.probes = {}
    self.starts = {}
    self.paths = {}
    for i = 1, 32 do
        local ground = SceneMan:MovePointToGround(Vector(SceneMan.SceneWidth / 2 + (i % 8) * 3, 0), 0, 3)
        local particle = CreateMOSRotating("Gib Metal Grey Small A", "Base.rte")
        particle.Pos = ground + Vector(0, -4 - math.floor(i / 8) * 2)
        particle.GetsHitByMOs = true
        particle.MissionCritical = true
        particle.GibImpulseLimit = 1000000000
        particle.GibWoundLimit = 1000000
        particle.RestThreshold = -1
        particle.Lifetime = 60000
        self.paths[i] = i % 4 == 0 and "PerformanceStress.rte/EmptyCollisionProbe.lua" or probeScript
        particle:AddScript(self.paths[i])
        self.probes[i] = particle
        self.starts[i] = Vector(particle.Pos.X, particle.Pos.Y)
        MovableMan:AddParticle(particle)
    end
    self.center = self.starts[1]
    print("LUA_CHECK_START")
end
local function totals(self)
    local mo = 0
    local terrain = 0
    local creates = 0
    for i, particle in ipairs(self.probes) do
        assert(MovableMan:ValidMO(particle), "Probe disappeared")
        if i % 4 == 0 then
            assert(particle:GetNumberValue("probe_mo") == 0 and particle:GetNumberValue("probe_terrain") == 0, "Missing callback ran")
        end
        mo = mo + particle:GetNumberValue("probe_mo")
        terrain = terrain + particle:GetNumberValue("probe_terrain")
        creates = creates + particle:GetNumberValue("probe_create")
    end
    return mo, terrain, creates
end
function PerformanceStress:UpdateActivity()
    self.ticks = self.ticks + 1
    CameraMan:SetScrollTarget(self.center, 1, 0)
    for i, particle in ipairs(self.probes) do
        assert(MovableMan:ValidMO(particle), "Probe disappeared")
        particle.Pos = self.starts[i]
        particle.Vel = Vector(i % 2 == 0 and 8 or -8, 20)
        particle.AngularVel = 0
        particle.RotAngle = 0
    end
    if self.ticks == 80 then
        self.disabledMO, self.disabledTerrain, self.creates = totals(self)
        assert(self.disabledMO > 0 and self.disabledTerrain > 0, "Enabled callbacks did not run")
        assert(self.creates == 32, "Create callback count mismatch")
        for i, particle in ipairs(self.probes) do particle:DisableScript(self.paths[i]) end
        print("LUA_CHECK_ENABLED mo=" .. self.disabledMO .. " terrain=" .. self.disabledTerrain .. " creates=" .. self.creates)
    elseif self.ticks == 160 then
        local mo, terrain, creates = totals(self)
        assert(mo == self.disabledMO and terrain == self.disabledTerrain, "Disabled callbacks ran")
        assert(creates == self.creates, "Create repeated while disabled")
        for i, particle in ipairs(self.probes) do particle:EnableScript(self.paths[i]) end
        print("LUA_CHECK_DISABLED_PASS")
    elseif self.ticks == 239 then
        local mo, terrain, creates = totals(self)
        assert(mo > self.disabledMO and terrain > self.disabledTerrain, "Re-enabled callbacks did not run")
        assert(creates == self.creates, "Create repeated after enable")
        print("LUA_CHECK_PASS mo=" .. mo .. " terrain=" .. terrain .. " creates=" .. creates)
        print("PERF_DONE")
    end
end
