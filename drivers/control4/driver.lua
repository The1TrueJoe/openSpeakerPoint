-- SpeakerPoint (Open): Control4 driver for an openSpeakerPoint box.
--
-- Control4 sees an audio endpoint (avswitch): it selects a source into it and sets
-- volume; the box carries the audio itself (line-in from the EA3 ADC, or its own
-- USB/AirPlay media). This driver controls the box over its MQTT API and mirrors the
-- box's retained state back to the proxy and to the Properties.
--
-- DriverWorks has no MQTT client, so we frame MQTT 3.1.1 (QoS 0) ourselves over the
-- TCP binding (6001 -> the box's mosquitto on :1883, anonymous). Self-check: test.lua.

local NET, PORT, PROXY = 6001, 1883, 5001
local INPUT_LINEIN, INPUT_MEDIA = 100, 101
local OUTPUT = 4000
local KEEPALIVE = 30

local base           -- "openspeakerpoint/<hostname>", learned from the first message
local buffer = ""
local connected = false
local subPid = 0
local volume, muted, premuteVol = 0, false, 0

local function prop(name, value) C4:UpdateProperty(name, tostring(value)) end
local function dbg(msg) if Properties["Debug"] == "On" then print("[speakerpoint] " .. msg) end end

----------------------------------------------------------------------- MQTT framing
local function encodeLen(n)
  local s = ""
  repeat
    local b = n % 128
    n = math.floor(n / 128)
    if n > 0 then b = b + 128 end
    s = s .. string.char(b)
  until n == 0
  return s
end

local function u16(n) return string.char(math.floor(n / 256) % 256, n % 256) end
local function mqttStr(s) return u16(#s) .. s end
local function packet(typeFlags, payload) return string.char(typeFlags) .. encodeLen(#payload) .. payload end

local function sendRaw(data) C4:SendToNetwork(NET, PORT, data) end

local function mqttConnect()
  local clientId = "c4-speakerpoint-" .. tostring(math.random(100000, 999999))
  local vh = mqttStr("MQTT") .. string.char(4)   -- protocol level 4 = MQTT 3.1.1
           .. string.char(2)                     -- flags: clean session
           .. u16(KEEPALIVE)
  sendRaw(packet(0x10, vh .. mqttStr(clientId)))
end

local function mqttSubscribe(topic)
  subPid = subPid + 1
  sendRaw(packet(0x82, u16(subPid) .. mqttStr(topic) .. string.char(0)))
end

-- Publish a command. `sub` is relative to the box base, e.g. "cmd/audio/volume".
local function pub(sub, msg)
  if not base then dbg("drop " .. sub .. " (box not seen yet)"); return end
  local topic = base .. "/" .. sub
  dbg("> " .. topic .. " " .. msg)
  prop("Last Command", topic .. " " .. msg)
  sendRaw(packet(0x30, mqttStr(topic) .. msg))   -- PUBLISH, QoS 0
end

local function ping() sendRaw(string.char(0xC0, 0x00)) end

----------------------------------------------------------------------- state -> proxy
local function reportVolume()
  C4:SendToProxy(PROXY, "VOLUME_LEVEL_CHANGED", { OUTPUT = OUTPUT, LEVEL = volume }, "NOTIFY")
end
local function reportMute()
  C4:SendToProxy(PROXY, "MUTE_CHANGED", { OUTPUT = OUTPUT, MUTE = muted and "True" or "False" }, "NOTIFY")
end

-- A retained box message. `sub` is the topic below the base.
local function onBoxMessage(sub, payload)
  if sub == "status" then
    prop("Status", payload)
  elseif sub == "state/audio/volume" then
    volume = tonumber(payload) or volume
    prop("Volume", volume)
    if not muted then reportVolume() end
  elseif sub == "state/audio/source" then
    prop("Source", payload)
  elseif sub == "state/audio/output" then
    prop("Output", payload)
  elseif sub == "state/audio/bass" then
    prop("Bass", payload)
  elseif sub == "state/audio/treble" then
    prop("Treble", payload)
  elseif sub == "state/audio/loudness" then
    prop("Loudness", payload == "ON" and "On" or "Off")
  elseif sub == "state/now" then
    local ok, now = pcall(function() return C4:JsonDecode(payload) end)
    if ok and type(now) == "table" then
      local who = now.title or now.file or "-"
      if now.artist then who = who .. " - " .. now.artist end
      prop("Now Playing", (now.state or "?") .. ": " .. who .. " [" .. (now.source or "?") .. "]")
    else
      prop("Now Playing", payload)
    end
  end
end

-- Topic is "<prefix>/<host>/<sub...>". Learn the base, dispatch the rest.
local function onMessage(topic, payload)
  dbg("< " .. topic .. " " .. payload)
  local b, sub = topic:match("^([^/]+/[^/]+)/(.+)$")
  if not b then return end
  if not base then base = b; prop("Status", "connected (" .. base .. ")") end
  if b == base then onBoxMessage(sub, payload) end
end

----------------------------------------------------------------------- MQTT decode
-- Pull whole packets out of `buffer`; returns the unconsumed tail.
local function parse(buf)
  while #buf >= 2 do
    local b1 = buf:byte(1)
    local mult, len, i, byte = 1, 0, 2, 0
    repeat
      if i > #buf then return buf end        -- length not fully arrived
      byte = buf:byte(i)
      len = len + (byte % 128) * mult
      mult = mult * 128
      i = i + 1
      if mult > 128 * 128 * 128 * 128 then return "" end   -- malformed, resync by dropping
    until byte < 128
    if #buf < (i - 1) + len then return buf end            -- body not fully arrived
    local ptype = math.floor(b1 / 16)
    local flags = b1 % 16
    local body = buf:sub(i, i - 1 + len)
    buf = buf:sub(i + len)
    if ptype == 2 then                                     -- CONNACK
      if body:byte(2) == 0 then
        connected = true
        mqttSubscribe("openspeakerpoint/+/#")
        if C4.SetTimer then C4:SetTimer(math.floor(KEEPALIVE * 1000 * 0.75), ping, true) end
      else
        prop("Status", "CONNACK refused " .. tostring(body:byte(2)))
      end
    elseif ptype == 3 then                                 -- PUBLISH
      local tlen = body:byte(1) * 256 + body:byte(2)
      local topic = body:sub(3, 2 + tlen)
      local idx = 3 + tlen
      local qos = math.floor(flags / 2) % 4
      if qos > 0 then idx = idx + 2 end                    -- skip packet id (we sub at QoS 0, but be safe)
      onMessage(topic, body:sub(idx))
    end
    -- SUBACK (9) / PINGRESP (13): nothing to do.
  end
  return buf
end

----------------------------------------------------------------------- outbound ops
local function setVolume(level)
  level = math.max(0, math.min(100, math.floor(tonumber(level) or 0)))
  volume = level
  muted = false
  pub("cmd/audio/volume", tostring(level))
  prop("Volume", level)
  reportVolume()
end

local function setMute(on)
  if on and not muted then
    premuteVol = volume
    muted = true
    pub("cmd/audio/volume", "0")
    volume = 0
  elseif not on and muted then
    muted = false
    pub("cmd/audio/volume", tostring(premuteVol))
    volume = premuteVol
  end
  prop("Volume", volume)
  reportVolume()
  reportMute()
end

local function setSource(src) pub("cmd/audio/source", src) end
local function transport(t) pub("cmd/transport", t) end

----------------------------------------------------------------------- C4 callbacks
function ReceivedFromNetwork(idBinding, nPort, strData)
  if idBinding ~= NET then return end
  buffer = parse(buffer .. strData)
end

function OnConnectionStatusChanged(idBinding, nPort, strStatus)
  if idBinding ~= NET then return end
  if strStatus == "ONLINE" then
    connected = false
    prop("Status", "socket up, MQTT connecting")
    mqttConnect()
  else
    connected = false
    prop("Status", strStatus)
  end
end

function OnNetworkBindingChanged(idBinding, bIsBound)
  if idBinding ~= NET then return end
  prop("Status", bIsBound and "bound, connecting" or "not bound")
  if bIsBound then C4:NetConnect(NET, PORT) end
end

local INPUT_SOURCE = { [INPUT_LINEIN] = "linein", [INPUT_MEDIA] = "media" }

function ReceivedFromProxy(idBinding, strCommand, tParams)
  tParams = tParams or {}
  dbg("proxy " .. strCommand)
  if strCommand == "SET_INPUT" then
    local src = INPUT_SOURCE[tonumber(tParams.INPUT)]
    if src then setSource(src) end
    C4:SendToProxy(PROXY, "INPUT_OUTPUT_CHANGED", { INPUT = tParams.INPUT, OUTPUT = tParams.OUTPUT }, "NOTIFY")
  elseif strCommand == "SET_VOLUME_LEVEL" then
    setVolume(tParams.LEVEL)
  elseif strCommand == "PULSE_VOL_UP" then
    setVolume(volume + 2)
  elseif strCommand == "PULSE_VOL_DOWN" then
    setVolume(volume - 2)
  elseif strCommand == "MUTE_ON" then
    setMute(true)
  elseif strCommand == "MUTE_OFF" then
    setMute(false)
  elseif strCommand == "MUTE_TOGGLE" then
    setMute(not muted)
  elseif strCommand == "ON" or strCommand == "OFF" then
    -- avswitch power: the box has no standby; acknowledge so the room can select it.
  end
end

-- Programming commands / Actions.
local TRANSPORT = { Play = "play", Pause = "pause", Stop = "stop", Next = "next", Previous = "prev" }

function ExecuteCommand(strCommand, tParams)
  tParams = tParams or {}
  dbg("command " .. strCommand)
  if TRANSPORT[strCommand] then
    transport(TRANSPORT[strCommand])
  elseif strCommand == "RescanLibrary" then
    pub("cmd/library/rescan", "")
  elseif strCommand == "ReturnToStock" then
    pub("cmd/system/restore", "confirm")
  elseif strCommand == "SetOutput" then
    pub("cmd/audio/output", tParams.Output or "amp")
  elseif strCommand == "SetSource" then
    setSource(tParams.Source or "media")
  end
end

-- Settable Properties -> box commands.
function OnPropertyChanged(strProperty)
  local v = Properties[strProperty]
  if strProperty == "Output" then
    pub("cmd/audio/output", v)
  elseif strProperty == "Bass" then
    pub("cmd/audio/bass", v)
  elseif strProperty == "Treble" then
    pub("cmd/audio/treble", v)
  elseif strProperty == "Loudness" then
    pub("cmd/audio/loudness", v == "On" and "ON" or "OFF")
  end
end

function OnDriverLateInit()
  prop("Status", "connecting")
  C4:NetConnect(NET, PORT)
end
