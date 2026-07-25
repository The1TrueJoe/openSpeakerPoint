import { Trash2 } from "lucide-react";
import { AlbumArt } from "@/components/AlbumArt";
import { formatTime } from "@/lib/utils";
import type { Track } from "@/lib/types";

interface TrackRowProps {
  track: Track;
  playing: boolean;
  onPlay: () => void;
  onDelete: () => void;
}

export function TrackRow({ track, playing, onPlay, onDelete }: TrackRowProps) {
  return (
    <div
      className={`group flex items-center gap-3 rounded-lg px-2 py-2 transition ${
        playing ? "bg-accent/10" : "hover:bg-white/[0.06]"
      }`}
    >
      <button onClick={onPlay} className="flex min-w-0 flex-1 items-center gap-3 text-left">
        <AlbumArt file={track.file} hasArt={track.hasArt} className="h-11 w-11 shrink-0 rounded-md" />
        <div className="min-w-0 flex-1">
          <div className={`truncate text-sm font-medium ${playing ? "text-accent-soft" : "text-slate-100"}`}>
            {track.title ?? track.file.split("/").pop()}
          </div>
          <div className="truncate text-xs text-slate-500">
            {[track.artist, track.album].filter(Boolean).join(" · ") || "Unknown artist"}
          </div>
        </div>
        <span className="shrink-0 text-xs tabular-nums text-slate-500">
          {track.duration ? formatTime(track.duration) : ""}
        </span>
      </button>
      <button
        onClick={onDelete}
        className="shrink-0 rounded-lg p-2 text-slate-600 opacity-70 transition hover:bg-red-500/10 hover:text-red-400 md:opacity-0 md:group-hover:opacity-100"
        aria-label="Delete track"
      >
        <Trash2 className="h-4 w-4" />
      </button>
    </div>
  );
}
