-- Self-check for the assembled driver against a stubbed C4 API:
--   cd drivers/control4 && ./package.sh && lua test.lua
-- Runs against build/driver.lua (C4MQTT framing + driver body concatenated). Stubs
-- C4:CreateTCPClient (the transport) and feeds hand-encoded MQTT 3.1.1 bytes in,
-- checking the bytes and proxy notifies out. lua5.1, as on the controller.

local writes, notified, props, timers = {}, {}, {}, {}
local client   -- the fake TCP client the driver creates

local function makeClient()
  local c, h = {}, {}
  for _, ev in ipairs({ "OnConnect", "OnRead", "OnDisconnect", "OnError" }) do
    c[ev] = function(self, f) h[ev] = f; return self end
  end
  function c:Connect(ip, port) self.ip, self.port = ip, port; return self end
  function c:Write(b) writes[#writes + 1] = b end
  function c:ReadAtLeast() end
  function c:Close() end
  c.h = h
  return c
end

C4 = {
  UpdateProperty    = function(_, k, v) props[k] = v end,
  CreateTCPClient   = function() client = makeClient(); return client end,
  GetBindingAddress = function() return "10.0.0.9" end,
  SendToProxy       = function(_, id, cmd, params) notified[#notified + 1] = { cmd = cmd, params = params } end,
  SetTimer          = function(_, ms, f, rep) timers[#timers + 1] = { ms = ms, f = f, rep = rep }; return #timers end,
}
Properties = { Debug = "Off", Output = "amp", ["Topic Prefix"] = "openspeakerpoint" }
dofile("build/driver.lua")

local function u16(n) return string.char(math.floor(n / 256) % 256, n % 256) end
local function pubPacket(topic, payload)
  local body = u16(#topic) .. topic .. payload
  assert(#body < 128, "test payloads stay in a one-byte remaining length")
  return string.char(0x30) .. string.char(#body) .. body
end
local function last() return writes[#writes] end
local function read(data) client.h.OnRead(client, data) end

-- 1) Late init connects to the box's broker (IP from the 6001 binding, port 1883).
OnDriverLateInit()
assert(client and client.ip == "10.0.0.9" and client.port == 1883, "connects to box:1883")

-- 2) Socket up -> driver writes MQTT CONNECT.
client.h.OnConnect(client)
assert(last():byte(1) == 0x10, "CONNECT sent on socket connect")

-- 3) CONNACK accepted -> SUBSCRIBE to "<prefix>/+/#".
read(string.char(0x20, 0x02, 0x00, 0x00))
assert(last():byte(1) == 0x82 and last():find("openspeakerpoint/+/#", 1, true), "CONNACK -> SUBSCRIBE prefix")

-- 4) First retained message (volume), split across two reads: learns the base, forces
--    linein as the active source, and reports volume to the proxy.
local vol = pubPacket("openspeakerpoint/spk/state/audio/volume", "55")
read(vol:sub(1, 4)); read(vol:sub(5))
assert(tonumber(props.Volume) == 55, "volume property = 55, got " .. tostring(props.Volume))
assert(notified[#notified].cmd == "VOLUME_LEVEL_CHANGED" and notified[#notified].params.LEVEL == 55)
assert(last():find("cmd/audio/source", 1, true) and last():sub(-6) == "linein", "base learned -> force linein")

-- 5) Proxy SET_VOLUME_LEVEL -> PUBLISH cmd/audio/volume.
ReceivedFromProxy(5001, "SET_VOLUME_LEVEL", { OUTPUT = "4000", LEVEL = "35" })
assert(last():byte(1) == 0x30 and last():find("openspeakerpoint/spk/cmd/audio/volume", 1, true) and last():sub(-2) == "35")

-- 6) Clamp.
ReceivedFromProxy(5001, "SET_VOLUME_LEVEL", { OUTPUT = "4000", LEVEL = "250" })
assert(last():sub(-3) == "100", "volume clamps to 100")

-- 7) SET_INPUT (room selects the amp) -> force linein.
ReceivedFromProxy(5001, "SET_INPUT", { INPUT = "3001", OUTPUT = "4000" })
assert(last():find("cmd/audio/source", 1, true) and last():sub(-6) == "linein", last())

-- 8) Mute emulated via volume 0, then restore.
ReceivedFromProxy(5001, "SET_VOLUME_LEVEL", { OUTPUT = "4000", LEVEL = "40" })
ReceivedFromProxy(5001, "MUTE_ON", { OUTPUT = "4000" })
assert(last():sub(-1) == "0" and tonumber(props.Volume) == 0, "mute sends volume 0")
assert(notified[#notified].cmd == "MUTE_CHANGED" and notified[#notified].params.MUTE == "True")
ReceivedFromProxy(5001, "MUTE_OFF", { OUTPUT = "4000" })
assert(last():sub(-2) == "40" and tonumber(props.Volume) == 40, "unmute restores 40")

-- 9) Output property change + SetOutput command -> cmd/audio/output.
Properties.Output = "both"
OnPropertyChanged("Output")
assert(last():find("cmd/audio/output", 1, true) and last():sub(-4) == "both", last())
ExecuteCommand("SetOutput", { Output = "rca" })
assert(last():find("cmd/audio/output", 1, true) and last():sub(-3) == "rca", last())

-- 10) A disconnect schedules a reconnect timer (no crash).
client.h.OnDisconnect(client)
assert(#timers >= 1, "disconnect schedules a retry")

print("speakerpoint driver selfcheck OK")
