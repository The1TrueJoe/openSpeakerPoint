import { Header } from "./components/Header";
import { NowPlaying } from "./components/NowPlaying";
import { OutputSelector } from "./components/OutputSelector";
import { VolumeControl } from "./components/VolumeControl";
import { TestTonePanel } from "./components/TestTonePanel";
import { MediaLibrary } from "./components/MediaLibrary";
import { useDevice } from "./hooks/useDevice";

export function App() {
  const { state, output, volume } = useDevice();
  const online = state.isSuccess;
  const current = state.data ?? { volume: 0, output: "off" as const };

  return (
    <div className="mx-auto max-w-5xl px-4 py-6 sm:px-6 sm:py-10">
      <Header online={online} />

      <div className="grid gap-4 lg:grid-cols-2">
        <div className="space-y-4">
          <NowPlaying />
          <MediaLibrary />
        </div>

        <div className="space-y-4">
          <OutputSelector
            value={current.output}
            onChange={(v) => output.mutate(v)}
            disabled={!online || output.isPending}
          />
          <VolumeControl
            value={current.volume}
            onCommit={(v) => volume.mutate(v)}
            disabled={!online || current.output === "off"}
          />
          <TestTonePanel />
        </div>
      </div>

      <footer className="mt-8 text-center text-xs text-slate-600">
        Not affiliated with or endorsed by Control4.
      </footer>
    </div>
  );
}
