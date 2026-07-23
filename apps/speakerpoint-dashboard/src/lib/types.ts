/** Shared types describing the speakerpoint-control backend API. */

export type OutputMode = "off" | "rca" | "amp" | "both";

export interface DeviceState {
  volume: number; // 0-100
  output: OutputMode;
}

export type ToneChannel = "left" | "right" | "both";

export type PlaybackState = "play" | "pause" | "stop";

export interface NowPlaying {
  state: PlaybackState;
  file: string | null;
  title: string | null;
  artist: string | null;
  album: string | null;
  elapsed: number; // seconds
  duration: number; // seconds
  hasArt: boolean;
}

export interface Track {
  file: string;
  title: string | null;
  artist: string | null;
  album: string | null;
  duration: number;
  hasArt: boolean;
}

export interface Library {
  usb: boolean;
  updating: boolean;
  tracks: Track[];
}
