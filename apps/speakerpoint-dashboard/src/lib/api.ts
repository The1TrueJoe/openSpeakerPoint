import type { Library } from "./types";

/**
 * REST is only for what MQTT carries badly (see lib/mqtt.ts for everything
 * else): the library listing, uploads and album-art bytes. The control daemon
 * listens on :8081; the dashboard is served by httpd on :80, so the API origin
 * comes from the page host.
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

export const api = {
  getLibrary: () => request<Library>("/api/library"),

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
