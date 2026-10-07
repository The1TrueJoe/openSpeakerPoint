import { MeasurePanel } from "@/components/MeasurePanel";
import { RestorePanel } from "@/components/RestorePanel";
import { TestTonePanel } from "@/components/TestTonePanel";

export function SettingsPage() {
  return (
    <div className="mx-auto max-w-2xl px-4 py-6 sm:px-6 sm:py-10">
      <header className="mb-6">
        <h1 className="text-xl font-extrabold tracking-tight text-slate-50">Settings</h1>
        <p className="text-sm text-slate-500">
          Diagnostics. Source, output routing, and volume live in the sidebar.
        </p>
      </header>

      <div className="space-y-4">
        <TestTonePanel />
        <MeasurePanel />
        <RestorePanel />
      </div>

      <footer className="mt-8 text-center text-xs text-slate-600">
        Not affiliated with or endorsed by Control4 or Apple. AirPlay is a trademark of Apple Inc.
      </footer>
    </div>
  );
}
