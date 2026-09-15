R"LUA(
-- ========== Runtime services ==========
-- RunService
do
    local rs = game:GetService("RunService")
    rs._signals = { RenderStep = signal(), Heartbeat = signal(), Stepped = signal(), RenderStepped = signal() }
    rs._renderBindings = {}
    rs._nextRenderOrder = 0
    function rs:BindToRenderStep(name, priority, fn)
        assert(type(name) == "string", "BindToRenderStep name must be a string")
        assert(type(priority) == "number", "BindToRenderStep priority must be a number")
        assert(type(fn) == "function", "BindToRenderStep callback must be a function")
        local previous = rs._renderBindings[name]
        if not previous then rs._nextRenderOrder += 1 end
        rs._renderBindings[name] = { priority = priority, fn = fn, order = previous and previous.order or rs._nextRenderOrder }
    end
    function rs:UnbindFromRenderStep(name)
        rs._renderBindings[name] = nil
    end
    function rs:_fireRenderStep(dt)
        local bindings = {}
        for name, binding in pairs(rs._renderBindings) do
            table.insert(bindings, { name = name, priority = binding.priority, fn = binding.fn, order = binding.order })
        end
        table.sort(bindings, function(a, b)
            if a.priority == b.priority then return a.order < b.order end
            return a.priority < b.priority
        end)
        for _, binding in ipairs(bindings) do binding.fn(dt) end
        rs._signals.RenderStep:Fire(dt)
    end
    function rs:Heartbeat() return rs._signals.Heartbeat end
    function rs:RenderStepped() return rs._signals.RenderStepped end
    function rs:Stepped() return rs._signals.Stepped end
    rs.Heartbeat = rs._signals.Heartbeat
    rs.RenderStepped = rs._signals.RenderStepped
    rs.Stepped = rs._signals.Stepped
    function rs:IsStudio() return false end
    function rs:IsRunning() return true end
    function rs:IsClient() return true end
    function rs:IsServer() return false end
end

-- UserInputService
do
    local uis = game:GetService("UserInputService")
    uis._signals = { InputBegan = signal(), InputChanged = signal(), InputEnded = signal(),
        JumpRequest = signal(), TextBoxFocused = signal(), TextBoxFocusReleased = signal(),
        WindowFocusReleased = signal(), WindowFocused = signal(),
        TouchStarted = signal(), TouchEnded = signal(), TouchMoved = signal() }
    uis.InputBegan = uis._signals.InputBegan
    uis.InputChanged = uis._signals.InputChanged
    uis.InputEnded = uis._signals.InputEnded
    uis.JumpRequest = uis._signals.JumpRequest
    uis.TextBoxFocused = uis._signals.TextBoxFocused
    uis.TextBoxFocusReleased = uis._signals.TextBoxFocusReleased
    uis.WindowFocusReleased = uis._signals.WindowFocusReleased
    uis.WindowFocused = uis._signals.WindowFocused
    uis.TouchStarted = uis._signals.TouchStarted
    uis.TouchEnded = uis._signals.TouchEnded
    uis.TouchMoved = uis._signals.TouchMoved
    function uis:GetMouseLocation() return Vector2.new(0, 0) end
    function uis:GetMouseDelta() return Vector2.new(0, 0) end
    function uis:IsKeyDown(key) return false end
    function uis:GetMouseButtonsPressed() return {} end
    function uis:GetFocusedTextBox() return nil end
    function uis:TouchEnabled() return false end
    function uis:MouseEnabled() return true end
    function uis:KeyboardEnabled() return true end
    function uis:GamepadEnabled() return false end
    function uis:GetMouse() return { Hit = CFrame.new(0,0,0), X = 0, Y = 0, ViewportPoint = Vector3.new(0,0,0) } end
end

-- TextService: deterministic headless text measurement used by real UI libs.
do
    local textService = game:GetService("TextService")
    function textService:GetTextSize(text, textSize, font, bounds)
        text = tostring(text or "")
        local size = tonumber(textSize) or 14
        local width = math.min(#text * size * 0.52, (bounds and bounds.X) or math.huge)
        local lines = math.max(1, math.ceil((#text * size * 0.52) / math.max((bounds and bounds.X) or math.huge, 1)))
        return Vector2.new(width, size * 1.25 * lines)
    end
end

-- Stats service values used by status bars and ping displays.
do
    local stats = game:GetService("Stats")
    local network = stats:FindFirstChild("Network") or Instance.new("Folder", stats)
    network.Name = "Network"
    local serverStats = network:FindFirstChild("ServerStatsItem") or Instance.new("Folder", network)
    serverStats.Name = "ServerStatsItem"
    local ping = serverStats:FindFirstChild("Data Ping") or Instance.new("Folder", serverStats)
    ping.Name = "Data Ping"
    ping.GetValue = function() return 0 end
    ping.GetValueString = function() return "0 ms" end
    network.ServerStatsItem = serverStats
    serverStats["Data Ping"] = ping
    -- Expose the same named properties as the child hierarchy.  Scripts
    -- commonly use both Stats:WaitForChild("Network") and Stats.Network.
    stats.Network = network
    local workspaceStats = stats:FindFirstChild("Workspace") or Instance.new("Folder", stats)
    workspaceStats.Name = "Workspace"
    local heartbeat = workspaceStats:FindFirstChild("Heartbeat") or Instance.new("Folder", workspaceStats)
    heartbeat.Name = "Heartbeat"
    heartbeat.GetValueString = function() return "60" end
    workspaceStats.Heartbeat = heartbeat
    stats.Workspace = workspaceStats
end

-- Players
do
    local players = game:GetService("Players")
    players._signals = { PlayerAdded = signal(), PlayerRemoving = signal(), LocalPlayerAdded = signal() }
    players.PlayerAdded = players._signals.PlayerAdded
    players.PlayerRemoving = players._signals.PlayerRemoving
    function players:GetPlayers() return players._playerList or {} end
    function players:GetAllPlayers() return players:GetPlayers() end

    -- Deterministic local player simulation for headless compatibility tests.
    -- This never connects to Roblox or mutates an external game session.
    lumora = lumora or {}
    local function makeSimCharacter(player, position)
        local character = Instance.new("Model")
        character.Name = player.Name .. "Character"
        local root = Instance.new("Part")
        root.Name = "HumanoidRootPart"
        root.Size = Vector3.new(2, 2, 1)
        root.Position = position or Vector3.new(0, 5, 0)
        root.CFrame = CFrame.new(root.Position)
        root.Parent = character
        local head = Instance.new("Part")
        head.Name = "Head"
        head.Size = Vector3.new(2, 1, 1)
        head.Position = (position or Vector3.new(0, 5, 0)) + Vector3.new(0, 2, 0)
        head.CFrame = CFrame.new(head.Position)
        head.Parent = character
        local humanoid = _makeHumanoid and _makeHumanoid() or Instance.new("Humanoid")
        humanoid.Name = "Humanoid"
        humanoid.Parent = character
        character.Parent = player
        return character
    end

    function lumora.simulatePlayers(specs)
        specs = specs or {}
        local list = players._playerList or { players.LocalPlayer }
        for index, spec in ipairs(specs) do
            spec = spec or {}
            local simulated = Instance.new("Player")
            simulated.Name = spec.Name or ("SimPlayer" .. index)
            simulated.DisplayName = spec.DisplayName or simulated.Name
            simulated.UserId = spec.UserId or (90000000 + index)
            simulated.Team = spec.Team
            simulated.Backpack = Instance.new("Backpack", simulated)
            simulated.PlayerGui = Instance.new("PlayerGui", simulated)
            simulated.PlayerScripts = Instance.new("PlayerScripts", simulated)
            simulated._signals = { CharacterAdded = signal(), CharacterRemoving = signal(), CharacterAppearanceLoaded = signal() }
            simulated.CharacterAdded = simulated._signals.CharacterAdded
            simulated.CharacterRemoving = simulated._signals.CharacterRemoving
            simulated.CharacterAppearanceLoaded = simulated._signals.CharacterAppearanceLoaded
            function simulated:GetMouse()
                return { Hit = CFrame.new(0,0,0), X = 0, Y = 0, ViewportPoint = Vector3.new(0,0,0), Target = nil,
                    Origin = Vector3.new(0,0,0), UnitRay = Ray.new(Vector3.new(0,0,0), Vector3.new(0,0,-1)) }
            end
            simulated.Character = makeSimCharacter(simulated, spec.Position or Vector3.new(index * 6, 5, 0))
            table.insert(list, simulated)
            players._playerList = list
            players.PlayerAdded:Fire(simulated)
        end
        players._playerList = list
        return list
    end

    function lumora.resetSimulatedPlayers()
        local keep = players.LocalPlayer
        for _, player in ipairs(players._playerList or {}) do
            if player ~= keep then
                players.PlayerRemoving:Fire(player)
                player:Destroy()
            end
        end
        players._playerList = { keep }
    end
    function players:GetPlayerFromCharacter(char)
        if not char then return nil end
        for _, p in ipairs(players:GetPlayers()) do
            if p.Character == char then return p end
        end
        return nil
    end
    function players:GetPlayerByUserId(id)
        for _, p in ipairs(players:GetPlayers()) do
            if p.UserId == id then return p end
        end
        return nil
    end
    function players:GetUserThumbnailAsync(userId, thumbType, thumbSize)
        return "rbxasset://textures/ui/GuiImagePlaceholder.png", true
    end
    function players:GetCharacterAppearanceAsync(userId)
        return { andThen = function(self, fn) return fn() end }
    end
    -- Create a LocalPlayer
    local lp = Instance.new("Player")
    lp.Name = "LocalPlayer"
    lp.UserId = 12345678
    lp.Character = nil
    lp.Backpack = Instance.new("Backpack")
    lp.PlayerGui = Instance.new("PlayerGui")
    lp.PlayerScripts = Instance.new("PlayerScripts")
    lp.Backpack.Name = "Backpack"
    lp.PlayerGui.Name = "PlayerGui"
    lp.PlayerScripts.Name = "PlayerScripts"
    lp.Backpack.Parent = lp
    lp.PlayerGui.Parent = lp
    lp.PlayerScripts.Parent = lp
    -- Player signals
    lp._signals = { CharacterAdded = signal(), CharacterRemoving = signal(),
        CharacterAppearanceLoaded = signal() }
    lp.CharacterAdded = lp._signals.CharacterAdded
    lp.CharacterRemoving = lp._signals.CharacterRemoving
    lp.CharacterAppearanceLoaded = lp._signals.CharacterAppearanceLoaded
    -- Create a Character for the LocalPlayer
    local char = Instance.new("Model")
    char.Name = "LocalPlayerCharacter"
    local hrp = Instance.new("Part")
    hrp.Name = "HumanoidRootPart"
    hrp.Size = Vector3.new(2, 2, 1)
    hrp.CFrame = CFrame.new(0, 5, 0)
    hrp.Velocity = Vector3.zero
    hrp.Anchored = false
    hrp.CanCollide = true
    hrp.Parent = char
    local head = Instance.new("Part")
    head.Name = "Head"
    head.Size = Vector3.new(2, 1, 1)
    head.CFrame = CFrame.new(0, 7, 0)
    head.Parent = char
    local humanoid = _makeHumanoid and _makeHumanoid() or Instance.new("Humanoid")
    humanoid.Name = "Humanoid"
    humanoid.Parent = char
    -- Humanoid signals
    humanoid._signals = humanoid._signals or {}
    humanoid._signals.StateChanged = signal()
    humanoid.StateChanged = humanoid._signals.StateChanged
    humanoid._signals.HealthChanged = signal()
    humanoid.HealthChanged = humanoid._signals.HealthChanged
    humanoid._signals.Died = signal()
    humanoid.Died = humanoid._signals.Died
    char.Parent = lp
    lp.Character = char
    players.LocalPlayer = lp
    players._playerList = { lp }
    lp.Parent = players

    -- Extend Player metatable
    local lp2 = lp
    function lp2:GetMouse()
        return { Hit = CFrame.new(0,0,0), X = 0, Y = 0, ViewportPoint = Vector3.new(0,0,0),
                 Target = nil, Origin = Vector3.new(0,0,0), UnitRay = Ray.new(Vector3.new(0,0,0), Vector3.new(0,0,-1)) }
    end
end

-- TweenService
do
    local ts = game:GetService("TweenService")
    function ts:Create(obj, tweenInfo, properties)
        assert(typeof(obj) == "Instance" and type(properties) == "table", "TweenService:Create expects an Instance and property table")
        local tween = setmetatable({ _obj = obj, _info = tweenInfo, _props = properties,
            PlaybackState = Enum.PlaybackState.Begin.__type and Enum.PlaybackState.Begin or "Begin",
            Completed = signal(), __type = "Tween" }, {
                __index = {
                Play = function(self)
                    self.PlaybackState = Enum.PlaybackState.Playing
                    for key, value in pairs(self._props) do self._obj[key] = value end
                    self.PlaybackState = Enum.PlaybackState.Completed
                    self.Completed:Fire(self.PlaybackState)
                end,
                Pause = function(self) self.PlaybackState = Enum.PlaybackState.Paused end,
                Cancel = function(self) self.PlaybackState = Enum.PlaybackState.Cancelled end,
                Destroy = function(self) self.Completed:DisconnectAll(); self.PlaybackState = Enum.PlaybackState.Cancelled end
            }
        })
        return tween
    end
end

-- HttpService
do
    local hs = game:GetService("HttpService")
    -- JSONEncode/JSONDecode are replaced by the native codec after the
    -- prelude is installed; these fallbacks are intentionally not lossy.
    function hs:GenerateGUID(wrapInCurlyBraces)
        local guid = string.format("%08x-%04x-%04x-%04x-%012x", math.random(0, 0xffffffff), math.random(0, 0xffff), math.random(0, 0xffff), math.random(0, 0xffff), math.random(0, 0xffffffffffff))
        if wrapInCurlyBraces == false then return guid end
        return "{" .. guid .. "}"
    end
    function hs:UrlEncode(str)
        return tostring(str or ""):gsub("([^%w%-_%.~])", function(c) return string.format("%%%02X", string.byte(c)) end)
    end
end

-- MarketplaceService
do
    local ms = game:GetService("MarketplaceService")
    function ms:GetProductInfo(assetId, infoType)
        return { AssetId = assetId, Name = "Asset", Description = "",
                 Creator = { Id = 0, Name = "Creator", Type = "User" },
                 ProductType = "Asset", Created = "2020-01-01T00:00:00Z" }
    end
    function ms:GetAssetInfo(assetId) return ms:GetProductInfo(assetId) end
end

-- ContentProvider
do
    local cp = game:GetService("ContentProvider")
    function cp:PreloadAsync(assets, callback)
        if callback then
            for _, a in ipairs(assets or {}) do callback({ Key = a, Succeeded = true, Status = "Success" }) end
        end
    end
end

-- Lighting
do
    local lighting = game:GetService("Lighting")
    lighting.ClockTime = 12
    lighting.Brightness = 2
    lighting.ExposureCompensation = 0
    lighting.FogEnd = 100000
    lighting.Ambient = Color3.new(0,0,0)
    lighting.GlobalShadows = true
    lighting.OutdoorAmbient = Color3.new(0.5, 0.5, 0.5)
end

-- ReplicatedStorage
game:GetService("ReplicatedStorage")

-- Camera setup
do
    local ws = workspace
    local cam = Instance.new("Camera")
    cam.Name = "Camera"
    cam.CFrame = CFrame.new(0, 10, 20)
    cam.Focus = CFrame.new(0, 0, 0)
    cam.FieldOfView = 70
    cam.NearPlaneSize = 0.1
    cam.FarPlane = 1000
    cam.ViewportSize = Vector2.new(1920, 1080)
    cam.CameraType = "Custom"
    ws.CurrentCamera = cam
    cam.Parent = ws

    function cam:WorldToViewportPoint(pos)
        local localPos = self.CFrame:PointToObjectSpace(pos)
        local depth = -localPos.Z
        if depth <= 0 then return Vector3.new(0, 0, depth), false end
        local aspect = self.ViewportSize.X / math.max(self.ViewportSize.Y, 1)
        local scale = math.tan(math.rad(self.FieldOfView) / 2)
        local x = self.ViewportSize.X * 0.5 + (localPos.X / (depth * scale * aspect)) * self.ViewportSize.X * 0.5
        local y = self.ViewportSize.Y * 0.5 - (localPos.Y / (depth * scale)) * self.ViewportSize.Y * 0.5
        local visible = x >= 0 and x <= self.ViewportSize.X and y >= 0 and y <= self.ViewportSize.Y
        return Vector3.new(x, y, depth), visible
    end
    function cam:ViewportPointToRay(x, y, depth)
        depth = depth or 0
        local aspect = self.ViewportSize.X / math.max(self.ViewportSize.Y, 1)
        local scale = math.tan(math.rad(self.FieldOfView) / 2)
        local nx = ((x - self.ViewportSize.X * 0.5) / (self.ViewportSize.X * 0.5)) * scale * aspect
        local ny = -((y - self.ViewportSize.Y * 0.5) / (self.ViewportSize.Y * 0.5)) * scale
        local direction = (self.CFrame:VectorToWorldSpace(Vector3.new(nx, ny, -1))).Unit
        return Ray.new(self.CFrame.Position + direction * depth, direction)
    end
    function cam:ScreenPointToRay(x, y, depth) return self:ViewportPointToRay(x, y, depth) end
    function cam:GetPartsObscuringTarget(targets, ignoreList) return {} end
    -- Deterministic headless raycast against axis-aligned BasePart bounds.
    local function _isDescendantOf(node, ancestor)
        local current = node
        while current do
            if current == ancestor then return true end
            current = current.Parent
        end
        return false
    end
    local function _rayAabb(origin, unit, maxDistance, center, size)
        local minB, maxB = center - size * 0.5, center + size * 0.5
        local tMin, tMax = 0, maxDistance
        local axes = {{origin.X, unit.X, minB.X, maxB.X}, {origin.Y, unit.Y, minB.Y, maxB.Y}, {origin.Z, unit.Z, minB.Z, maxB.Z}}
        for _, axis in ipairs(axes) do
            local o, d, lo, hi = axis[1], axis[2], axis[3], axis[4]
            if math.abs(d) < 1e-12 then
                if o < lo or o > hi then return nil end
            else
                local a, b = (lo - o) / d, (hi - o) / d
                if a > b then a, b = b, a end
                tMin, tMax = math.max(tMin, a), math.min(tMax, b)
                if tMin > tMax then return nil end
            end
        end
        local hit = origin + unit * tMin
        local delta, half = hit - center, size * 0.5
        local ax = math.abs(math.abs(delta.X) - half.X)
        local ay = math.abs(math.abs(delta.Y) - half.Y)
        local az = math.abs(math.abs(delta.Z) - half.Z)
        local normal
        if ax <= ay and ax <= az then normal = Vector3.new(delta.X >= 0 and 1 or -1, 0, 0)
        elseif ay <= az then normal = Vector3.new(0, delta.Y >= 0 and 1 or -1, 0)
        else normal = Vector3.new(0, 0, delta.Z >= 0 and 1 or -1) end
        return tMin, hit, normal
    end
    function ws:Raycast(origin, direction, params)
        local length = direction.Magnitude
        if length <= 1e-12 then return nil end
        local unit, best = direction / length, nil
        local excluded = params and params.FilterDescendantsInstances or {}
        local filterType = params and params.FilterType or "Exclude"
        local function blocked(part)
            for _, item in ipairs(excluded) do
                if _isDescendantOf(part, item) then return filterType ~= "Include" end
            end
            return filterType == "Include"
        end
        local function visit(node)
            if node:IsA("BasePart") and not blocked(node) and node.CanCollide ~= false then
                local center = node.Position or (node.CFrame and node.CFrame.Position) or Vector3.new(0, 0, 0)
                local size = node.Size or Vector3.new(1, 1, 1)
                local distance, position, normal = _rayAabb(origin, unit, length, center, size)
                if distance and (not best or distance < best.Distance) then
                    best = {Instance=node, Position=position, Distance=distance, Normal=normal, Material=node.Material or Enum.Material.Plastic, __type="RaycastResult"}
                end
            end
            for _, child in ipairs(node._children or {}) do visit(child) end
        end
        visit(self)
        return best
    end
end

-- VirtualInputManager / VirtualUser
do
    local vi = game:GetService("VirtualInputManager")
    vi.InputBegan = signal(); vi.InputChanged = signal(); vi.InputEnded = signal()
    function vi:SendKeyEvent(keyCode, key, isDown, sync)
        local input = {KeyCode=keyCode, UserInputType=Enum.UserInputType.Keyboard, State=isDown and "Begin" or "End"}
        local event = isDown and self.InputBegan or self.InputEnded; event:Fire(input)
    end
    function vi:SendMouseButtonEvent(x, y, button, isDown, sync)
        local input = {Position=Vector2.new(x,y), UserInputType=button, State=isDown and "Begin" or "End"}
        local event = isDown and self.InputBegan or self.InputEnded; event:Fire(input)
    end
    function vi:SendMouseMoveEvent(x, y, sync) self.InputChanged:Fire({Position=Vector2.new(x,y), UserInputType=Enum.UserInputType.MouseMovement}) end
    function vi:SendMouseWheelEvent(x, y, scroll, sync) self.InputChanged:Fire({Position=Vector2.new(x,y), Delta=Vector3.new(0,scroll,0), UserInputType=Enum.UserInputType.MouseWheel}) end

    local vu = game:GetService("VirtualUser")
    local function fireMouse(button, isDown, x, y)
        local uis = game:GetService("UserInputService")
        local input = {Position=Vector2.new(x or 0, y or 0), UserInputType=button,
                       State=isDown and "Begin" or "End"}
        (isDown and uis.InputBegan or uis.InputEnded):Fire(input)
        return input
    end
    function vu:Button1Down(x, y, camera) return fireMouse(Enum.UserInputType.MouseButton1, true, x, y) end
    function vu:Button1Up(x, y, camera) return fireMouse(Enum.UserInputType.MouseButton1, false, x, y) end
    function vu:Button2Down(x, y, camera) return fireMouse(Enum.UserInputType.MouseButton2, true, x, y) end
    function vu:Button2Up(x, y, camera) return fireMouse(Enum.UserInputType.MouseButton2, false, x, y) end
    function vu:MoveCamera(cam, cframe, hitCFrame, sync)
        if cam and cframe then cam.CFrame = cframe end
        return true
    end
    function vu:CaptureFrame() return true end
end

-- ContextActionService
do
    local cas = game:GetService("ContextActionService")
    cas._actions = {}
    function cas:BindAction(actionName, fn, createTouchButton, ...) self._actions[actionName] = {Callback=fn, Inputs={...}}; return actionName end
    function cas:UnbindAction(actionName) self._actions[actionName] = nil end
    function cas:BindActionAtPriority(actionName, fn, createTouchButton, priority, ...) return self:BindAction(actionName, fn, createTouchButton, ...) end
    function cas:UnbindActionAtPriority(actionName, priority) return self:UnbindAction(actionName) end
    function cas:FireAction(actionName, state, input) local action = self._actions[actionName]; if not action then return nil end; return action.Callback(actionName, state, input) end
end

-- ScriptContext
do
    local sc = game:GetService("ScriptContext")
    sc.Timeout = nil
    sc.CoreScriptsOnly = false
    function sc:SetTimeout(duration, coreScriptsOnly)
        assert(type(duration) == "number", "ScriptContext:SetTimeout expects a number")
        sc.Timeout = duration
        sc.CoreScriptsOnly = not not coreScriptsOnly
        return sc.Timeout
    end
end

-- TeleportService
do
    local ts = game:GetService("TeleportService")
    ts.TeleportInitFailed = signal()
    local function unavailable() error("TeleportService is unavailable: Lumora has no Roblox client") end
    function ts:Teleport(placeId, player, options, bindable) unavailable() end
    function ts:TeleportToPlaceInstance(placeId, jobId, player, ...) unavailable() end
    function ts:TeleportPartyAsync(placeId, players, options) unavailable() end
end

-- SoundService
game:GetService("SoundService")

-- Debris service
do
    local debris = game:GetService("Debris")
    function debris:AddItem(item, lifetime)
        assert(item and item.Destroy, "Debris:AddItem expects an Instance")
        task.delay(math.max(0, lifetime or 0), function() if item.Parent then item:Destroy() end end)
    end
    function debris:SetLegacyMaxItems(maxItems) self.MaxItems = maxItems end
end

-- LocalizationService
do
    local ls = game:GetService("LocalizationService")
    ls.SystemLocaleId = "en"
end

-- SetCoreGuiEnabled via StarterGui
do
    local sg = game:GetService("StarterGui")
    sg._enabled = {}; sg._core = {}
    function sg:SetCoreGuiEnabled(gui, enabled) self._enabled[gui] = not not enabled end
    function sg:GetCoreGuiEnabled(gui) return self._enabled[gui] ~= false end
    function sg:SetCore(name, value) self._core[name] = value end
    function sg:GetCore(name) return self._core[name] end
end

-- CollectionService
do
    local cs = game:GetService("CollectionService")
    cs._tags = {}; cs._added = {}; cs._removed = {}
    local function bucket(tag) cs._tags[tag] = cs._tags[tag] or {}; return cs._tags[tag] end
    function cs:GetTagged(tag) local out = {}; for instance in pairs(bucket(tag)) do table.insert(out, instance) end; return out end
    function cs:AddTag(instance, tag)
        assert(instance and tag, "CollectionService:AddTag expects instance and tag")
        local b = bucket(tag); if b[instance] then return end; b[instance] = true
        self:GetInstanceAddedSignal(tag):Fire(instance)
    end
    function cs:RemoveTag(instance, tag)
        local b = bucket(tag); if not b[instance] then return end; b[instance] = nil
        self:GetInstanceRemovedSignal(tag):Fire(instance)
    end
    function cs:GetInstanceAddedSignal(tag) self._added[tag] = self._added[tag] or signal(); return self._added[tag] end
    function cs:GetInstanceRemovedSignal(tag) self._removed[tag] = self._removed[tag] or signal(); return self._removed[tag] end
end

-- Animator / Humanoid simulation (set on Character instances)
local function _makeHumanoid()
    local h = Instance.new("Humanoid")
    h.Health = 100
    h.MaxHealth = 100
    h.WalkSpeed = 16
    h.JumpPower = 50
    h.RigType = Enum.HumanoidRigType.R15
    h.MoveDirection = Vector3.new(0,0,0)
    h._animator = Instance.new("Animator")
    h._animator.Name = "Animator"
    h._animator.Parent = h
    h._tracks = {}
    h._states = {}
    h._state = Enum.HumanoidStateType.Running
    function h:GetPlayingAnimationTracks() local out = {}; for _, track in ipairs(h._tracks) do if track.IsPlaying then table.insert(out, track) end end; return out end
    function h:Move(direction, relative) self.MoveDirection = direction; self.LastMoveRelative = not not relative end
    function h:Jump() self.JumpCount = (self.JumpCount or 0) + 1; self._state = Enum.HumanoidStateType.Jumping end
    function h:ChangeState(state) self._state = state end
    function h:GetState() return self._state end
    function h:SetStateEnabled(state, enabled) self._states[state] = not not enabled end
    function h:LoadAnimation(anim) local track = { Animation = anim, IsPlaying = false, Speed = 1 }; function track:Play() self.IsPlaying = true end; function track:Stop() self.IsPlaying = false end; function track:AdjustSpeed(speed) self.Speed = speed end; table.insert(h._tracks, track); return track end
    return h
end

-- Animator
do
    local anim = Instance.new("Animator")
    function anim:LoadAnimation(animTrack) return { Play = function() end, Stop = function() end } end
    function anim:GetPlayingAnimationTracks() return {} end
end

-- workspace extra properties
workspace.CurrentCamera = workspace:FindFirstChild("Camera")
if not workspace.CurrentCamera then
    workspace.CurrentCamera = Instance.new("Camera")
    workspace.CurrentCamera.Name = "Camera"
    workspace.CurrentCamera.Parent = workspace
end

-- SetCoreGuiEnabled global compatibility forwards to StarterGui state.
function game:SetCoreGuiEnabled(gui, enabled) return game:GetService("StarterGui"):SetCoreGuiEnabled(gui, enabled) end
function game:GetCoreGuiEnabled(gui) return game:GetService("StarterGui"):GetCoreGuiEnabled(gui) end
)LUA"
