import { useCallback, useState } from "react";
import { keepPreviousData, useQuery } from "@tanstack/react-query";
import { api } from "@/lib/api";
import { command, useSnapshot } from "@/lib/mqtt";
import type { PlaybackState, UploadItem } from "@/lib/types";

/** Now-playing across whichever source is actually active (media/airplay/linein),
 * live over MQTT. */
export function useNowPlaying() {
  const s = useSnapshot();
  return {
    now: { data: s.now },
    transport: { mutate: (cmd: PlaybackState | "next" | "prev") => command("transport", cmd), isPending: false },
    seek: { mutate: (seconds: number) => command("seek", Math.round(seconds)), isPending: false },
  };
}

/** The USB library. Its contents come over REST (too big for a retained MQTT
 * value), fetched again only when MQTT's state/library rev changes. */
export function useLibrary() {
  const s = useSnapshot();
  const rev = s.library?.rev;

  const library = useQuery({
    queryKey: ["library", rev],
    queryFn: api.getLibrary,
    placeholderData: keepPreviousData,
  });

  return {
    library,
    play: { mutate: (index: number) => command("play", index), isPending: false },
    rescan: { mutate: () => command("library/rescan", 1), isPending: false },
    remove: { mutate: (file: string) => command("library/delete", file), isPending: false },
  };
}

/** Tracks a queue of in-flight uploads with per-file progress, independent
 * of react-query (progress is ephemeral UI state, not server state). */
export function useUploadQueue(onSettled: () => void) {
  const [items, setItems] = useState<UploadItem[]>([]);

  const upload = useCallback(
    (files: FileList | File[]) => {
      const list = Array.from(files).filter((f) => /\.mp3$/i.test(f.name));
      const queued: UploadItem[] = list.map((f) => ({
        id: `${Date.now()}-${f.name}-${Math.random().toString(36).slice(2)}`,
        name: f.name,
        progress: 0,
        status: "uploading",
      }));
      if (queued.length === 0) return;
      setItems((prev) => [...prev, ...queued]);

      list.forEach((file, i) => {
        const id = queued[i].id;
        api
          .uploadWithProgress(file, (pct) => {
            setItems((prev) => prev.map((it) => (it.id === id ? { ...it, progress: pct } : it)));
          })
          .then(() => {
            setItems((prev) => prev.map((it) => (it.id === id ? { ...it, status: "done", progress: 100 } : it)));
            onSettled();
          })
          .catch((err: Error) => {
            setItems((prev) =>
              prev.map((it) => (it.id === id ? { ...it, status: "error", error: err.message } : it)),
            );
          });
      });
    },
    [onSettled],
  );

  const dismiss = useCallback((id: string) => {
    setItems((prev) => prev.filter((it) => it.id !== id));
  }, []);

  return { items, upload, dismiss };
}
