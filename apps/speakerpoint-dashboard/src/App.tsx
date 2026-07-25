import { useState } from "react";
import { Sidebar } from "@/components/Sidebar";
import { NowPlayingPage } from "@/pages/NowPlayingPage";
import { LibraryPage } from "@/pages/LibraryPage";
import { SettingsPage } from "@/pages/SettingsPage";
import { useDevice } from "@/hooks/useDevice";

export type View = "now-playing" | "library" | "settings";

export function App() {
  const [view, setView] = useState<View>("now-playing");
  const { state } = useDevice();

  return (
    <div className="flex min-h-screen">
      <Sidebar view={view} onChange={setView} online={state.isSuccess} />
      <main className="flex min-h-screen flex-1 flex-col pb-44 md:pb-0">
        {view === "now-playing" && <NowPlayingPage />}
        {view === "library" && <LibraryPage />}
        {view === "settings" && <SettingsPage />}
      </main>
    </div>
  );
}
