R"LUA(
-- ========== game:HttpGet ==========
-- Load WindUI from the vendored file when the known URL is requested.
-- We provide a comprehensive stub that implements all the WindUI methods
-- the Da Hood script uses, instead of loading the full 20k-line WindUI
-- library (which requires a complete Roblox GUI rendering system).
local _winduiCache = nil

-- Create a headless WindUI element with observable state and callbacks.
local function _makeElement()
    local el = {
        __type = "WindUIElement",
        _flags = {},
        Enabled = false,
        Value = false,
    }
    -- Elements that act as containers (tabs) have sub-creator methods
    function el:Toggle(opts)
        opts = opts or {}
        if opts.Value ~= nil then self._flags[opts.Flag] = opts.Value end
        local sub = _makeElement()
        sub.Value = opts.Value or false
        sub.Enabled = opts.Value or false
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback and opts.Value then pcall(opts.Callback, opts.Value) end
        return sub
    end
    function el:Slider(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or opts.Min or 0
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Dropdown(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or opts.Default or ""
        sub.Options = opts.Options or {}
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Colorpicker(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or Color3.fromRGB(255, 255, 255)
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Button(opts)
        opts = opts or {}
        if opts.Callback then pcall(opts.Callback) end
        return _makeElement()
    end
    function el:Input(opts)
        opts = opts or {}
        local sub = _makeElement()
        sub.Value = opts.Value or ""
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Paragraph(opts)
        opts = opts or {}
        return _makeElement()
    end
    function el:Divider() return _makeElement() end
    function el:Select(opts) return el:Dropdown(opts) end
    function el:Keybind(opts) return el:Toggle(opts) end
    return el
end

local function _makeTab()
    local tab = _makeElement()
    tab._flags = {}
    return tab
end

local function _makeWindow()
    local w = _makeElement()
    w._flags = {}
    w._tabs = {}
    w.IsOpen = false
    w.Destroyed = false
    w._destroyCallbacks = {}
    function w:Tab(opts)
        opts = opts or {}
        local t = _makeTab()
        t.Title = opts.Title or ""
        t.Icon = opts.Icon
        table.insert(w._tabs, t)
        return t
    end
    function w:GetFlag(name) return w._flags[name] end
    function w:GetFlagElement(name) return w._flags[name] end
    function w:ListFlags() return w._flags end
    function w:Open()
        if w.Destroyed then return false end
        w.IsOpen = true
        if w._onOpen then pcall(w._onOpen, w) end
        return true
    end
    function w:Close()
        if w.Destroyed then return false end
        w.IsOpen = false
        if w._onClose then pcall(w._onClose, w) end
        return true
    end
    function w:OnDestroy(fn)
        assert(type(fn) == "function", "OnDestroy callback must be a function")
        table.insert(w._destroyCallbacks, fn)
        return { Disconnect = function() end, Connected = true }
    end
    function w:SetBackgroundImage(img) w.BackgroundImage = img; return w end
    function w:SetBackgroundImageTransparency(val) w.BackgroundImageTransparency = val; return w end
    function w:SetIconSize(size) w.IconSize = size; return w end
    function w:Destroy()
        if w.Destroyed then return end
        w.Destroyed = true
        w.IsOpen = false
        for _, fn in ipairs(w._destroyCallbacks) do pcall(fn, w) end
        w._destroyCallbacks = {}
    end
    return w
end

local function _makeWindUI()
    local ui = {}
    ui._themes = {
        Dark = { Name="Dark", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(255,255,255),
            Background=Color3.fromRGB(30,30,30), BorderColor=Color3.fromRGB(50,50,50) },
        Light = { Name="Light", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(0,0,0),
            Background=Color3.fromRGB(255,255,255), BorderColor=Color3.fromRGB(200,200,200) },
        Graphite = { Name="Graphite", Accent=Color3.fromRGB(80,80,80), TextColor=Color3.fromRGB(255,255,255),
            Background=Color3.fromRGB(20,20,20), BorderColor=Color3.fromRGB(40,40,40) },
    }
    function ui:CreateWindow(opts)
        return _makeWindow()
    end
    function ui:GetThemes() return ui._themes end
    function ui:SetTheme(name) assert(ui._themes[name], "unknown theme: " .. tostring(name)); ui.Theme = name; return ui._themes[name] end
    function ui:AddTheme(theme)
        if theme and theme.Name then ui._themes[theme.Name] = theme end
    end
    function ui:SetLanguage(lang) assert(type(lang) == "string", "language must be a string"); ui.Language = lang; return lang end
    function ui:Localization(opts) assert(type(opts) == "table", "localization options must be a table"); ui.LocalizationData = opts; return opts end
    function ui:Notify(opts) opts = opts or {}; ui.LastNotification = opts; return opts end
    function ui:Save() return { Theme = ui.Theme, Language = ui.Language, Localization = ui.LocalizationData } end
    function ui:Load(state) assert(type(state) == "table", "UI state must be a table"); if state.Theme then ui:SetTheme(state.Theme) end; if state.Language then ui:SetLanguage(state.Language) end; if state.Localization then ui.LocalizationData = state.Localization end; return ui end
    function ui:SetIconSize(size) assert(type(size) == "number", "icon size must be a number"); ui.IconSize = size; return size end
    return ui
end

local function _loadWindUI()
    if _winduiCache then return _winduiCache end
    -- Return a Lua source that creates and returns the headless WindUI implementation.
    -- This source is compiled and called by loadstring(...)() in the script.
    _winduiCache = [[
local ui = {}
ui._themes = {
    Dark = { Name="Dark", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(255,255,255),
        Background=Color3.fromRGB(30,30,30), BorderColor=Color3.fromRGB(50,50,50) },
    Light = { Name="Light", Accent=Color3.fromRGB(100,100,255), TextColor=Color3.fromRGB(0,0,0),
        Background=Color3.fromRGB(255,255,255), BorderColor=Color3.fromRGB(200,200,200) },
    Graphite = { Name="Graphite", Accent=Color3.fromRGB(80,80,80), TextColor=Color3.fromRGB(255,255,255),
        Background=Color3.fromRGB(20,20,20), BorderColor=Color3.fromRGB(40,40,40) },
}
local function makeElement()
    local el = { __type="WindUIElement", _flags={}, Enabled=false, Value=false }
    function el:Toggle(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or false
        sub.Enabled = opts.Value or false
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, opts.Value) end
        return sub
    end
    function el:Slider(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or opts.Min or 0
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Dropdown(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or opts.Default or ""
        sub.Options = opts.Options or {}
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Colorpicker(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or Color3.fromRGB(255, 255, 255)
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Button(opts)
        opts = opts or {}
        if opts.Callback then pcall(opts.Callback) end
        return makeElement()
    end
    function el:Input(opts)
        opts = opts or {}
        local sub = makeElement()
        sub.Value = opts.Value or ""
        if opts.Flag then self._flags[opts.Flag] = sub end
        if opts.Callback then pcall(opts.Callback, sub.Value) end
        return sub
    end
    function el:Paragraph(opts) return makeElement() end
    function el:Divider() return makeElement() end
    function el:Select(opts) return el:Dropdown(opts) end
    function el:Keybind(opts) return el:Toggle(opts) end
    function el:SetValue(v) self.Value = v; if self._cb then pcall(self._cb, v) end end
    function el:SetEnabled(v) self.Enabled = v end
    return el
end
local function makeWindow()
    local w = makeElement()
    w._flags = {}
    w._tabs = {}
    w.IsOpen = false
    w.Destroyed = false
    w._destroyCallbacks = {}
    function w:Tab(opts)
        opts = opts or {}
        local t = makeElement()
        t._flags = {}
        t.Title = opts.Title or ""
        t.Icon = opts.Icon
        table.insert(w._tabs, t)
        return t
    end
    function w:GetFlag(name) return w._flags[name] end
    function w:GetFlagElement(name) return w._flags[name] end
    function w:ListFlags() return w._flags end
    function w:Open()
        if w.Destroyed then return false end
        w.IsOpen = true
        if w._onOpen then pcall(w._onOpen, w) end
        return true
    end
    function w:Close()
        if w.Destroyed then return false end
        w.IsOpen = false
        if w._onClose then pcall(w._onClose, w) end
        return true
    end
    function w:OnDestroy(fn)
        assert(type(fn) == "function", "OnDestroy callback must be a function")
        table.insert(w._destroyCallbacks, fn)
        return { Disconnect = function() end, Connected = true }
    end
    function w:SetBackgroundImage(img) w.BackgroundImage = img; return w end
    function w:SetBackgroundImageTransparency(val) w.BackgroundImageTransparency = val; return w end
    function w:SetIconSize(size) w.IconSize = size; return w end
    function w:Destroy()
        if w.Destroyed then return end
        w.Destroyed = true
        w.IsOpen = false
        for _, fn in ipairs(w._destroyCallbacks) do pcall(fn, w) end
        w._destroyCallbacks = {}
    end
    return w
end
function ui:CreateWindow(opts) return makeWindow() end
function ui:GetThemes() return ui._themes end
function ui:SetTheme(name) assert(ui._themes[name], "unknown theme: " .. tostring(name)); ui.Theme = name; return ui._themes[name] end
function ui:AddTheme(theme) if theme and theme.Name then ui._themes[theme.Name] = theme end end
function ui:SetLanguage(lang) assert(type(lang) == "string", "language must be a string"); ui.Language = lang; return lang end
function ui:Localization(opts) assert(type(opts) == "table", "localization options must be a table"); ui.LocalizationData = opts; return opts end
function ui:Notify(opts) opts = opts or {}; ui.LastNotification = opts; return opts end
function ui:Save() return { Theme = ui.Theme, Language = ui.Language, Localization = ui.LocalizationData } end
function ui:Load(state) assert(type(state) == "table", "UI state must be a table"); if state.Theme then ui:SetTheme(state.Theme) end; if state.Language then ui:SetLanguage(state.Language) end; if state.Localization then ui.LocalizationData = state.Localization end; return ui end
function ui:SetIconSize(size) assert(type(size) == "number", "icon size must be a number"); ui.IconSize = size; return size end
return ui
]]
    return _winduiCache
end

)LUA"
