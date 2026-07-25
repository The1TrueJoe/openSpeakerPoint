import type {
  DeviceState,
  Library,
  NowPlaying,
  OutputMode,
  PlaybackState,
  SourceMode,
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
  setSource: (value: SourceMode) => request<DeviceState>(`/api/source?value=${value}`, { method: "POST" }),
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
  deleteTrack: (file: string) => post(`/api/delete?file=${encodeURIComponent(file)}`),

  /** XHR (not fetch) so we get real upload progress events. */
  uploadWithProgress: (file: File, onProgress: (pct: number) => void) =>
    new Promise<void>((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open("POST", `${API_BASE}/api/upload?name=${encodeURIComponent(file.name)}`);
      xhr.setRequestHeader("Content-Type", "application/octet-stream");
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) onProgress(Math.round((e.loaded / e.total) * 100));
      };
      xhr.onload = () => {
        if (xhr.status >= 200 && xhr.status < 300) resolve();
        else reject(new Error(`upload failed (${xhr.status})`));
      };
      xhr.onerror = () => reject(new Error("network error"));
      xhr.send(file);
    }),

  albumArtUrl: (file: string | null | undefined) =>
    file ? `${API_BASE}/api/albumart?file=${encodeURIComponent(file)}` : null,
};

export { API_BASE };
