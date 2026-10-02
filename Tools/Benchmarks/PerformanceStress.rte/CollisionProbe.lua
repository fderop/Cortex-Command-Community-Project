function Create(self)
    self:SetNumberValue("probe_create", self:GetNumberValue("probe_create") + 1)
end
function OnCollideWithMO(self, other, rootMO)
    assert(other ~= nil and rootMO ~= nil, "Missing collision arguments")
    self:SetNumberValue("probe_mo", self:GetNumberValue("probe_mo") + 1)
end
function OnCollideWithTerrain(self, materialID)
    assert(type(materialID) == "number" and materialID > 0, "Invalid terrain argument")
    self:SetNumberValue("probe_terrain", self:GetNumberValue("probe_terrain") + 1)
end
