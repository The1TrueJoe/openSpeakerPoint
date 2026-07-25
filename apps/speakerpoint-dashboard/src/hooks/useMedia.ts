import { useCallback, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { api } from "@/lib/api";
import type { PlaybackState, UploadItem } from "@/lib/types";

/** Now-playing across whichever source is actually active (media/airplay/linein). */
export function useNowPlaying() {
  const qc = useQueryClient();

  const now = useQuery({
    queryKey: ["now"],
    queryFn: api.getNowPlaying,
    refetchInterval: (q) => (q.state.data?.state === "play" ? 1000 : 3000),
  });

  const invalidate = () => qc.invalidateQueries({ queryKey: ["now"] });

  const transport = useMutation({
    mutationFn: (cmd: PlaybackState | "next" | "prev") => api.transport(cmd),
    onSuccess: invalidate,
  });

  const seek = useMutation({
    mutationFn: (seconds: number) => api.seek(seconds),
    onSuccess: invalidate,
  });

  return { now, transport, seek };
}

export function useLibrary() {
  const qc = useQueryClient();

  const library = useQuery({
    queryKey: ["library"],
    queryFn: api.getLibrary,
    refetchInterval: (q) => (q.state.data?.updating ? 1500 : false),
  });

  const play = useMutation({
    mutationFn: (index: number) => api.playTrack(index),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["now"] }),
  });

  const rescan = useMutation({
    mutationFn: () => api.rescan(),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["library"] }),
  });

  const remove = useMutation({
    mutationFn: (file: string) => api.deleteTrack(file),
    onSuccess: () => qc.invalidateQueries({ queryKey: ["library"] }),
  });

  return { library, play, rescan, remove };
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
