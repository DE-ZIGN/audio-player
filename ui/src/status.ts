// Shape of the playback status the app sends to the UI (event "playbackStatus"
// and native function "getStatus"). Must match app/src/PlayerBridge.cpp.

export type PlayerState = "empty" | "stopped" | "playing" | "ended" | "error";
export type SampleType = "int" | "float";
export type Repack = "none" | "widerContainer" | "zeroPadded";
export type FormatRejection = "none" | "sampleRate" | "bitDepth" | "channels" | "sampleType" | "other";
export type OutputError =
  | "deviceNotFound"
  | "deviceInUse"
  | "exclusiveModeNotAllowed"
  | "formatNotSupported"
  | "deviceInvalidated"
  | "timeout"
  | "deviceError";
export type ErrorKind = "none" | "formatNotSupported" | "output" | "load" | "other";

export interface FormatInfo {
  sampleRate: number;
  channels: number;
  containerBits: number;
  validBits: number;
  sampleType: SampleType;
}

export interface PlaybackError {
  kind: ErrorKind;
  reason: FormatRejection;
  outputError: OutputError;
  detail: string;
  // Sample rates (Hz) the device accepts right now, when a format was refused.
  acceptedRates: number[];
}

export type TransitionKind = "gapless" | "reopened";

export interface TrackTransition {
  fromIndex: number;
  toIndex: number;
  kind: TransitionKind;
  fromFormat: FormatInfo;
  toFormat: FormatInfo;
  // Reopened: time from the end of the previous track's data to the first
  // request for the next track's data (upper bound of the silence).
  switchMilliseconds: number;
}

export interface PlaybackStatus {
  state: PlayerState;
  fileName: string; // name of the current track
  queueIndex: number;
  queueLength: number;
  transitions: TrackTransition[]; // most recent last (the app sends the last few)
  sourceFormat: FormatInfo | null;
  outputFormat: FormatInfo | null;
  repack: Repack;
  outputName: string;
  positionFrames: number;
  totalFrames: number;
  error: PlaybackError;
}

export const emptyStatus: PlaybackStatus = {
  state: "empty",
  fileName: "",
  queueIndex: 0,
  queueLength: 0,
  transitions: [],
  sourceFormat: null,
  outputFormat: null,
  repack: "none",
  outputName: "",
  positionFrames: 0,
  totalFrames: 0,
  error: { kind: "none", reason: "none", outputError: "deviceError", detail: "", acceptedRates: [] },
};
