import { useState } from "react";
import { Disc3 } from "lucide-react";
import { api } from "@/lib/api";
import { cn } from "@/lib/utils";

interface AlbumArtProps {
  file: string | null;
  hasArt: boolean;
  className?: string;
  spinning?: boolean;
}

export function AlbumArt({ file, hasArt, className, spinning }: AlbumArtProps) {
  const [failed, setFailed] = useState(false);
  const url = hasArt && !failed ? api.albumArtUrl(file) : null;

  return (
    <div
      className={cn(
        "relative aspect-square overflow-hidden rounded-xl border border-white/10 bg-base-800",
        className,
      )}
    >
      {url ? (
        <img
          key={url}
          src={url}
          alt="Album art"
          loading="lazy"
          decoding="async"
          className="h-full w-full object-cover"
          onError={() => setFailed(true)}
        />
      ) : (
        <div className="flex h-full w-full items-center justify-center text-base-600">
          <Disc3 className={cn("h-1/2 w-1/2", spinning && "animate-spin [animation-duration:6s]")} />
        </div>
      )}
    </div>
  );
}
