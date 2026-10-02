function PerformanceStress:StartActivity()
    self.ActivityState = Activity.RUNNING
    self.ticks = 0
    self.timer = Timer()
    self.center = SceneMan:MovePointToGround(Vector(SceneMan.SceneWidth / 2, 0), 0, 3)
    self.center.Y = self.center.Y - 100
    PerformanceMan.ShowPerformanceStats = true
    print("PERF_START debris_waves")
end

function PerformanceStress:UpdateActivity()
    self.ticks = self.ticks + 1
    CameraMan:SetScrollTarget(self.center, 1, 0)
    if self.ticks % 60 == 1 and self.ticks <= 541 then
        for i = 1, 400 do
            local gib = CreateMOSRotating("Gib Metal Grey Small A", "Base.rte")
            gib.Pos = self.center + Vector((i * 17) % 120 - 60, (i * 23) % 80 - 40)
            gib.Vel = Vector((i * 7) % 30 - 15, -((i * 11) % 20))
            gib.AngularVel = (i % 20) - 10
            gib.HFlipped = i % 2 == 0
            gib.Lifetime = 3000
            gib.GetsHitByMOs = true
            MovableMan:AddParticle(gib)
        end
        local grenade = CreateTDExplosive("Frag Grenade", "Base.rte")
        grenade.Pos = self.center
        grenade:GibThis()
    end
    if self.ticks % 60 == 0 then
        print("PERF_SAMPLE ticks=" .. self.ticks .. " real_ms=" .. self.timer.ElapsedRealTimeMS .. " particles=" .. MovableMan:GetParticleCount())
    end
    if self.ticks == 600 then
        print("PERF_DONE real_ms=" .. self.timer.ElapsedRealTimeMS)
    end
end
