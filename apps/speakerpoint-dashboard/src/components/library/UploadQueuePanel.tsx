import { CheckCircle2, AlertCircle, X, Music4 } from "lucide-react";
import type { UploadItem } from "@/lib/types";

interface UploadQueuePanelProps {
  items: UploadItem[];
  onDismiss: (id: string) => void;
}

export function UploadQueuePanel({ items, onDismiss }: UploadQueuePanelProps) {
  if (items.length === 0) return null;

  return (
    <div className="fixed bottom-20 right-4 z-30 w-72 space-y-2 md:bottom-6">
      {items.map((it) => (
        <div key={it.id} className="animate-fade-up rounded-xl border border-white/10 bg-base-850 p-3 shadow-panel">
          <div className="flex items-center gap-2">
            <Music4 className="h-4 w-4 shrink-0 text-slate-500" />
            <span className="min-w-0 flex-1 truncate text-xs font-medium text-slate-200">{it.name}</span>
            {it.status === "done" && <CheckCircle2 className="h-4 w-4 shrink-0 text-signal" />}
            {it.status === "error" && <AlertCircle className="h-4 w-4 shrink-0 text-red-400" />}
            {it.status !== "uploading" && (
              <button onClick={() => onDismiss(it.id)} className="shrink-0 text-slate-500 hover:text-slate-300">
                <X className="h-3.5 w-3.5" />
              </button>
            )}
          </div>
          {it.status === "uploading" && (
            <div className="mt-2 h-1 overflow-hidden rounded-full bg-base-700">
              <div
                className="h-full rounded-full bg-accent transition-all"
                style={{ width: `${it.progress}%` }}
              />
            </div>
          )}
          {it.status === "error" && <p className="mt-1 text-[11px] text-red-400">{it.error}</p>}
        </div>
      ))}
    </div>
  );
}
