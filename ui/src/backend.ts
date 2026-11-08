// Connection to the C++ side. Inside the app, JUCE's WebBrowserComponent
// provides window.__JUCE__ and native functions; in a plain browser (Vite dev
// server opened directly) a mock backend simulates playback so the page can
// be worked on without the app.

import { emptyStatus, type PlaybackStatus } from "./status";

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

// Simulated player for working on the page in a normal browser.
export class MockBackend implements Backend {
  readonly kind = "mock" as const;
  private status: PlaybackStatus = { ...emptyStatus };
  private listeners: ((s: PlaybackStatus) => void)[] = [];
  private timer: ReturnType<typeof setInterval> | undefined;

  async openFile(): Promise<OpenResult> {
    this.halt();
    const format = { sampleRate: 96000, channels: 2, containerBits: 24, validBits: 24, sampleType: "int" as const };
    this.update({
      ...emptyStatus,
      state: "stopped",
      fileName: "（試験用）sine_96k_24bit.wav",
      sourceFormat: format,
      totalFrames: 96000 * 30,
    });
    return { ok: true };
  }
  async play() {
    if (this.status.state === "empty" || this.status.state === "playing") return;
    const restart = this.status.state === "ended";
    this.update({
      ...this.status,
      state: "playing",
      positionFrames: restart ? 0 : this.status.positionFrames,
      outputName: "（模擬）出力デバイス",
      outputFormat: this.status.sourceFormat && { ...this.status.sourceFormat, containerBits: 32 },
      repack: "widerContainer",
    });
    const rate = this.status.sourceFormat?.sampleRate ?? 48000;
    this.timer = setInterval(() => {
      const next = this.status.positionFrames + rate / 10;
      if (next >= this.status.totalFrames) {
        this.halt();
        this.update({ ...this.status, state: "ended", positionFrames: this.status.totalFrames });
      } else {
        this.update({ ...this.status, positionFrames: next });
      }
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
  if (!window.__JUCE__) return new MockBackend();
  // JUCE's helper reads window.__JUCE__ when it is loaded, so import it only inside the app.
  const juce = await import("./juce/index.js");
  return new AppBackend((name) => juce.getNativeFunction(name) as (...args: unknown[]) => Promise<unknown>);
}
