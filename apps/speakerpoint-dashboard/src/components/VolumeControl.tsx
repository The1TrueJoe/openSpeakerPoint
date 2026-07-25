import { useEffect, useState } from "react";
import { Volume1, Volume2, VolumeX } from "lucide-react";

interface Props {
  value: number;
  onCommit: (v: number) => void;
  disabled?: boolean;
}

export function VolumeControl({ value, onCommit, disabled }: Props) {
  const [local, setLocal] = useState(value);
  const [dragging, setDragging] = useState(false);

  // Keep in sync with backend unless the user is actively dragging.
  useEffect(() => {
    if (!dragging) setLocal(value);
  }, [value, dragging]);

  const Icon = local === 0 ? VolumeX : local < 50 ? Volume1 : Volume2;

  return (
    <div className="flex items-center gap-3">
      <Icon className="h-4 w-4 shrink-0 text-slate-400" />
      <input
        type="range"
        className="slider"
        min={0}
        max={100}
        value={local}
        disabled={disabled}
        onChange={(e) => {
          setDragging(true);
          setLocal(Number(e.target.value));
        }}
        onMouseUp={() => {
          setDragging(false);
          onCommit(local);
        }}
        onTouchEnd={() => {
          setDragging(false);
          onCommit(local);
        }}
      />
      <span className="w-8 shrink-0 text-right text-sm font-semibold tabular-nums text-slate-300">{local}</span>
    </div>
  );
}
