-- SpeakerPoint: Control4 driver for an openSpeakerPoint box.
--
-- The box is a network-controllable amp whose audio comes in on line-in (aux). Control4
-- (avswitch) selects this amp and sets volume/tone/output; the box plays whatever is on
-- aux. Not a media player and not a digital-audio streaming endpoint - no transport or
-- library controls. This driver controls the box over its MQTT API and mirrors the amp
-- state (volume/output/tone) back to the proxy and Properties.
--
-- DriverWorks has no MQTT client. The MQTT 3.1.1 (QoS 0) framing is the shared
-- control4-lua-mqtt lib (the global C4MQTT), concatenated ahead of this body by
-- package.sh; here we own only the transport (the 6001 TCP binding -> the box's
-- mosquitto on :1883, anonymous) and the box's topic contract. Self-check: test.lua.

local NET, PORT, PROXY = 6001, 1883, 5001
local OUTPUT = 4000
local KEEPALIVE = 30

local base           -- "openspeakerpoint/<hostname>", learned from the first message
local buffer = ""
local connected = false
local volume, muted, premuteVol = 0, false, 0

local function prop(name, value) C4:UpdateProperty(name, tostring(value)) end
local function dbg(msg) if Properties["Debug"] == "On" then print("[speakerpoint] " .. msg) end end

local function sendRaw(data) C4:SendToNetwork(NET, PORT, data) end

-- Publish a command. `sub` is relative to the box base, e.g. "cmd/audio/volume".
local function pub(sub, msg)
  if not base then dbg("drop " .. sub .. " (box not seen yet)"); return end
  local topic = base .. "/" .. sub
  dbg("> " .. topic .. " " .. msg)
  prop("Last Command", topic .. " " .. msg)
  sendRaw(C4MQTT.publish(topic, msg))
end

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
  elseif sub == "state/audio/output" then
    prop("Output", payload)
  elseif sub == "state/audio/bass" then
    prop("Bass", payload)
  elseif sub == "state/audio/treble" then
    prop("Treble", payload)
  elseif sub == "state/audio/loudness" then
    prop("Loudness", payload == "ON" and "On" or "Off")
  end
end

-- Topic is "<prefix>/<host>/<sub...>". Learn the base, dispatch the rest.
local function onMessage(topic, payload)
  dbg("< " .. topic .. " " .. payload)
  local b, sub = topic:match("^([^/]+/[^/]+)/(.+)$")
  if not b then return end
  if not base then
    base = b
    prop("Status", "connected (" .. base .. ")")
    -- Line-in (aux) is this amp's source; make it active as soon as we reach the box.
    sendRaw(C4MQTT.publish(base .. "/cmd/audio/source", "linein"))
  end
  if b == base then onBoxMessage(sub, payload) end
end

----------------------------------------------------------------------- MQTT decode
local function onConnack(rc)
  if rc == 0 then
    connected = true
    sendRaw(C4MQTT.subscribe(1, "openspeakerpoint/+/#"))
    if C4.SetTimer then
      C4:SetTimer(math.floor(KEEPALIVE * 1000 * 0.75), function() sendRaw(C4MQTT.pingreq()) end, true)
    end
  else
    prop("Status", "CONNACK refused " .. tostring(rc))
  end
end

local MQTT_HANDLERS = { connack = onConnack, publish = onMessage }

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

local function selectLinein() pub("cmd/audio/source", "linein") end

----------------------------------------------------------------------- C4 callbacks
function ReceivedFromNetwork(idBinding, nPort, strData)
  if idBinding ~= NET then return end
  buffer = C4MQTT.parse(buffer .. strData, MQTT_HANDLERS)
end

function OnConnectionStatusChanged(idBinding, nPort, strStatus)
  if idBinding ~= NET then return end
  if strStatus == "ONLINE" then
    connected = false
    buffer = ""
    prop("Status", "socket up, MQTT connecting")
    sendRaw(C4MQTT.connect("c4-speakerpoint-" .. tostring(math.random(100000, 999999)), { keepalive = KEEPALIVE }))
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

function ReceivedFromProxy(idBinding, strCommand, tParams)
  tParams = tParams or {}
  dbg("proxy " .. strCommand)
  if strCommand == "SET_INPUT" then
    -- One input (Line In / aux); selecting the amp just makes aux the active source.
    selectLinein()
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

-- Programming command: switch the amp output relay (amp/rca/both/off).
function ExecuteCommand(strCommand, tParams)
  tParams = tParams or {}
  dbg("command " .. strCommand)
  if strCommand == "SetOutput" then
    pub("cmd/audio/output", tParams.Output or "amp")
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
