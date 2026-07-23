import { Radio } from "lucide-react";
import { cn } from "@/lib/utils";

export function Header({ online }: { online: boolean }) {
  return (
    <header className="mb-6 flex items-center justify-between">
      <div className="flex items-center gap-3">
        <div className="grid h-11 w-11 place-items-center rounded-xl bg-accent/15 text-accent-soft shadow-glow">
          <Radio className="h-6 w-6" />
        </div>
        <div>
          <h1 className="text-lg font-extrabold leading-tight tracking-tight text-slate-50">
            SpeakerPoint
          </h1>
          <p className="text-xs text-slate-500">Open firmware · audio control</p>
        </div>
      </div>
      <div className="chip">
        <span
          className={cn(
            "h-2 w-2 rounded-full",
            online ? "bg-signal animate-pulse-ring" : "bg-red-500",
          )}
        />
        {online ? "Connected" : "Offline"}
      </div>
    </header>
  );
}
