import { useEffect, useState } from "react";
import {
  Cable,
  Speaker,
  Layers,
  Volume2,
  VolumeX,
  Radio,
  AudioLines,
  Music4,
  type LucideIcon,
} from "lucide-react";
import { AlbumArt } from "./AlbumArt";
import { useDevice } from "@/hooks/useDevice";
import { useNowPlaying } from "@/hooks/useMedia";
import { cn } from "@/lib/utils";
import type { OutputMode, SourceMode } from "@/lib/types";

const OUTPUTS: { id: Exclude<OutputMode, "off">; label: string; icon: LucideIcon }[] = [
  { id: "rca", label: "Line Out", icon: Cable },
  { id: "amp", label: "Speakers", icon: Speaker },
  { id: "both", label: "Both", icon: Layers },
];

/* AirPlay isn't listed here - it's automatic, pre-empting whichever of
 * these is selected whenever a stream connects. */
const SOURCES: { id: SourceMode; label: string; icon: LucideIcon }[] = [
  { id: "media", label: "USB Media", icon: Music4 },
  { id: "linein", label: "Line In", icon: AudioLines },
];

interface PlaybackWidgetProps {
  onOpenNowPlaying?: () => void;
  className?: string;
}

/** Persistent mini-player: shown in the desktop sidebar and as a mobile bar
 * above the bottom nav. Lets you route output and mute/adjust volume from
 * anywhere without leaving the current page. */
export function PlaybackWidget({ onOpenNowPlaying, className }: PlaybackWidgetProps) {
  const { state, output, source, volume } = useDevice();
  const { now } = useNowPlaying();

  const current = state.data;
  const muted = current?.output === "off";
  const [lastOutput, setLastOutput] = useState<Exclude<OutputMode, "off">>("both");

  useEffect(() => {
    if (current && current.output !== "off") setLastOutput(current.output);
  }, [current?.output]);

  const [localVolume, setLocalVolume] = useState(0);
  const [dragging, setDragging] = useState(false);
  useEffect(() => {
    if (!dragging) setLocalVolume(current?.volume ?? 0);
  }, [current?.volume, dragging]);

  const data = now.data;
  const nowSource = data?.source ?? "media";
  const isMedia = nowSource === "media";
  const title =
    nowSource === "airplay"
      ? (data?.title ?? "AirPlay")
      : nowSource === "linein"
        ? "Line In"
        : (data?.title ?? "Nothing playing");
  const subtitle =
    nowSource === "airplay"
      ? (data?.artist ?? "AirPlay")
      : nowSource === "linein"
        ? "External source"
        : (data?.artist ?? "");
  const placeholderIcon = nowSource === "airplay" ? Radio : nowSource === "linein" ? AudioLines : Music4;

  return (
    <div className={cn("flex flex-col gap-3", className)}>
      <button
        onClick={onOpenNowPlaying}
        className="flex min-w-0 items-center gap-2.5 rounded-lg px-1 py-1 text-left transition hover:bg-white/5"
      >
        <AlbumArt
          file={isMedia ? (data?.file ?? null) : null}
          hasArt={isMedia && !!data?.hasArt}
          placeholderIcon={placeholderIcon}
          className="h-10 w-10 shrink-0 rounded-lg"
        />
        <div className="min-w-0 flex-1">
          <div className="truncate text-xs font-semibold text-slate-100">{title}</div>
          <div className="truncate text-[11px] text-slate-500">{subtitle}</div>
        </div>
      </button>

      <div className="grid grid-cols-2 gap-1">
        {SOURCES.map(({ id, label, icon: Icon }) => (
          <button
            key={id}
            title={label}
            onClick={() => source.mutate(id)}
            className={cn(
              "flex flex-col items-center gap-1 rounded-lg py-1.5 text-[10px] font-medium transition",
              current?.source === id
                ? "bg-accent/15 text-accent-soft"
                : "text-slate-500 hover:bg-white/5 hover:text-slate-300",
            )}
          >
            <Icon className="h-4 w-4" />
            {label}
          </button>
        ))}
      </div>

      <div className="grid grid-cols-3 gap-1">
        {OUTPUTS.map(({ id, label, icon: Icon }) => (
          <button
            key={id}
            title={label}
            onClick={() => output.mutate(id)}
            className={cn(
              "flex flex-col items-center gap-1 rounded-lg py-1.5 text-[10px] font-medium transition",
              current?.output === id
                ? "bg-accent/15 text-accent-soft"
                : "text-slate-500 hover:bg-white/5 hover:text-slate-300",
            )}
          >
            <Icon className="h-4 w-4" />
            {label}
          </button>
        ))}
      </div>

      <div className="flex items-center gap-2">
        <button
          onClick={() => output.mutate(muted ? lastOutput : "off")}
          className="shrink-0 text-slate-400 transition hover:text-slate-200"
          aria-label={muted ? "Unmute" : "Mute"}
        >
          {muted ? <VolumeX className="h-4 w-4" /> : <Volume2 className="h-4 w-4" />}
        </button>
        <input
          type="range"
          className="slider"
          min={0}
          max={100}
          value={localVolume}
          disabled={!state.isSuccess || muted}
          onChange={(e) => {
            setDragging(true);
            setLocalVolume(Number(e.target.value));
          }}
          onMouseUp={() => {
            setDragging(false);
            volume.mutate(localVolume);
          }}
          onTouchEnd={() => {
            setDragging(false);
            volume.mutate(localVolume);
          }}
        />
      </div>
    </div>
  );
}
