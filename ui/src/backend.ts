// Connection to the C++ side. Inside the app, JUCE's WebBrowserComponent
// provides window.__JUCE__ and native functions; in a plain browser (Vite dev
// server opened directly) a mock backend simulates playback so the page can
// be worked on without the app.

import { emptyStatus, type PlaybackStatus, type SignalPath } from "./status";

export interface OpenResult {
  ok: boolean;
  cancelled?: boolean;
  message?: string;
}

export interface Backend {
  readonly kind: "app" | "mock";
  openFile(): Promise<OpenResult>;
  play(): Promise<void>;
  stop(): Promise<void>;
  seek(frame: number): Promise<void>;
  getStatus(): Promise<PlaybackStatus>;
  uiReady(): Promise<void>;
  selfTestResult(result: { ok: boolean; log: string[] }): Promise<void>;
  onStatus(listener: (status: PlaybackStatus) => void): void;
}

declare global {
  interface Window {
    __JUCE__?: {
      backend: {
        addEventListener(id: string, fn: (payload: unknown) => void): unknown;
        emitEvent(id: string, payload: unknown): void;
      };
      initialisationData: Record<string, unknown>;
    };
  }
}

export const STATUS_EVENT = "playbackStatus";

class AppBackend implements Backend {
  readonly kind = "app" as const;
  constructor(private readonly fn: (name: string) => (...args: unknown[]) => Promise<unknown>) {}

  async openFile(): Promise<OpenResult> {
    return (await this.fn("openFile")()) as OpenResult;
  }
  async play() {
    await this.fn("play")();
  }
  async stop() {
    await this.fn("stop")();
  }
  async seek(frame: number) {
    await this.fn("seek")(Math.max(0, Math.round(frame)));
  }
  async getStatus() {
    return (await this.fn("getStatus")()) as PlaybackStatus;
  }
  async uiReady() {
    await this.fn("uiReady")();
  }
  async selfTestResult(result: { ok: boolean; log: string[] }) {
    await this.fn("selfTestResult")(result);
  }
  onStatus(listener: (status: PlaybackStatus) => void) {
    window.__JUCE__!.backend.addEventListener(STATUS_EVENT, (p) => listener(p as PlaybackStatus));
  }
}

export interface MockTrack {
  name: string;
  sampleRate: number;
  seconds: number;
  dsdRate?: number; // DSD file sent as DoP at sampleRate (= dsdRate / 16)
}

const fmt24 = (sampleRate: number) => ({
  sampleRate,
  channels: 2,
  containerBits: 24,
  validBits: 24,
  sampleType: "int" as const,
});

// What a WASAPI exclusive output reports for a track (10 ms periods).
const mockPath = (t: MockTrack): SignalPath => ({
  container: t.dsdRate ? "DSF" : t.name.endsWith(".flac") ? "FLAC" : "WAV",
  processing: false,
  output: {
    api: "wasapiExclusive",
    periodFrames: t.sampleRate / 100,
    periodMilliseconds: 10,
    eventDriven: true,
    layout: "interleaved",
    asio: null,
    endpointVolume: { scalar: 1, muted: false, hardware: false },
  },
});

// Three tracks with a rate change, for working on the page in a browser.
export const DEMO_QUEUE: MockTrack[] = [
  { name: "（試験用）01_44k.flac", sampleRate: 44100, seconds: 8 },
  { name: "（試験用）02_44k.flac", sampleRate: 44100, seconds: 8 },
  { name: "（試験用）03_96k.wav", sampleRate: 96000, seconds: 8 },
  { name: "（試験用）04_dsd64.dsf", sampleRate: 176400, seconds: 8, dsdRate: 2822400 },
];

// Simulated player for working on the page in a normal browser.
export class MockBackend implements Backend {
  readonly kind = "mock" as const;
  private status: PlaybackStatus = { ...emptyStatus };
  private listeners: ((s: PlaybackStatus) => void)[] = [];
  private timer: ReturnType<typeof setInterval> | undefined;

  constructor(private readonly tracks: MockTrack[] = [{ name: "（試験用）sine_96k_24bit.wav", sampleRate: 96000, seconds: 30 }]) {}

  async openFile(): Promise<OpenResult> {
    this.halt();
    this.update({ ...emptyStatus, state: "stopped", queueLength: this.tracks.length, ...this.trackFields(0) });
    return { ok: true };
  }

  private trackFields(index: number) {
    const t = this.tracks[index];
    return {
      queueIndex: index,
      fileName: t.name,
      sourceFormat: fmt24(t.sampleRate),
      dop: t.dsdRate ? { dsdRate: t.dsdRate, container: "DSF" } : null,
      outputFormat: null,
      path: null,
      // FLAC: pending until the read-ahead has decoded the whole file (shown as passed once playing).
      integrity: { state: t.name.endsWith(".flac") ? ("pending" as const) : ("notAvailable" as const), decodeErrors: 0 },
      totalFrames: Math.round(t.sampleRate * t.seconds),
      positionFrames: 0,
    };
  }

  async play() {
    if (this.status.state === "empty" || this.status.state === "playing") return;
    if (this.status.state === "ended") this.update({ ...this.status, ...this.trackFields(0), transitions: [] });
    this.update({
      ...this.status,
      state: "playing",
      outputName: "（模擬）出力デバイス",
      outputFormat: this.status.sourceFormat && { ...this.status.sourceFormat, containerBits: 32 },
      path: mockPath(this.tracks[this.status.queueIndex]),
      integrity: this.status.integrity.state === "pending" ? { state: "passed", decodeErrors: 0 } : this.status.integrity,
      repack: "widerContainer",
      dropouts: { ...this.status.dropouts, realtimePriority: true },
    });
    this.timer = setInterval(() => {
      const rate = this.status.sourceFormat?.sampleRate ?? 48000;
      const next = this.status.positionFrames + rate / 10;
      if (next < this.status.totalFrames) {
        this.update({ ...this.status, positionFrames: next });
        return;
      }
      const from = this.status.queueIndex;
      if (from + 1 >= this.tracks.length) {
        this.halt();
        this.update({ ...this.status, state: "ended", positionFrames: this.status.totalFrames });
        return;
      }
      const fields = this.trackFields(from + 1);
      const gapless = fields.sourceFormat.sampleRate === this.status.sourceFormat?.sampleRate;
      this.update({
        ...this.status,
        ...fields,
        outputFormat: { ...fields.sourceFormat, containerBits: 32 },
        path: mockPath(this.tracks[from + 1]),
        transitions: [
          ...this.status.transitions,
          {
            fromIndex: from,
            toIndex: from + 1,
            kind: gapless ? "gapless" : "reopened",
            fromFormat: this.status.sourceFormat!,
            toFormat: fields.sourceFormat,
            switchMilliseconds: gapless ? 0 : 40,
          },
        ],
      });
    }, 100);
  }
  async stop() {
    if (this.status.state !== "playing") return;
    this.halt();
    this.update({ ...this.status, state: "stopped" });
  }
  async seek(frame: number) {
    if (this.status.state === "empty") return;
    const f = Math.min(Math.max(0, Math.round(frame)), this.status.totalFrames);
    this.update({ ...this.status, positionFrames: f, state: this.status.state === "ended" ? "stopped" : this.status.state });
  }
  async getStatus() {
    return this.status;
  }
  async uiReady() {}
  async selfTestResult(result: { ok: boolean; log: string[] }) {
    console.info("self test", result);
  }
  onStatus(listener: (status: PlaybackStatus) => void) {
    this.listeners.push(listener);
  }

  private halt() {
    if (this.timer !== undefined) clearInterval(this.timer);
    this.timer = undefined;
  }
  private update(s: PlaybackStatus) {
    this.status = s;
    for (const l of this.listeners) l(s);
  }
}

export async function createBackend(): Promise<Backend> {
  if (!window.__JUCE__) return new MockBackend(DEMO_QUEUE);
  // JUCE's helper reads window.__JUCE__ when it is loaded, so import it only inside the app.
  const juce = await import("./juce/index.js");
  return new AppBackend((name) => juce.getNativeFunction(name) as (...args: unknown[]) => Promise<unknown>);
}
