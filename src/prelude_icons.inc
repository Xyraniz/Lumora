R"LUA(
-- game:HttpGet(url, nocache) — return cached content for known URLs
-- We wrap instance_mt.__index directly (instance_mt is a local in scope)
-- because getmetatable(game) returns the locked string, not the real table.
local _savedInstanceIndex = instance_mt.__index
instance_mt.__index = function(self, key)
    -- Only treat `game` (the root DataModel) specially here.
    if self == game then
        if key == "HttpGet" then
            return function(obj, url, nocache)
                if url and string.find(url, "WindUI", 1, true) then
                    return _loadWindUI()
                end
                -- Icons library URL — return a deterministic, observable icon API
                if url and string.find(url, "Icons", 1, true) then
                    return [[
local module = {}
local icons = {}
local iconType = 'lucide'
function module.SetIconsType(t) iconType = t or 'lucide' end
function module.AddIcons(tbl) for k,v in pairs(tbl or {}) do icons[k] = v end end
function module.Init(r, name) module.Root = r; module.Name = name; return module end
function module.Icon(name, opts, h)
    -- WindUI expects Icon to return {imageString, {ImageRectSize=..., ImageRectPosition=...}}
    -- when h ~= false, or just the image string when h == false
    h = h ~= false
    local img = 'rbxasset://textures/ui/GuiImagePlaceholder.png'
    local rect = { ImageRectSize = Vector2.new(0, 0), ImageRectPosition = Vector2.new(0, 0) }
    if h then
        return { img, rect }
    end
    return img
end
function module.GetIcon(name) return module.Icon(name, nil, false) end
function module.Icon2(name, opts) return module.Icon(name, opts, true) end
module.IconsType = iconType
module.Icons = {}
return module
]]
                end
                return ""
            end
        end
        if key == "HttpGetAsync" then
            return function(obj, url) return game:HttpGet(url) end
        end
        if key == "GetObjects" then
            return function(obj, url) return {} end
        end
        if key == "PostAsync" then
            return function(obj, url, data, contentType) return "" end
        end
        if key == "PlaceId" then return 2788229376 end
        if key == "JobId" then return "LUMORA_JOB_001" end
        if key == "GameId" then return 0 end
        if key == "CreatorId" then return 0 end
        if key == "CreatorType" then return Enum.CreatorType.User end
        if key == "Players" then return game:GetService("Players") end
        if key == "Workspace" then return workspace end
        if key == "Lighting" then return game:GetService("Lighting") end
        if key == "ReplicatedStorage" then return game:GetService("ReplicatedStorage") end
        if key == "RunService" then return game:GetService("RunService") end
        if key == "UserInputService" then return game:GetService("UserInputService") end
        if key == "TweenService" then return game:GetService("TweenService") end
        if key == "CoreGui" then return game:GetService("CoreGui") end
        if key == "HttpService" then return game:GetService("HttpService") end
        if key == "ContentProvider" then return game:GetService("ContentProvider") end
        if key == "MarketplaceService" then return game:GetService("MarketplaceService") end
        if key == "TeleportService" then return game:GetService("TeleportService") end
        if key == "SoundService" then return game:GetService("SoundService") end
        if key == "ContextActionService" then return game:GetService("ContextActionService") end
        if key == "VirtualInputManager" then return game:GetService("VirtualInputManager") end
        if key == "VirtualUser" then return game:GetService("VirtualUser") end
        if key == "ScriptContext" then return game:GetService("ScriptContext") end
        if key == "StarterGui" then return game:GetService("StarterGui") end
        if key == "CollectionService" then return game:GetService("CollectionService") end
    end
    return _savedInstanceIndex(self, key)
end

)LUA"
