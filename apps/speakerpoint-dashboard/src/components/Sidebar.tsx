import { Disc3, ListMusic, Settings, Radio } from "lucide-react";
import { cn } from "@/lib/utils";
import { PlaybackWidget } from "./PlaybackWidget";
import type { View } from "@/App";

const ITEMS: { id: View; label: string; icon: typeof Disc3 }[] = [
  { id: "now-playing", label: "Now Playing", icon: Disc3 },
  { id: "library", label: "Library", icon: ListMusic },
  { id: "settings", label: "Settings", icon: Settings },
];

interface SidebarProps {
  view: View;
  onChange: (v: View) => void;
  online: boolean;
}

export function Sidebar({ view, onChange, online }: SidebarProps) {
  return (
    <>
      {/* Desktop left rail */}
      <nav className="hidden w-60 shrink-0 flex-col border-r border-white/5 bg-base-900/60 px-4 py-6 md:flex">
        <div className="mb-8 flex items-center gap-3 px-2">
          <div className="grid h-10 w-10 place-items-center rounded-xl bg-accent/15 text-accent-soft shadow-glow">
            <Radio className="h-5 w-5" />
          </div>
          <div>
            <div className="text-sm font-extrabold tracking-tight text-slate-50">SpeakerPoint</div>
            <div className="text-[11px] text-slate-500">Open firmware</div>
          </div>
        </div>

        <div className="flex flex-col gap-1">
          {ITEMS.map(({ id, label, icon: Icon }) => (
            <button
              key={id}
              onClick={() => onChange(id)}
              className={cn(
                "flex items-center gap-3 rounded-xl px-3 py-2.5 text-sm font-medium transition",
                view === id
                  ? "bg-accent/15 text-accent-soft"
                  : "text-slate-400 hover:bg-white/5 hover:text-slate-200",
              )}
            >
              <Icon className="h-5 w-5" />
              {label}
            </button>
          ))}
        </div>

        <div className="mt-auto flex flex-col gap-4">
          <PlaybackWidget onOpenNowPlaying={() => onChange("now-playing")} className="border-t border-white/5 pt-4" />
          <div className="flex items-center gap-2 px-1 text-xs text-slate-500">
            <span className={cn("h-1.5 w-1.5 rounded-full", online ? "bg-signal" : "bg-red-500")} />
            {online ? "Connected" : "Offline"}
          </div>
        </div>
      </nav>

      {/* Mobile: mini-player above the bottom tab bar */}
      <div className="fixed inset-x-0 bottom-16 z-40 border-t border-white/10 bg-base-900/95 px-4 py-3 backdrop-blur md:hidden">
        <PlaybackWidget onOpenNowPlaying={() => onChange("now-playing")} />
      </div>

      {/* Mobile bottom bar */}
      <nav className="fixed inset-x-0 bottom-0 z-40 flex border-t border-white/10 bg-base-900/95 backdrop-blur md:hidden">
        {ITEMS.map(({ id, label, icon: Icon }) => (
          <button
            key={id}
            onClick={() => onChange(id)}
            className={cn(
              "flex flex-1 flex-col items-center gap-1 py-2.5 text-[11px] font-medium transition",
              view === id ? "text-accent-soft" : "text-slate-500",
            )}
          >
            <Icon className="h-5 w-5" />
            {label}
          </button>
        ))}
      </nav>
    </>
  );
}
