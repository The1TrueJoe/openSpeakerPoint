import { RefreshCw, Usb, Music } from "lucide-react";
import { Card } from "./ui/Card";
import { AlbumArt } from "./AlbumArt";
import { useLibrary } from "@/hooks/useMedia";
import { cn, formatTime } from "@/lib/utils";

export function MediaLibrary() {
  const { library, play, rescan } = useLibrary();
  const data = library.data;
  const tracks = data?.tracks ?? [];

  return (
    <Card
      title="USB Library"
      icon={<Usb className="h-4 w-4" />}
      action={
        <button
          onClick={() => rescan.mutate()}
          disabled={rescan.isPending || data?.updating}
          className="chip hover:bg-white/10 disabled:opacity-50"
        >
          <RefreshCw className={cn("h-3.5 w-3.5", (rescan.isPending || data?.updating) && "animate-spin")} />
          {data?.updating ? "Scanning" : "Rescan"}
        </button>
      }
    >
      {library.isError ? (
        <Empty text="Media backend unavailable." />
      ) : !data?.usb ? (
        <Empty text="No USB drive detected. Plug in a drive with music." />
      ) : tracks.length === 0 ? (
        <Empty text={data.updating ? "Scanning drive…" : "No audio files found on the drive."} />
      ) : (
        <ul className="-mx-1 max-h-96 space-y-1 overflow-y-auto pr-1">
          {tracks.map((t, i) => (
            <li key={t.file}>
              <button
                onClick={() => play.mutate(i)}
                className="flex w-full items-center gap-3 rounded-lg px-2 py-2 text-left transition hover:bg-white/[0.06]"
              >
                <AlbumArt file={t.file} hasArt={t.hasArt} className="h-11 w-11 shrink-0 rounded-md" />
                <div className="min-w-0 flex-1">
                  <div className="truncate text-sm font-medium text-slate-100">
                    {t.title ?? t.file.split("/").pop()}
                  </div>
                  <div className="truncate text-xs text-slate-500">
                    {[t.artist, t.album].filter(Boolean).join(" · ") || "Unknown artist"}
                  </div>
                </div>
                <span className="shrink-0 text-xs tabular-nums text-slate-500">
                  {t.duration ? formatTime(t.duration) : ""}
                </span>
              </button>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}

function Empty({ text }: { text: string }) {
  return (
    <div className="flex flex-col items-center gap-2 py-10 text-center text-sm text-slate-500">
      <Music className="h-8 w-8 opacity-40" />
      {text}
    </div>
  );
}
