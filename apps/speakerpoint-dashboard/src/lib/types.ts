/** Shared types describing the speakerpoint-control backend API. */

export type OutputMode = "off" | "rca" | "amp" | "both";
/** User-selectable input. AirPlay is automatic (whenever a stream arrives),
 * not a manual choice, so it's not part of this type. */
export type SourceMode = "media" | "linein";

export interface DeviceState {
  volume: number; // 0-100
  output: OutputMode;
  source: SourceMode;
}

export type ToneChannel = "left" | "right" | "both";

export type PlaybackState = "play" | "pause" | "stop";
/** The source actually feeding the output right now. AirPlay pre-empts the
 * manual SourceMode selection whenever a stream is active. */
export type NowPlayingSource = "media" | "airplay" | "linein";

export interface NowPlaying {
  source: NowPlayingSource;
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

export type SortField = "title" | "artist" | "album";
export type SortDir = "asc" | "desc";

export interface UploadItem {
  id: string;
  name: string;
  progress: number; // 0-100
  status: "uploading" | "done" | "error";
  error?: string;
}
