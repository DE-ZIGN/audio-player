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
export type ErrorKind = "none" | "formatNotSupported" | "output" | "other";

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

export interface PlaybackStatus {
  state: PlayerState;
  fileName: string;
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
  sourceFormat: null,
  outputFormat: null,
  repack: "none",
  outputName: "",
  positionFrames: 0,
  totalFrames: 0,
  error: { kind: "none", reason: "none", outputError: "deviceError", detail: "", acceptedRates: [] },
};
