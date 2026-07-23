import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { api } from "@/lib/api";
import type { PlaybackState } from "@/lib/types";

/** MPD-backed now-playing + library, proxied through the control daemon. */
export function useNowPlaying() {
  const qc = useQueryClient();

  const now = useQuery({
    queryKey: ["now"],
    queryFn: api.getNowPlaying,
    refetchInterval: (q) => (q.state.data?.state === "play" ? 1000 : 4000),
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

  return { library, play, rescan };
}
