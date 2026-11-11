// Connection to the C++ side. Inside the app, JUCE's WebBrowserComponent
// provides window.__JUCE__ and native functions; in a plain browser (Vite dev
// server opened directly) a mock backend simulates playback so the page can
// be worked on without the app.

import { emptyStatus, type BitPerfectVerdict, type MeterStatus, type PlaybackStatus, type SignalPath } from "./status";
import type {
  ActionResult,
  AlbumQuery,
  AlbumRow,
  Folder,
  LibraryCounts,
  Page,
  TrackQuery,
  TrackRow,
} from "./library";
import { MockLibrary } from "./mockLibrary";

// The play queue as the app holds it (task 6-2). id 0: not from the library.
export interface QueueItemInfo {
  id: number;
  name: string;
  artist: string;
  seconds: number | null;
}

export interface QueueInfo {
  version: number;
  items: QueueItemInfo[];
}

export interface OpenResult {
  ok: boolean;
  cancelled?: boolean;
  message?: string;
}

// The library (task 6-1), see app/src/LibraryBridge.h.
export interface LibraryApi {
  folders(): Promise<Folder[]>;
  addFolder(): Promise<ActionResult>; // shows a folder dialog
  removeFolder(id: number): Promise<void>;
  tracks(q: TrackQuery): Promise<Page<TrackRow>>;
  albums(q: AlbumQuery): Promise<Page<AlbumRow>>;
  counts(): Promise<LibraryCounts>;
  play(ids: number[], start: number): Promise<ActionResult>; // these tracks as the queue
  replaceUpcoming(ids: number[]): Promise<void>; // these tracks after the current one
  coverUrl(trackId: number): string;
  onChanged(listener: (c: LibraryCounts) => void): void;
}

export interface Backend {
  readonly kind: "app" | "mock";
  readonly library: LibraryApi;
  openFile(): Promise<OpenResult>;
  play(): Promise<void>;
  stop(): Promise<void>;
  seek(frame: number): Promise<void>;
  getStatus(): Promise<PlaybackStatus>;
  playAt(index: number): Promise<void>;
  getQueue(): Promise<QueueInfo>;
  setVolume(db: number | "-inf"): Promise<void>;
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
export const LIBRARY_EVENT = "libraryChanged";

type NativeFn = (name: string) => (...args: unknown[]) => Promise<unknown>;

class AppLibrary implements LibraryApi {
  constructor(
    private readonly fn: NativeFn,
    private readonly coverBase: string,
  ) {}
  async folders() {
    return ((await this.fn("libraryFolders")()) as Folder[] | null) ?? [];
  }
  async addFolder() {
    return (await this.fn("libraryAddFolder")()) as ActionResult;
  }
  async removeFolder(id: number) {
    await this.fn("libraryRemoveFolder")(id);
  }
  async tracks(q: TrackQuery) {
    return ((await this.fn("libraryTracks")(q)) as Page<TrackRow> | null) ?? { rows: [], total: 0 };
  }
  async albums(q: AlbumQuery) {
    return ((await this.fn("libraryAlbums")(q)) as Page<AlbumRow> | null) ?? { rows: [], total: 0 };
  }
  async counts() {
    return (await this.fn("libraryCounts")()) as LibraryCounts;
  }
  async play(ids: number[], start: number) {
    return ((await this.fn("libraryPlay")(ids, start)) as ActionResult | null) ?? { ok: false };
  }
  async replaceUpcoming(ids: number[]) {
    await this.fn("libraryReplaceUpcoming")(ids);
  }
  coverUrl(trackId: number) {
    return `${this.coverBase}${trackId}.jpg`;
  }
  onChanged(listener: (c: LibraryCounts) => void) {
    window.__JUCE__!.backend.addEventListener(LIBRARY_EVENT, (p) => listener(p as LibraryCounts));
  }
}

class AppBackend implements Backend {
  readonly kind = "app" as const;
  readonly library: LibraryApi;
  constructor(
    private readonly fn: NativeFn,
    coverBase: string,
  ) {
    this.library = new AppLibrary(fn, coverBase);
  }

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
  async playAt(index: number) {
    await this.fn("playAt")(index);
  }
  async getQueue() {
    return ((await this.fn("getQueue")()) as QueueInfo | null) ?? { version: 0, items: [] };
  }
  async setVolume(db: number | "-inf") {
    await this.fn("setVolume")(db);
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
  processingStages: [],
  processingRequired: false,
  dither: false,
  comparison: false,
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

// The direct path: bit-perfect (DoP for DSD).
const mockVerdict = (t: MockTrack): BitPerfectVerdict => ({
  bitPerfect: true,
  quantized: false,
  dither: false,
  comparison: false,
  repacked: true, // the mock device takes 24-bit in a 32-bit container (as the repack field says)
  dop: t.dsdRate !== undefined,
  floatSource: false,
  dummyOutput: false,
  deviceVolume: null,
  deviceMuted: false,
  deliveryMismatch: false,
});

// Meter values that move a little, for working on the page in a browser.
const mockMeter = (t: MockTrack, index: number, playedFrames: number, measuring: boolean): MeterStatus => {
  if (t.dsdRate !== undefined) return { state: measuring ? "notPcm" : "idle", queueIndex: index, droppedFrames: 0, reading: null };
  const wobble = Math.sin(playedFrames / t.sampleRate) * 1.5;
  const round = (x: number) => Math.round(x * 100) / 100;
  return {
    state: measuring ? "measuring" : "idle",
    queueIndex: index,
    droppedFrames: 0,
    reading: {
      channels: 2,
      truePeakRecent: [round(-3.1 + wobble), round(-3.8 + wobble)],
      truePeakMax: [-0.92, -1.17],
      momentaryLufs: round(-13.6 + wobble),
      shortTermLufs: round(-14.1 + wobble / 3),
      integratedLufs: playedFrames >= t.sampleRate * 0.4 ? -14.3 : "-inf",
      trackFrames: playedFrames,
    },
  };
};

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
  readonly library: LibraryApi;
  private status: PlaybackStatus = { ...emptyStatus };
  private listeners: ((s: PlaybackStatus) => void)[] = [];
  private timer: ReturnType<typeof setInterval> | undefined;
  private queueItems: QueueItemInfo[] = [];
  private queueVersion = 0;
  private volumeDb: number | "-inf" = 0;

  constructor(
    private tracks: MockTrack[] = [{ name: "（試験用）sine_96k_24bit.wav", sampleRate: 96000, seconds: 30 }],
    mockLibrary: MockLibrary | null = null,
  ) {
    const lib = mockLibrary ?? new MockLibrary(0);
    this.library = {
      folders: async () => lib.listFolders(),
      addFolder: async () => lib.addFolder(),
      removeFolder: async (id) => lib.removeFolder(id),
      tracks: async (q) => lib.trackPage(q),
      albums: async (q) => lib.albumPage(q),
      counts: async () => lib.counts(),
      coverUrl: (id) => lib.coverUrl(id),
      onChanged: (l) => lib.onChanged(l),
      play: async (ids, start) => {
        const found = ids.flatMap((id) => {
          const t = lib.track(id);
          return t ? [t] : [];
        });
        if (found.length === 0) return { ok: false, message: "再生できる曲がありません" };
        this.halt();
        this.tracks = found.map((t) => ({ name: t.title, sampleRate: t.sampleRate ?? 44100, seconds: t.seconds ?? 200 }));
        this.setQueueItems(found.map((t) => ({ id: t.id, name: t.title, artist: t.artist, seconds: t.seconds })));
        const i = Math.min(start, found.length - 1);
        this.update({ ...this.withQueue(emptyStatus), state: "stopped", ...this.trackFields(i) });
        await this.play();
        return { ok: true };
      },
      replaceUpcoming: async (ids) => {
        const cur = this.status.queueIndex;
        const found = ids.flatMap((id) => {
          const t = lib.track(id);
          return t ? [t] : [];
        });
        this.tracks = [
          ...this.tracks.slice(0, cur + 1),
          ...found.map((t) => ({ name: t.title, sampleRate: t.sampleRate ?? 44100, seconds: t.seconds ?? 200 })),
        ];
        this.setQueueItems([
          ...this.queueItems.slice(0, cur + 1),
          ...found.map((t) => ({ id: t.id, name: t.title, artist: t.artist, seconds: t.seconds })),
        ]);
        this.update(this.withQueue(this.status));
      },
    };
  }

  async openFile(): Promise<OpenResult> {
    this.halt();
    this.setQueueItems(this.tracks.map((t) => ({ id: 0, name: t.name, artist: "", seconds: t.seconds })));
    this.update({ ...this.withQueue(emptyStatus), state: "stopped", ...this.trackFields(0) });
    return { ok: true };
  }

  private setQueueItems(items: QueueItemInfo[]) {
    this.queueItems = items;
    this.queueVersion++;
  }

  private withQueue(s: PlaybackStatus): PlaybackStatus {
    return { ...s, queueLength: this.queueItems.length, queueVersion: this.queueVersion, volumeDb: this.volumeDb };
  }

  async playAt(index: number) {
    if (index < 0 || index >= this.tracks.length) return;
    const wasPlaying = this.status.state === "playing";
    this.halt();
    this.update({ ...this.status, ...this.trackFields(index), state: "stopped" });
    if (wasPlaying) await this.play();
  }

  async getQueue() {
    return { version: this.queueVersion, items: this.queueItems.map((i) => ({ ...i })) };
  }

  async setVolume(db: number | "-inf") {
    this.volumeDb = db === "-inf" ? db : Math.min(0, db);
    const processing = this.volumeDb !== 0;
    const s = this.status;
    this.update({
      ...this.withQueue(s),
      stages: processing ? [{ name: "volume", gainDb: this.volumeDb }] : [],
      path: s.path && { ...s.path, processing },
      bitPerfect: s.bitPerfect && { ...s.bitPerfect, bitPerfect: !processing, dither: processing },
    });
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
      bitPerfect: null,
      delivery: null,
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
      bitPerfect: mockVerdict(this.tracks[this.status.queueIndex]),
      delivery: { matchedFrames: 0, mismatchedFrames: 0 },
      integrity: this.status.integrity.state === "pending" ? { state: "passed", decodeErrors: 0 } : this.status.integrity,
      repack: "widerContainer",
      dropouts: { ...this.status.dropouts, realtimePriority: true },
    });
    this.timer = setInterval(() => {
      const rate = this.status.sourceFormat?.sampleRate ?? 48000;
      const next = this.status.positionFrames + rate / 10;
      if (next < this.status.totalFrames) {
        const d = this.status.delivery;
        this.update({
          ...this.status,
          positionFrames: next,
          delivery: d && { ...d, matchedFrames: d.matchedFrames + rate / 10 },
          meter: mockMeter(this.tracks[this.status.queueIndex], this.status.queueIndex, next, true),
        });
        return;
      }
      const from = this.status.queueIndex;
      if (from + 1 >= this.tracks.length) {
        this.halt();
        this.update({
          ...this.status,
          state: "ended",
          positionFrames: this.status.totalFrames,
          meter: { ...this.status.meter, state: "idle" },
        });
        return;
      }
      const fields = this.trackFields(from + 1);
      const gapless = fields.sourceFormat.sampleRate === this.status.sourceFormat?.sampleRate;
      this.update({
        ...this.status,
        ...fields,
        outputFormat: { ...fields.sourceFormat, containerBits: 32 },
        path: mockPath(this.tracks[from + 1]),
        bitPerfect: mockVerdict(this.tracks[from + 1]),
        // A new output session when the format changes; gapless keeps counting.
        delivery: gapless ? this.status.delivery : { matchedFrames: 0, mismatchedFrames: 0 },
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
    this.update({ ...this.status, state: "stopped", meter: { ...this.status.meter, state: "idle" } });
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
  if (!window.__JUCE__) return new MockBackend(DEMO_QUEUE, new MockLibrary());
  // JUCE's helper reads window.__JUCE__ when it is loaded, so import it only inside the app.
  const juce = await import("./juce/index.js");
  const fn: NativeFn = (name) => juce.getNativeFunction(name) as (...args: unknown[]) => Promise<unknown>;
  const info = (await fn("libraryInfo")()) as { coverBase?: string } | null;
  return new AppBackend(fn, info?.coverBase ?? "/art/track/");
}
