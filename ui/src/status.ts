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
export type ErrorKind = "none" | "formatNotSupported" | "output" | "load" | "dopNotAllowed" | "other";

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

export interface DropoutCounts {
  lateBuffers: number; // device side: buffers not ready in time
  deviceTimeouts: number; // device side: the device did not ask for data
  readStarvations: number; // read-ahead side: data not read from disk in time
  starvedFrames: number;
  realtimePriority: boolean; // playback thread ran with MMCSS "Pro Audio"
}

export interface DopInfo {
  dsdRate: number; // e.g. 2822400 (DSD64)
  container: string; // "DSF" or "DFF"
}

export type OutputApi = "dummy" | "wasapiExclusive" | "asio";
export type SampleLayout = "interleaved" | "perChannel" | "perChannelRightAligned";

export interface AsioPathInfo {
  driverName: string;
  driverVersion: number;
  sampleType: string; // e.g. "Int32LSB24"
  rateChanged: boolean; // the driver's rate was switched for this track
  rateBefore: number; // Hz
}

export interface EndpointVolume {
  scalar: number; // 0..1
  muted: boolean;
  hardware: boolean; // applied inside the device (otherwise it has no effect in exclusive mode)
}

// Facts about the path that are not elsewhere in the status. Set once the output is open.
export interface SignalPath {
  container: string; // "WAV", "DSF", "DFF"
  processing: boolean; // phase 1: always false (direct path)
  output: {
    api: OutputApi;
    periodFrames: number;
    periodMilliseconds: number;
    eventDriven: boolean;
    layout: SampleLayout;
    asio: AsioPathInfo | null;
    endpointVolume: EndpointVolume | null; // WASAPI, when the endpoint has a volume control
  };
}

export interface PlaybackStatus {
  state: PlayerState;
  path: SignalPath | null;
  dropouts: DropoutCounts;
  dop: DopInfo | null; // set when the current track is DSD sent as DoP
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
  path: null,
  dropouts: { lateBuffers: 0, deviceTimeouts: 0, readStarvations: 0, starvedFrames: 0, realtimePriority: false },
  dop: null,
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
