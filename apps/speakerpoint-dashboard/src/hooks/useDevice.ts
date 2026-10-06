import { command, deviceState, useSnapshot } from "@/lib/mqtt";
import type { OutputMode, SourceMode } from "@/lib/types";

/** Audio routing + source + volume, live over MQTT (retained state; no polling). */
export function useDevice() {
  const s = useSnapshot();
  const data = deviceState(s);
  return {
    state: { data, isSuccess: s.online && data !== undefined },
    output: { mutate: (value: OutputMode) => command("audio/output", value), isPending: false },
    source: { mutate: (value: SourceMode) => command("audio/source", value), isPending: false },
    volume: { mutate: (value: number) => command("audio/volume", Math.round(value)), isPending: false },
    tone: {
      bass: s.bass,
      treble: s.treble,
      loudness: s.loudness,
      setBass: (v: number) => command("audio/bass", Math.round(v)),
      setTreble: (v: number) => command("audio/treble", Math.round(v)),
      setLoudness: (on: boolean) => command("audio/loudness", on ? "ON" : "OFF"),
    },
  };
}
