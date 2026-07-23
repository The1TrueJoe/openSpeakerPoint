import type {
  DeviceState,
  Library,
  NowPlaying,
  OutputMode,
  PlaybackState,
  ToneChannel,
} from "./types";

/**
 * The control daemon listens on :8081; the dashboard is served by httpd on :80.
 * Derive the API origin from the page host so it works regardless of the
 * device's IP.
 */
const API_BASE = `${window.location.protocol}//${window.location.hostname}:8081`;

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`, init);
  if (!res.ok) {
    throw new Error(`${init?.method ?? "GET"} ${path} -> ${res.status}`);
  }
  const text = await res.text();
  return (text ? JSON.parse(text) : {}) as T;
}

const post = (path: string) => request<{ ok: boolean }>(path, { method: "POST" });

export const api = {
  getState: () => request<DeviceState>("/api/state"),
  setOutput: (value: OutputMode) => request<DeviceState>(`/api/output?value=${value}`, { method: "POST" }),
  setVolume: (value: number) =>
    request<DeviceState>(`/api/volume?value=${Math.round(value)}`, { method: "POST" }),

  playTone: (channel: ToneChannel) => post(`/api/tone?channel=${channel}&type=pink`),
  stopTone: () => post("/api/tone/stop"),

  getNowPlaying: () => request<NowPlaying>("/api/now"),
  transport: (cmd: PlaybackState | "next" | "prev") => post(`/api/transport?cmd=${cmd}`),
  seek: (seconds: number) => post(`/api/seek?value=${Math.round(seconds)}`),

  getLibrary: () => request<Library>("/api/library"),
  playTrack: (index: number) => post(`/api/play?index=${index}`),
  rescan: () => post("/api/rescan"),

  albumArtUrl: (file: string | null | undefined) =>
    file ? `${API_BASE}/api/albumart?file=${encodeURIComponent(file)}` : null,
};

export { API_BASE };
