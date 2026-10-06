import mqtt, { type MqttClient } from "mqtt";
import { useSyncExternalStore } from "react";
import type { DeviceState, NowPlaying, OutputMode, SourceMode } from "./types";

/**
 * The dashboard's live channel: MQTT over websockets to the box's own
 * mosquitto, through speakerpoint-control's websocket bridge at :8081/mqtt
 * (mosquitto's own websockets need OpenSSL, which doesn't fit). Everything
 * that changes - routing, volume, now-playing,
 * the library's revision, restore availability - arrives as retained state, so
 * nothing here polls; commands go out on `<base>/cmd/...`. See
 * apps/speakerpoint-control/src/mqtt.h for the topic map.
 *
 * The broker belongs to this one box, so its topic base is simply whichever
 * `openspeakerpoint/<hostname>` publishes state - the page knows the box's IP,
 * not its hostname.
 */

export interface LibraryState {
  usb: boolean;
  updating: boolean;
  rev: number;
}

export interface RestoreState {
  available: boolean;
  state: string;
}

export interface MeasureChannel {
  rms_dbfs: number;
  peak_dbfs: number;
  freq_hz: number | null;
}

export interface Measurement {
  ok: boolean;
  error?: string;
  seconds?: number;
  left?: MeasureChannel;
  right?: MeasureChannel;
}

export interface Snapshot {
  connected: boolean;
  base: string | null;
  online: boolean;
  output?: OutputMode;
  source?: SourceMode;
  volume?: number;
  now?: NowPlaying;
  library?: LibraryState;
  restore?: RestoreState;
  measure?: Measurement;
  error?: { topic: string; message: string };
}

const PREFIX = "openspeakerpoint";
const BROKER = `ws://${window.location.hostname}:8081/mqtt`;

let snap: Snapshot = { connected: false, base: null, online: false };
const listeners = new Set<() => void>();
let client: MqttClient | null = null;

function update(patch: Partial<Snapshot>) {
  snap = { ...snap, ...patch };
  listeners.forEach((l) => l());
}

/** Structured values are JSON; scalars are bare. */
function decode(raw: string): unknown {
  const t = raw.trim();
  if (t.startsWith("{") || t.startsWith("[")) {
    try {
      return JSON.parse(t);
    } catch {
      return t;
    }
  }
  return t;
}

function onMessage(topic: string, payload: Buffer) {
  const parts = topic.split("/");
  if (parts[0] !== PREFIX || parts.length < 3) return;
  const base = `${parts[0]}/${parts[1]}`;
  if (!snap.base) update({ base });
  if (base !== snap.base) return;

  const path = parts.slice(2).join("/");
  const value = decode(payload.toString());
  switch (path) {
    case "status":
      return update({ online: value === "online" });
    case "state/audio/output":
      return update({ output: value as OutputMode });
    case "state/audio/source":
      return update({ source: value as SourceMode });
    case "state/audio/volume":
      return update({ volume: Number(value) });
    case "state/now":
      return update({ now: value as NowPlaying });
    case "state/library":
      return update({ library: value as LibraryState });
    case "state/system/restore":
      return update({ restore: value as RestoreState });
    case "event/audio/measure":
      return update({ measure: value as Measurement });
    case "error":
      return update({ error: value as Snapshot["error"] });
  }
}

function connect() {
  if (client) return;
  client = mqtt.connect(BROKER, { reconnectPeriod: 2000, connectTimeout: 5000 });
  client.on("connect", () => {
    update({ connected: true });
    client!.subscribe([`${PREFIX}/+/status`, `${PREFIX}/+/state/#`, `${PREFIX}/+/event/#`, `${PREFIX}/+/error`]);
  });
  client.on("close", () => update({ connected: false }));
  client.on("message", onMessage);
}

/** Publish a command under this box's `cmd/`. Dropped until the base is known. */
export function command(path: string, value: string | number = "") {
  if (!client || !snap.base) return;
  client.publish(`${snap.base}/cmd/${path}`, String(value), { qos: 1 });
}

function subscribe(l: () => void) {
  connect();
  listeners.add(l);
  return () => listeners.delete(l);
}

export function useSnapshot(): Snapshot {
  return useSyncExternalStore(subscribe, () => snap);
}

/** The routing/volume triple, once all three have arrived. */
export function deviceState(s: Snapshot): DeviceState | undefined {
  if (s.output === undefined || s.source === undefined || s.volume === undefined) return undefined;
  return { output: s.output, source: s.source, volume: s.volume };
}
