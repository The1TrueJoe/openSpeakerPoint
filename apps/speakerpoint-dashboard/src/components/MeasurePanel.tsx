import { useEffect, useState } from "react";
import { Gauge } from "lucide-react";
import { Card } from "./ui/Card";
import { command, useSnapshot, type MeasureChannel } from "@/lib/mqtt";

/** Record the RCA line input and report level + pitch per channel - the
 * end-to-end check for whatever is feeding it (play a known tone, measure). */
export function MeasurePanel() {
  const { measure } = useSnapshot();
  const [busy, setBusy] = useState(false);
  const [asked, setAsked] = useState<typeof measure>(undefined);

  // A measurement arrived since we asked: done.
  useEffect(() => {
    if (busy && measure && measure !== asked) setBusy(false);
  }, [busy, measure, asked]);

  const run = () => {
    setAsked(measure);
    setBusy(true);
    command("audio/measure", 2);
  };

  const row = (name: string, c?: MeasureChannel) => (
    <div className="flex justify-between text-sm text-slate-300">
      <span className="text-slate-500">{name}</span>
      <span className="tabular-nums">
        {c ? `${c.rms_dbfs.toFixed(1)} dBFS rms · ${c.peak_dbfs.toFixed(1)} peak · ${c.freq_hz === null ? "—" : `${c.freq_hz.toFixed(0)} Hz`}` : "—"}
      </span>
    </div>
  );

  return (
    <Card title="Line-in measurement" icon={<Gauge className="h-4 w-4" />}>
      <p className="mb-3 text-xs text-slate-500">
        Records the RCA input for 2 s at 0 dB gain and reports each channel's level and dominant frequency.
      </p>
      <div className="space-y-1">
        {measure && !measure.ok ? (
          <p className="text-sm text-red-400">{measure.error}</p>
        ) : (
          <>
            {row("Left", measure?.left)}
            {row("Right", measure?.right)}
          </>
        )}
      </div>
      <button
        onClick={run}
        disabled={busy}
        className="mt-3 w-full rounded-lg border border-white/10 bg-white/[0.03] py-2 text-sm text-slate-200 hover:bg-white/[0.07] disabled:opacity-50"
      >
        {busy ? "Measuring…" : "Measure"}
      </button>
    </Card>
  );
}
