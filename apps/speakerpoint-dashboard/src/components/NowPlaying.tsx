import { Pause, Play, SkipBack, SkipForward, Square } from "lucide-react";
import { AlbumArt } from "./AlbumArt";
import { IconButton } from "./ui/IconButton";
import { useNowPlaying } from "@/hooks/useMedia";
import { formatTime } from "@/lib/utils";

export function NowPlaying() {
  const { now, transport, seek } = useNowPlaying();
  const data = now.data;

  const playing = data?.state === "play";
  const hasTrack = !!data && data.state !== "stop" && !!data.file;

  return (
    <div className="card animate-fade-up bg-gradient-to-b from-base-800/80 to-base-850/80">
      <div className="flex gap-5">
        <AlbumArt
          file={data?.file ?? null}
          hasArt={!!data?.hasArt}
          spinning={playing}
          className="w-28 shrink-0 sm:w-36"
        />
        <div className="flex min-w-0 flex-1 flex-col justify-between">
          <div className="min-w-0">
            <div className="mb-1 text-xs font-semibold uppercase tracking-widest text-accent-soft">
              {data?.state === "stop" ? "Stopped" : playing ? "Now Playing" : "Paused"}
            </div>
            <h2 className="truncate text-xl font-bold text-slate-50">
              {data?.title ?? (hasTrack ? "Unknown title" : "Nothing playing")}
            </h2>
            <p className="truncate text-sm text-slate-400">{data?.artist ?? "—"}</p>
            <p className="truncate text-xs text-slate-500">{data?.album ?? ""}</p>
          </div>

          <SeekBar
            elapsed={data?.elapsed ?? 0}
            duration={data?.duration ?? 0}
            disabled={!hasTrack}
            onSeek={(s) => seek.mutate(s)}
          />
        </div>
      </div>

      <div className="mt-5 flex items-center justify-center gap-3">
        <IconButton onClick={() => transport.mutate("prev")} disabled={!hasTrack} aria-label="Previous">
          <SkipBack className="h-5 w-5" />
        </IconButton>
        <IconButton
          variant="solid"
          size="lg"
          onClick={() => transport.mutate(playing ? "pause" : "play")}
          aria-label={playing ? "Pause" : "Play"}
        >
          {playing ? <Pause className="h-6 w-6" /> : <Play className="h-6 w-6 translate-x-0.5" />}
        </IconButton>
        <IconButton onClick={() => transport.mutate("next")} disabled={!hasTrack} aria-label="Next">
          <SkipForward className="h-5 w-5" />
        </IconButton>
        <IconButton onClick={() => transport.mutate("stop")} disabled={!hasTrack} aria-label="Stop">
          <Square className="h-4 w-4" />
        </IconButton>
      </div>
    </div>
  );
}

function SeekBar({
  elapsed,
  duration,
  disabled,
  onSeek,
}: {
  elapsed: number;
  duration: number;
  disabled: boolean;
  onSeek: (s: number) => void;
}) {
  return (
    <div className="mt-3">
      <input
        type="range"
        className="slider"
        min={0}
        max={Math.max(duration, 1)}
        value={Math.min(elapsed, duration || 0)}
        disabled={disabled}
        onChange={(e) => onSeek(Number(e.target.value))}
      />
      <div className="mt-1 flex justify-between text-xs tabular-nums text-slate-500">
        <span>{formatTime(elapsed)}</span>
        <span>{formatTime(duration)}</span>
      </div>
    </div>
  );
}
