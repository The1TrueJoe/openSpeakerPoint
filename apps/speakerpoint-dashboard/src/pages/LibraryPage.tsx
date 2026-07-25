import { useMemo, useRef, useState } from "react";
import { RefreshCw, Usb, Music, UploadCloud, ArrowDownAZ, ArrowUpAZ } from "lucide-react";
import { useLibrary, useNowPlaying, useUploadQueue } from "@/hooks/useMedia";
import { TrackRow } from "@/components/library/TrackRow";
import { UploadQueuePanel } from "@/components/library/UploadQueuePanel";
import { ConfirmDialog } from "@/components/ui/ConfirmDialog";
import { cn } from "@/lib/utils";
import type { SortDir, SortField, Track } from "@/lib/types";

const SORT_FIELDS: { id: SortField; label: string }[] = [
  { id: "title", label: "Title" },
  { id: "artist", label: "Artist" },
  { id: "album", label: "Album" },
];

function sortTracks(tracks: Track[], field: SortField, dir: SortDir): Track[] {
  const sorted = [...tracks].sort((a, b) => {
    const av = (a[field] ?? "").toLocaleLowerCase();
    const bv = (b[field] ?? "").toLocaleLowerCase();
    return av.localeCompare(bv);
  });
  return dir === "asc" ? sorted : sorted.reverse();
}

export function LibraryPage() {
  const { library, play, rescan, remove } = useLibrary();
  const { now } = useNowPlaying();
  const { items, upload, dismiss } = useUploadQueue(() => rescan.mutate());

  const [sortField, setSortField] = useState<SortField>("title");
  const [sortDir, setSortDir] = useState<SortDir>("asc");
  const [dragActive, setDragActive] = useState(false);
  const [pendingDelete, setPendingDelete] = useState<Track | null>(null);
  const fileInput = useRef<HTMLInputElement>(null);
  const dragCounter = useRef(0);

  const data = library.data;
  const tracks = useMemo(
    () => sortTracks(data?.tracks ?? [], sortField, sortDir),
    [data?.tracks, sortField, sortDir],
  );

  const nowPlayingFile = now.data?.source === "media" ? now.data.file : null;

  const onDrop = (e: React.DragEvent) => {
    e.preventDefault();
    dragCounter.current = 0;
    setDragActive(false);
    if (e.dataTransfer.files?.length) upload(e.dataTransfer.files);
  };

  return (
    <div
      className="relative mx-auto max-w-2xl px-4 py-6 sm:px-6 sm:py-10"
      onDragEnter={(e) => {
        e.preventDefault();
        dragCounter.current += 1;
        setDragActive(true);
      }}
      onDragOver={(e) => e.preventDefault()}
      onDragLeave={() => {
        dragCounter.current -= 1;
        if (dragCounter.current <= 0) setDragActive(false);
      }}
      onDrop={onDrop}
    >
      <header className="mb-6 flex items-center justify-between">
        <div>
          <h1 className="text-xl font-extrabold tracking-tight text-slate-50">Library</h1>
          <p className="text-sm text-slate-500">
            {tracks.length > 0
              ? `${tracks.length} track${tracks.length === 1 ? "" : "s"}`
              : data?.usb
                ? "No tracks found"
                : "No USB drive detected"}
          </p>
        </div>
        <div className="flex items-center gap-2">
          <input
            ref={fileInput}
            type="file"
            accept="audio/mpeg,.mp3"
            multiple
            hidden
            onChange={(e) => {
              if (e.target.files?.length) upload(e.target.files);
              e.target.value = "";
            }}
          />
          <button
            onClick={() => fileInput.current?.click()}
            disabled={!data?.usb && tracks.length === 0}
            className="chip hover:bg-white/10 disabled:opacity-50"
          >
            <UploadCloud className="h-3.5 w-3.5" />
            Upload
          </button>
          <button
            onClick={() => rescan.mutate()}
            disabled={rescan.isPending || data?.updating}
            className="chip hover:bg-white/10 disabled:opacity-50"
          >
            <RefreshCw className={cn("h-3.5 w-3.5", (rescan.isPending || data?.updating) && "animate-spin")} />
          </button>
        </div>
      </header>

      <div className="mb-4 flex items-center gap-2">
        <div className="flex rounded-full border border-white/10 p-0.5 text-xs">
          {SORT_FIELDS.map(({ id, label }) => (
            <button
              key={id}
              onClick={() => setSortField(id)}
              className={cn(
                "rounded-full px-3 py-1.5 font-medium transition",
                sortField === id ? "bg-accent text-white" : "text-slate-400",
              )}
            >
              {label}
            </button>
          ))}
        </div>
        <button
          onClick={() => setSortDir((d) => (d === "asc" ? "desc" : "asc"))}
          className="chip hover:bg-white/10"
          aria-label="Toggle sort direction"
        >
          {sortDir === "asc" ? <ArrowDownAZ className="h-3.5 w-3.5" /> : <ArrowUpAZ className="h-3.5 w-3.5" />}
        </button>
      </div>

      <div className="card">
        {library.isError ? (
          <Empty text="Media backend unavailable." />
        ) : tracks.length === 0 && !data?.usb ? (
          /* Only claim "no drive" when there's genuinely nothing to show -
           * if the player knows about tracks, list them even if the mount
           * probe disagrees, rather than hiding a working library. */
          <Empty text="Plug in a USB drive with MP3s, or drag files anywhere on this page." icon={Usb} />
        ) : tracks.length === 0 ? (
          <Empty text={data?.updating ? "Scanning drive…" : "No MP3s found. Drag files here to upload."} />
        ) : (
          <div className="-mx-1 max-h-[60vh] space-y-1 overflow-y-auto pr-1">
            {tracks.map((t) => (
              <TrackRow
                key={t.file}
                track={t}
                playing={nowPlayingFile === t.file}
                /* Index into the player's own ordering, not this sorted
                 * view, or sorting would play the wrong track. */
                onPlay={() => play.mutate(data?.tracks.indexOf(t) ?? 0)}
                onDelete={() => setPendingDelete(t)}
              />
            ))}
          </div>
        )}
      </div>

      {dragActive && (
        <div className="pointer-events-none fixed inset-0 z-40 flex items-center justify-center bg-base-950/80 backdrop-blur-sm">
          <div className="rounded-2xl border-2 border-dashed border-accent/60 px-10 py-8 text-center">
            <UploadCloud className="mx-auto mb-3 h-10 w-10 text-accent-soft" />
            <p className="text-lg font-semibold text-slate-100">Drop MP3s to upload</p>
          </div>
        </div>
      )}

      <UploadQueuePanel items={items} onDismiss={dismiss} />

      <ConfirmDialog
        open={!!pendingDelete}
        title="Delete track?"
        message={`"${pendingDelete?.title ?? pendingDelete?.file.split("/").pop()}" will be permanently removed from the drive.`}
        onCancel={() => setPendingDelete(null)}
        onConfirm={() => {
          if (pendingDelete) remove.mutate(pendingDelete.file);
          setPendingDelete(null);
        }}
      />
    </div>
  );
}

function Empty({ text, icon: Icon = Music }: { text: string; icon?: typeof Music }) {
  return (
    <div className="flex flex-col items-center gap-2 py-14 text-center text-sm text-slate-500">
      <Icon className="h-8 w-8 opacity-40" />
      {text}
    </div>
  );
}
