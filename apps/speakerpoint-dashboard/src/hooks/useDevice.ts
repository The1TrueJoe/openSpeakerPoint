import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { api } from "@/lib/api";
import type { DeviceState, OutputMode, SourceMode } from "@/lib/types";

/** Audio routing + source + volume state, with optimistic updates on control. */
export function useDevice() {
  const qc = useQueryClient();

  const state = useQuery({
    queryKey: ["state"],
    queryFn: api.getState,
    refetchInterval: 10_000,
  });

  const setState = (next: DeviceState) => qc.setQueryData(["state"], next);

  const output = useMutation({
    mutationFn: (value: OutputMode) => api.setOutput(value),
    onSuccess: setState,
  });

  const source = useMutation({
    mutationFn: (value: SourceMode) => api.setSource(value),
    onSuccess: (next) => {
      setState(next);
      /* Now-playing is derived from the active source, so refresh it
       * immediately rather than leaving the hero showing the old source
       * until the next poll comes around. */
      qc.invalidateQueries({ queryKey: ["now"] });
    },
  });

  const volume = useMutation({
    mutationFn: (value: number) => api.setVolume(value),
    onSuccess: setState,
  });

  return { state, output, source, volume, setState };
}
