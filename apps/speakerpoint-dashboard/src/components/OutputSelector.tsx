import { Cable, Speaker, Layers, PowerOff, type LucideIcon } from "lucide-react";
import { Card } from "./ui/Card";
import { cn } from "@/lib/utils";
import type { OutputMode } from "@/lib/types";

const OPTIONS: { id: OutputMode; label: string; hint: string; icon: LucideIcon }[] = [
  { id: "rca", label: "Line Out", hint: "RCA analog jacks", icon: Cable },
  { id: "amp", label: "Speakers", hint: "Powered amplifier", icon: Speaker },
  { id: "both", label: "Both", hint: "Line + speakers", icon: Layers },
  { id: "off", label: "Off", hint: "Muted", icon: PowerOff },
];

interface Props {
  value: OutputMode;
  onChange: (v: OutputMode) => void;
  disabled?: boolean;
}

export function OutputSelector({ value, onChange, disabled }: Props) {
  return (
    <Card title="Output" icon={<Speaker className="h-4 w-4" />}>
      <div className="grid grid-cols-2 gap-3">
        {OPTIONS.map(({ id, label, hint, icon: Icon }) => {
          const active = value === id;
          return (
            <button
              key={id}
              type="button"
              disabled={disabled}
              onClick={() => onChange(id)}
              className={cn(
                "group flex flex-col items-start gap-2 rounded-xl border p-4 text-left transition disabled:opacity-50",
                active
                  ? "border-accent/60 bg-accent/10 shadow-glow"
                  : "border-white/5 bg-white/[0.03] hover:border-white/15 hover:bg-white/[0.06]",
              )}
            >
              <Icon
                className={cn("h-5 w-5 transition", active ? "text-accent-soft" : "text-slate-400")}
              />
              <div>
                <div className="font-semibold text-slate-100">{label}</div>
                <div className="text-xs text-slate-500">{hint}</div>
              </div>
            </button>
          );
        })}
      </div>
    </Card>
  );
}
