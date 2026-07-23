import { useState } from "react";
import { useMutation } from "@tanstack/react-query";
import { Activity, Square } from "lucide-react";
import { Card } from "./ui/Card";
import { api } from "@/lib/api";
import { cn } from "@/lib/utils";
import type { ToneChannel } from "@/lib/types";

const CHANNELS: { id: ToneChannel; label: string }[] = [
  { id: "left", label: "Left" },
  { id: "right", label: "Right" },
  { id: "both", label: "Both" },
];

export function TestTonePanel() {
  const [active, setActive] = useState<ToneChannel | null>(null);

  const play = useMutation({
    mutationFn: (channel: ToneChannel) => api.playTone(channel),
    onMutate: (channel) => setActive(channel),
  });
  const stop = useMutation({
    mutationFn: () => api.stopTone(),
    onSettled: () => setActive(null),
  });

  return (
    <Card title="Test Tones" icon={<Activity className="h-4 w-4" />}>
      <p className="mb-3 text-xs text-slate-500">
        Pink noise on the selected output, per channel. Playing a tone pauses media.
      </p>
      <div className="grid grid-cols-3 gap-2">
        {CHANNELS.map(({ id, label }) => (
          <button
            key={id}
            onClick={() => play.mutate(id)}
            disabled={play.isPending}
            className={cn(
              "rounded-lg border py-3 text-sm font-medium transition",
              active === id
                ? "border-signal/50 bg-signal/10 text-signal"
                : "border-white/10 bg-white/[0.03] text-slate-200 hover:bg-white/[0.07]",
            )}
          >
            {label}
          </button>
        ))}
      </div>
      <button
        onClick={() => stop.mutate()}
        className="mt-3 flex w-full items-center justify-center gap-2 rounded-lg border border-white/10 bg-white/[0.03] py-2 text-sm text-slate-300 hover:bg-white/[0.07]"
      >
        <Square className="h-3.5 w-3.5" /> Stop tone
      </button>
    </Card>
  );
}
