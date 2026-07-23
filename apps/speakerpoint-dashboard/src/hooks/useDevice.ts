import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { api } from "@/lib/api";
import type { DeviceState, OutputMode } from "@/lib/types";

/** Audio routing + volume state, with optimistic updates on control. */
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

  const volume = useMutation({
    mutationFn: (value: number) => api.setVolume(value),
    onSuccess: setState,
  });

  return { state, output, volume, setState };
}
