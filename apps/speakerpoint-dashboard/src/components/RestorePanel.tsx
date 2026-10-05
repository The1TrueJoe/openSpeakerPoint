import { useState } from "react";
import { RotateCcw, TriangleAlert } from "lucide-react";
import { Card } from "./ui/Card";
import { command, useSnapshot } from "@/lib/mqtt";

/** Return to stock Control4, from the box's own flash (osp-restore). The one
 * destructive control here, so it is two-step, and the daemon acts only on the
 * literal "confirm". Shown only when the box carries a stock payload. */
export function RestorePanel() {
  const { restore } = useSnapshot();
  const [armed, setArmed] = useState(false);
  const [started, setStarted] = useState(false);

  if (!restore?.available) return null;

  return (
    <Card title="Return to stock" icon={<RotateCcw className="h-4 w-4" />}>
      <p className="mb-3 text-xs text-slate-500">
        Puts back Control4's own firmware exactly as this unit had it, then reboots into it. openSpeakerPoint is
        removed until it is installed again.
      </p>
      {started ? (
        <p className="text-sm text-slate-300">Restoring - the speaker reboots into Control4 in a minute or two.</p>
      ) : !armed ? (
        <button
          onClick={() => setArmed(true)}
          className="w-full rounded-lg border border-red-500/30 py-2 text-sm font-medium text-red-400 hover:bg-red-500/10"
        >
          Reset to stock…
        </button>
      ) : (
        <div className="flex items-center gap-2">
          <span className="flex flex-1 items-center gap-1 text-xs text-red-400">
            <TriangleAlert className="h-3.5 w-3.5" /> This removes openSpeakerPoint. Sure?
          </span>
          <button
            onClick={() => {
              command("system/restore", "confirm");
              setStarted(true);
            }}
            className="rounded-lg bg-red-600 px-3 py-1.5 text-xs font-medium text-white hover:bg-red-500"
          >
            Yes, return to stock
          </button>
          <button
            onClick={() => setArmed(false)}
            className="rounded-lg border border-white/10 px-3 py-1.5 text-xs text-slate-300 hover:bg-white/[0.07]"
          >
            Cancel
          </button>
        </div>
      )}
    </Card>
  );
}
