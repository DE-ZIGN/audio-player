// Shape of the playback status the app sends to the UI (event "playbackStatus"
// and native function "getStatus"). Must match app/src/PlayerBridge.cpp.

import type { NativeText } from "./nativeText";
import type { TubeAutoStatus } from "./tubeAuto";

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

// processingChanged: within one track, processing was switched on or off (e.g. the volume left 100%).
export type TransitionKind = "gapless" | "reopened" | "processingChanged";

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
  processing: boolean; // false: the direct path (bit-perfect); true: the 64-bit processing path
  processingStages: string[]; // active stages when the output opened, e.g. ["volume"]
  processingRequired: boolean; // a float source on an output without float: quantized
  dither: boolean; // TPDF dither when quantizing
  comparison: boolean; // the listening comparison (A/B, ABX)
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

// Checksum check of the decoded samples (FLAC: the MD5 stored in the file).
export type IntegrityState = "notAvailable" | "pending" | "passed" | "failed" | "skipped";

export interface IntegrityCheck {
  state: IntegrityState;
  decodeErrors: number; // problems the decoder reported in the file
}

// A stage running on the processing path now, with the level it applies.
export interface StageState {
  name: string; // "volume", "replaygain", "eq", "isp-safe", "limiter", "comparison"
  gainDb: number | "-inf" | null; // the EQ's and the tone's: the automatic preamp; the tube's: DRIVE
  detail?: string; // the tube's type ("se300b", "ppel34", "ppclassa")
}

// The BIT-PERFECT judgement made by the engine from the facts of the path
// (from the application to the driver).
export interface BitPerfectVerdict {
  bitPerfect: boolean;
  quantized: boolean; // a float source made integer for the output
  dither: boolean;
  comparison: boolean;
  repacked: boolean; // container widened, values unchanged
  dop: boolean;
  floatSource: boolean; // the decoded values of a lossy source, as they are
  dummyOutput: boolean;
  deviceVolume: number | null; // the device's own volume (0..1) when not 100%; after the driver
  deviceMuted: boolean;
  deliveryMismatch: boolean; // the self-check found values in the driver's buffers that differ
}

// The self-check of the direct path (task 4-3): frames of this output session
// compared between the values read and what reached the driver's buffers.
export interface DeliveryResult {
  matchedFrames: number;
  mismatchedFrames: number;
}

// A level in dB (dBTP or LUFS), unrounded (the page rounds for display), or "-inf" for silence.
export type Level = number | "-inf";

// The meters on what goes to the output (task 4-4): the values handed to the
// output layer, after processing on the processing path.
export interface MeterReading {
  channels: number;
  truePeakRecent: Level[]; // dBTP per channel, about the last 0.1 s
  truePeakMax: Level[]; // dBTP per channel, since the track started
  vu: Level[]; // dBFS per channel: a sine's RMS with a VU meter's 300 ms ballistics (task 6-8)
  momentaryLufs: Level; // 400 ms
  shortTermLufs: Level; // 3 s
  integratedLufs: Level; // since the track started (gated)
  trackFrames: number; // measured since the track started
}

// idle: no output session (the last values are kept); notPcm: DoP, not measured.
export type MeterState = "idle" | "measuring" | "notPcm";

export interface MeterStatus {
  state: MeterState;
  queueIndex: number; // the track the reading's track values belong to
  droppedFrames: number; // not measured because the meter fell behind (the sound is not affected)
  reading: MeterReading | null;
}

import type { AbxStatus } from "./abx";

// The safety limiter since the settings or the queue last changed (task 6-3).
export interface LimiterStats {
  events: number;
  limitedFrames: number;
  maxReductionDb: number; // 0 or negative
}

export interface PlaybackStatus {
  state: PlayerState;
  limiter: LimiterStats;
  eqSolo: number | null; // the EQ band played alone (task 6-3), not saved
  // Task 6-4: the mode, the ISP-safe gain of that mode, and the current
  // track's measured true peak (null: not measured), for the ISP warning.
  mode: "original" | "processed";
  ispSafe: "off" | "measured" | "fixed";
  trackTruePeakDb: number | null;
  abx: AbxStatus | null; // an ABX running or just ended (task 6-6)
  path: SignalPath | null;
  stages: StageState[]; // processing path: the stages running now
  tubeAuto?: TubeAutoStatus; // the tube type by genre (task 9-7): the current track's album and choice
  tubeWarmUpSeconds?: number; // since the app started: the tube's warm-up counts from it (task 9-8)
  bitPerfect: BitPerfectVerdict | null; // set with path
  delivery: DeliveryResult | null; // direct path only
  meter: MeterStatus;
  integrity: IntegrityCheck; // of the current track
  integrityFailures: number[]; // queue indices of earlier tracks whose check failed
  dropouts: DropoutCounts;
  dop: DopInfo | null; // set when the current track is DSD sent as DoP
  fileName: string; // name of the current track
  queueIndex: number;
  queueLength: number;
  queueVersion: number; // changes when the queue's tracks change (fetch them with getQueue)
  volumeDb: number | "-inf"; // the digital volume (0: 100%)
  transitions: TrackTransition[]; // most recent last (the app sends the last few)
  sourceFormat: FormatInfo | null;
  outputFormat: FormatInfo | null;
  repack: Repack;
  outputName: string;
  // Why playback is not on the chosen output, or why it could not start there
  // (task 6-3: the chosen device is gone, an ASIO driver failed its check):
  // "" or a code and its values (task 11-3, nativeText.ts).
  outputNotice: NativeText;
  positionFrames: number;
  totalFrames: number;
  error: PlaybackError;
}

export const emptyStatus: PlaybackStatus = {
  state: "empty",
  limiter: { events: 0, limitedFrames: 0, maxReductionDb: 0 },
  eqSolo: null,
  mode: "original",
  ispSafe: "off",
  trackTruePeakDb: null,
  abx: null,
  path: null,
  stages: [],
  bitPerfect: null,
  delivery: null,
  meter: { state: "idle", queueIndex: 0, droppedFrames: 0, reading: null },
  integrity: { state: "notAvailable", decodeErrors: 0 },
  integrityFailures: [],
  dropouts: { lateBuffers: 0, deviceTimeouts: 0, readStarvations: 0, starvedFrames: 0, realtimePriority: false },
  dop: null,
  fileName: "",
  queueIndex: 0,
  queueLength: 0,
  queueVersion: 0,
  volumeDb: 0,
  transitions: [],
  sourceFormat: null,
  outputFormat: null,
  repack: "none",
  outputName: "",
  outputNotice: "",
  positionFrames: 0,
  totalFrames: 0,
  error: { kind: "none", reason: "none", outputError: "deviceError", detail: "", acceptedRates: [] },
};
