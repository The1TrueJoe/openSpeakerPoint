-- Self-check for driver.lua against a stubbed C4 API: cd drivers/control4 && lua test.lua
-- Exercises the MQTT framing (encode + decode, including a split packet) and the
-- proxy/command -> cmd/* mapping. Run on lua5.1, as the controller does.

local sent, notified, props = {}, {}, {}
C4 = {
  UpdateProperty = function(_, k, v) props[k] = v end,
  SendToNetwork  = function(_, id, port, data) sent[#sent + 1] = data end,
  SendToProxy    = function(_, id, cmd, params) notified[#notified + 1] = { cmd = cmd, params = params } end,
  NetConnect     = function() end,
  SetTimer       = function() end,
  JsonDecode     = function(_, s)
    -- Tiny stand-in: pull the fields the driver reads out of the now JSON.
    local t = {}
    for k, v in s:gmatch('"(%w+)":"([^"]*)"') do t[k] = v end
    return t
  end,
}
Properties = { Debug = "Off", Output = "amp" }
dofile("driver.lua")

-- Build an inbound MQTT PUBLISH (QoS 0) the way a broker would frame it.
local function u16(n) return string.char(math.floor(n / 256) % 256, n % 256) end
local function pubPacket(topic, payload)
  local body = u16(#topic) .. topic .. payload
  assert(#body < 128, "test payloads stay in a one-byte remaining length")
  return string.char(0x30) .. string.char(#body) .. body
end
local function last() return sent[#sent] end

-- 1) CONNACK (accepted) -> driver subscribes.
ReceivedFromNetwork(6001, 1883, string.char(0x20, 0x02, 0x00, 0x00))
assert(last():byte(1) == 0x82, "CONNACK accepted -> SUBSCRIBE sent")

-- 2) A retained volume message, delivered split across two reads, updates the proxy.
local vol = pubPacket("openspeakerpoint/spk/state/audio/volume", "55")
ReceivedFromNetwork(6001, 1883, vol:sub(1, 4))
ReceivedFromNetwork(6001, 1883, vol:sub(5))
assert(tonumber(props.Volume) == 55, "volume property = 55, got " .. tostring(props.Volume))
assert(notified[#notified].cmd == "VOLUME_LEVEL_CHANGED" and notified[#notified].params.LEVEL == 55)

-- 3) now-playing JSON -> Now Playing property.
ReceivedFromNetwork(6001, 1883,
  pubPacket("openspeakerpoint/spk/state/now",
            '{"source":"media","state":"play","title":"Song","artist":"Band"}'))
assert(props["Now Playing"] == "play: Song - Band [media]", props["Now Playing"])

-- 4) Proxy SET_VOLUME_LEVEL -> PUBLISH to cmd/audio/volume with the right topic+payload.
ReceivedFromProxy(5001, "SET_VOLUME_LEVEL", { OUTPUT = "4000", LEVEL = "35" })
local p = last()
assert(p:byte(1) == 0x30, "PUBLISH")
assert(p:find("openspeakerpoint/spk/cmd/audio/volume", 1, true), "volume cmd topic")
assert(p:sub(-2) == "35", "volume payload 35")

-- 5) Clamp.
ReceivedFromProxy(5001, "SET_VOLUME_LEVEL", { OUTPUT = "4000", LEVEL = "250" })
assert(last():sub(-3) == "100", "volume clamps to 100")

-- 6) SET_INPUT line-in -> cmd/audio/source linein.
ReceivedFromProxy(5001, "SET_INPUT", { INPUT = "100", OUTPUT = "4000" })
assert(last():find("cmd/audio/source", 1, true) and last():sub(-6) == "linein", last())

-- 7) Mute emulated via volume 0, then restore.
ReceivedFromProxy(5001, "SET_VOLUME_LEVEL", { OUTPUT = "4000", LEVEL = "40" })
ReceivedFromProxy(5001, "MUTE_ON", { OUTPUT = "4000" })
assert(last():sub(-1) == "0" and tonumber(props.Volume) == 0, "mute sends volume 0")
assert(notified[#notified].cmd == "MUTE_CHANGED" and notified[#notified].params.MUTE == "True")
ReceivedFromProxy(5001, "MUTE_OFF", { OUTPUT = "4000" })
assert(last():sub(-2) == "40" and tonumber(props.Volume) == 40, "unmute restores 40")

-- 8) Transport command -> cmd/transport.
ExecuteCommand("Next", {})
assert(last():find("cmd/transport", 1, true) and last():sub(-4) == "next", last())

-- 9) Output property change -> cmd/audio/output.
Properties.Output = "both"
OnPropertyChanged("Output")
assert(last():find("cmd/audio/output", 1, true) and last():sub(-4) == "both", last())

print("speakerpoint driver selfcheck OK")
