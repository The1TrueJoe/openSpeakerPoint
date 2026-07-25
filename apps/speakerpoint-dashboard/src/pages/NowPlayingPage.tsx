import { Pause, Play, SkipBack, SkipForward, Radio, AudioLines, Music4 } from "lucide-react";
import { AlbumArt } from "@/components/AlbumArt";
import { IconButton } from "@/components/ui/IconButton";
import { useNowPlaying } from "@/hooks/useMedia";
import { formatTime, cn } from "@/lib/utils";
import type { NowPlayingSource } from "@/lib/types";

const SOURCE_LABEL: Record<NowPlayingSource, string> = {
  media: "USB Media",
  airplay: "AirPlay",
  linein: "Line In",
};

export function NowPlayingPage() {
  const { now, transport, seek } = useNowPlaying();
  const data = now.data;

  const source = data?.source ?? "media";
  const isMedia = source === "media";
  const playing = isMedia && data?.state === "play";
  const hasTrack = isMedia && !!data?.file && data.state !== "stop";

  const heading =
    source === "airplay"
      ? data?.title ?? "AirPlay"
      : source === "linein"
        ? "Line In"
        : (data?.title ?? (hasTrack ? "Unknown title" : "Nothing playing"));

  const subheading =
    source === "airplay"
      ? (data?.artist ?? "Streaming from a connected device")
      : source === "linein"
        ? "External audio source"
        : (data?.artist ?? "—");

  const placeholderIcon = source === "airplay" ? Radio : source === "linein" ? AudioLines : Music4;

  return (
    <div className="mx-auto flex max-w-xl flex-1 flex-col items-center justify-center px-6 py-10">
      <div
        className={cn(
          "chip mb-6",
          source === "airplay" && "border-signal/40 bg-signal/10 text-signal",
        )}
      >
        <span
          className={cn(
            "h-1.5 w-1.5 rounded-full",
            source === "airplay" ? "bg-signal animate-pulse-ring" : "bg-accent-soft",
          )}
        />
        {SOURCE_LABEL[source]}
      </div>

      <AlbumArt
        file={isMedia ? (data?.file ?? null) : null}
        hasArt={isMedia && !!data?.hasArt}
        spinning={playing}
        placeholderIcon={placeholderIcon}
        className="w-64 shadow-panel sm:w-80"
      />

      <div className="mt-8 w-full text-center">
        <h1 className="truncate text-2xl font-extrabold tracking-tight text-slate-50 sm:text-3xl">
          {heading}
        </h1>
        <p className="mt-1 truncate text-base text-slate-400">{subheading}</p>
        {isMedia && data?.album && <p className="mt-0.5 truncate text-sm text-slate-600">{data.album}</p>}
      </div>

      {isMedia ? (
        <div className="mt-6 w-full">
          <input
            type="range"
            className="slider"
            min={0}
            max={Math.max(data?.duration ?? 0, 1)}
            value={Math.min(data?.elapsed ?? 0, data?.duration ?? 0)}
            disabled={!hasTrack}
            onChange={(e) => seek.mutate(Number(e.target.value))}
          />
          <div className="mt-1 flex justify-between text-xs tabular-nums text-slate-500">
            <span>{formatTime(data?.elapsed ?? 0)}</span>
            <span>{formatTime(data?.duration ?? 0)}</span>
          </div>
        </div>
      ) : (
        <div className="mt-6 h-6" />
      )}

      <div className="mt-6 flex items-center justify-center gap-4">
        <IconButton onClick={() => transport.mutate("prev")} disabled={!hasTrack} aria-label="Previous">
          <SkipBack className="h-5 w-5" />
        </IconButton>
        <IconButton
          variant="solid"
          size="lg"
          disabled={!isMedia}
          onClick={() => transport.mutate(playing ? "pause" : "play")}
          aria-label={playing ? "Pause" : "Play"}
        >
          {playing ? <Pause className="h-6 w-6" /> : <Play className="h-6 w-6 translate-x-0.5" />}
        </IconButton>
        <IconButton onClick={() => transport.mutate("next")} disabled={!hasTrack} aria-label="Next">
          <SkipForward className="h-5 w-5" />
        </IconButton>
      </div>
    </div>
  );
}
