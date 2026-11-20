// Connection to the C++ side. Inside the app, JUCE's WebBrowserComponent
// provides window.__JUCE__ and native functions; in a plain browser (Vite dev
// server opened directly) a mock backend simulates playback so the page can
// be worked on without the app.
//
// The app's messages are codes and values (task 11-3, nativeText.ts); the mock
// sends the same codes, so the page shows the same sentences. The file
// dialogs' titles are the page's, in its language.

import type { EqPreset } from "./eqPresets";
import { currentLang, t } from "./i18n";
import type { NativeText } from "./nativeText";
import { chooseTubeType, type TubeAutoSettings } from "./tubeAuto";
import type { RepeatMode } from "./playModes";
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
import { MOCK_IR_STATUS, mockIrReadable, MockSettings } from "./mockSettings";
import { readerAddress } from "./reader/links";
import { playingSpaceSource } from "./settings";
import { upperTailP, type AbxChoice, type AbxRequest, type AbxStatus } from "./abx";
import type {
  EqBand,
  EqCurve,
  EqImport,
  IrChoice,
  ListeningSettings,
  OutputChoice,
  OutputsInfo,
  SetResult,
  SettingsInfo,
  PresetsResult,
  TubeAutoResult,
} from "./settings";

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
  message?: NativeText;
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

// The settings screen (task 6-3), see app/src/SettingsBridge.h.
export interface SettingsApi {
  get(): Promise<SettingsInfo>;
  setListening(l: ListeningSettings): Promise<SetResult>;
  outputs(): Promise<OutputsInfo>;
  setOutput(o: OutputChoice): Promise<SetResult>; // an ASIO driver is checked first (seconds)
  importEq(): Promise<EqImport>; // shows a file dialog
  chooseIr(): Promise<IrChoice>; // a file dialog for a space IR (task 9-6)
  eqCurve(bands: EqBand[]): Promise<EqCurve>; // the engine's (the page draws with eqResponse.ts)
  eqSolo(band: number | null): Promise<void>; // play one band alone; not saved
  setEqPresets(list: EqPreset[]): Promise<PresetsResult>; // the whole list (task 6-9); saved
  setTubeAuto(t: TubeAutoSettings): Promise<TubeAutoResult>; // the tube type by genre (task 9-7); applied, saved
  onOutputsChanged(listener: () => void): void;
}

// The ABX (task 6-6), see app/src/PlayerBridge.h. Its state comes with the status.
export interface AbxApi {
  start(r: AbxRequest): Promise<{ ok: boolean; message?: NativeText; matchDb?: number }>;
  choose(c: AbxChoice): Promise<void>;
  answer(xIs: "A" | "B"): Promise<void>;
  abort(): Promise<void>;
  close(): Promise<void>; // forget the result shown
}

export interface Backend {
  readonly kind: "app" | "mock";
  readonly library: LibraryApi;
  readonly settings: SettingsApi;
  readonly abx: AbxApi;
  openFile(): Promise<OpenResult>;
  play(): Promise<void>;
  stop(): Promise<void>;
  seek(frame: number): Promise<void>;
  getStatus(): Promise<PlaybackStatus>;
  playAt(index: number): Promise<void>;
  getQueue(): Promise<QueueInfo>;
  setVolume(db: number | "-inf"): Promise<void>;
  setRepeat(mode: RepeatMode): Promise<void>; // task 9-9 (shuffle is the page's: playModes.ts)
  uiReady(): Promise<void>;
  selfTestResult(result: { ok: boolean; log: string[] }): Promise<void>;
  onStatus(listener: (status: PlaybackStatus) => void): void;
  // The article reader (task 7-1): a window of its own. No id: the list of articles.
  openArticle(id?: string, section?: string): Promise<{ ok: boolean; message?: NativeText }>;
  // A link in an article asking for a screen (articleCommand.ts reads the payload).
  onArticleCommand(listener: (payload: unknown) => void): void;
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
export const OUTPUTS_EVENT = "outputsChanged";
export const ARTICLE_COMMAND_EVENT = "articleCommand";

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
    return (await this.fn("libraryAddFolder")(t("page.chooser.addFolder"))) as ActionResult;
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

class AppSettings implements SettingsApi {
  constructor(private readonly fn: NativeFn) {}
  async get() {
    return (await this.fn("settingsGet")()) as SettingsInfo;
  }
  async setListening(l: ListeningSettings) {
    return ((await this.fn("settingsSetListening")(l)) as SetResult | null) ?? { ok: false };
  }
  async outputs() {
    return ((await this.fn("settingsOutputs")()) as OutputsInfo | null) ?? { wasapi: [], asio: [] };
  }
  async setOutput(o: OutputChoice) {
    return ((await this.fn("settingsSetOutput")(o)) as SetResult | null) ?? { ok: false };
  }
  async importEq() {
    return ((await this.fn("settingsImportEq")(t("page.chooser.eq"))) as EqImport | null) ?? { ok: false };
  }
  async chooseIr() {
    return ((await this.fn("settingsChooseIr")(t("page.chooser.ir"))) as IrChoice | null) ?? { ok: false };
  }
  async eqCurve(bands: EqBand[]) {
    return (await this.fn("settingsEqCurve")(bands)) as EqCurve;
  }
  async eqSolo(band: number | null) {
    await this.fn("settingsEqSolo")(band ?? -1);
  }
  async setEqPresets(list: EqPreset[]) {
    return ((await this.fn("settingsSetEqPresets")(list)) as PresetsResult | null) ?? { ok: false };
  }
  async setTubeAuto(t: TubeAutoSettings) {
    return ((await this.fn("settingsSetTubeAuto")(t)) as TubeAutoResult | null) ?? { ok: false };
  }
  onOutputsChanged(listener: () => void) {
    window.__JUCE__!.backend.addEventListener(OUTPUTS_EVENT, () => listener());
  }
}

// (Exported for its tests: backend.test.ts gives it native functions of its own.)
export class AppBackend implements Backend {
  readonly kind = "app" as const;
  readonly library: LibraryApi;
  readonly settings: SettingsApi;
  constructor(
    private readonly fn: NativeFn,
    coverBase: string,
  ) {
    this.library = new AppLibrary(fn, coverBase);
    this.settings = new AppSettings(fn);
    this.abx = {
      start: async (r) => ((await fn("abxStart")(r)) as { ok: boolean; message?: NativeText; matchDb?: number } | null) ?? { ok: false },
      choose: async (c) => void (await fn("abxChoose")(c)),
      answer: async (x) => void (await fn("abxAnswer")(x)),
      abort: async () => void (await fn("abxAbort")()),
      close: async () => void (await fn("abxClose")()),
    };
  }
  readonly abx: AbxApi;

  async openFile(): Promise<OpenResult> {
    return (await this.fn("openFile")(t("page.chooser.openFiles"))) as OpenResult;
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
  async setRepeat(mode: RepeatMode) {
    await this.fn("setRepeat")(mode);
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
  // The language in use goes along: the reader shows its words in it (task 11-5).
  async openArticle(id = "", section = "") {
    return ((await this.fn("openArticle")(id, section, currentLang())) as { ok: boolean; message?: NativeText } | null) ?? { ok: false };
  }
  onArticleCommand(listener: (payload: unknown) => void) {
    window.__JUCE__!.backend.addEventListener(ARTICLE_COMMAND_EVENT, listener);
  }
}

export interface MockTrack {
  name: string;
  sampleRate: number;
  seconds: number;
  dsdRate?: number; // DSD file sent as DoP at sampleRate (= dsdRate / 16)
  truePeakDb?: number | null; // measured at import (the ISP warning, task 6-4)
  container?: string; // from the library ("FLAC", "ALAC", ...)
  genres?: string[]; // for the tube type by genre (task 9-7)
  albumKey?: string;
  album?: string;
}

// Made-up genres per album for the mock library (some not in the default table).
const MOCK_GENRES = [["Jazz"], ["Rock"], ["Classical"], ["ジャズ・ボーカル"], ["Reggae"], []];
const mockGenres = (albumKey: string | null | undefined) => {
  let h = 0;
  for (const c of albumKey ?? "") h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return MOCK_GENRES[h % MOCK_GENRES.length];
};

const fmt24 = (sampleRate: number) => ({
  sampleRate,
  channels: 2,
  containerBits: 24,
  validBits: 24,
  sampleType: "int" as const,
});

// What a WASAPI exclusive output reports for a track (10 ms periods).
const mockPath = (t: MockTrack): SignalPath => ({
  container: t.container || (t.dsdRate ? "DSF" : t.name.endsWith(".flac") ? "FLAC" : "WAV"),
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
      vu: [round(-16.4 + wobble * 2), round(-17.1 + wobble * 2)],
      momentaryLufs: round(-13.6 + wobble),
      shortTermLufs: round(-14.1 + wobble / 3),
      integratedLufs: playedFrames >= t.sampleRate * 0.4 ? -14.3 : "-inf",
      trackFrames: playedFrames,
    },
  };
};

// Three tracks with a rate change, for working on the page in a browser.
export const DEMO_QUEUE: MockTrack[] = [
  { name: "（試験用）01_44k.flac", sampleRate: 44100, seconds: 8, genres: ["Jazz"], albumKey: "demo\u001fDemo", album: "Demo" },
  { name: "（試験用）02_44k.flac", sampleRate: 44100, seconds: 8, genres: ["Rock"], albumKey: "demo\u001fDemo", album: "Demo" },
  { name: "（試験用）03_96k.wav", sampleRate: 96000, seconds: 8 },
  { name: "（試験用）04_dsd64.dsf", sampleRate: 176400, seconds: 8, dsdRate: 2822400 },
];

// Simulated player for working on the page in a normal browser.
export class MockBackend implements Backend {
  readonly kind = "mock" as const;
  readonly library: LibraryApi;
  readonly settings: MockSettings = new MockSettings(
    (l) => this.applyListening(l),
    (band) => this.update({ ...this.status, eqSolo: band }),
    (t) => {
      this.tubeAuto = t;
      this.updateStages();
    },
  );
  private tubeAuto: TubeAutoSettings | null = null;
  // The warm-up (task 9-8) counts from the page's start, ten times as fast, to see it move.
  private readonly startedAt = Date.now();
  private mockWarmUpSeconds(): number {
    return ((Date.now() - this.startedAt) / 1000) * 10;
  }
  private listening: ListeningSettings | null = null;
  private abxX: boolean[] = []; // per trial: X is B
  private abxCorrect = 0;
  readonly abx: AbxApi = {
    start: async (r) => {
      if (this.status.state === "empty") return { ok: false, message: { code: "abx.notPlaying" } };
      if (this.status.dop) return { ok: false, message: { code: "abx.dop" } };
      await new Promise((res) => setTimeout(res, 400)); // the loudness match
      const rate = this.status.sourceFormat?.sampleRate ?? 48000;
      this.abxX = Array.from({ length: r.trials }, () => Math.random() < 0.5);
      this.abxCorrect = 0;
      const abx: AbxStatus = {
        running: true,
        kind: r.kind,
        trial: 1,
        trials: r.trials,
        answered: 0,
        listening: "A",
        startFrames: Math.round(r.startSeconds * rate),
        lengthFrames: Math.round(r.seconds * rate),
        rate,
        matchDb: r.kind === "16bit" ? 0 : r.kind === "presets" ? -1.42 : 3.21,
        ...(r.kind === "presets" ? { aName: r.a?.name, bName: r.b?.name } : {}),
        result: null,
      };
      this.update({ ...this.status, abx, positionFrames: abx.startFrames });
      if (this.status.state !== "playing") await this.play();
      // The comparison takes the processing path.
      const path = this.status.path;
      if (path) this.update({ ...this.status, path: { ...path, processing: true, comparison: true } });
      return { ok: true, matchDb: abx.matchDb };
    },
    choose: async (c) => {
      const a = this.status.abx;
      if (a?.running) this.update({ ...this.status, abx: { ...a, listening: c } });
    },
    answer: async (x) => {
      const a = this.status.abx;
      if (!a?.running) return;
      if ((x === "B") === this.abxX[a.answered]) this.abxCorrect++;
      const answered = a.answered + 1;
      if (answered < a.trials) {
        this.update({ ...this.status, abx: { ...a, answered, trial: answered + 1 } });
        return;
      }
      const p = upperTailP(this.abxCorrect, a.trials);
      this.halt();
      this.update({
        ...this.status,
        state: "stopped",
        abx: {
          ...a,
          answered,
          running: false,
          result: {
            finished: true,
            aborted: false,
            trials: a.trials,
            correct: this.abxCorrect,
            pValue: p,
            distinguished: p < 0.05,
            levelDifferenceDb: 0.012,
            levelMatched: true,
          },
        },
      });
    },
    abort: async () => {
      const a = this.status.abx;
      if (!a?.running) return;
      this.halt();
      this.update({
        ...this.status,
        state: "stopped",
        abx: {
          ...a,
          running: false,
          result: {
            finished: false,
            aborted: true,
            trials: a.trials,
            correct: null,
            pValue: null,
            distinguished: false,
            levelDifferenceDb: null,
            levelMatched: true,
          },
        },
      });
    },
    close: async () => {
      if (this.status.abx && !this.status.abx.running) this.update({ ...this.status, abx: null });
    },
  };
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
        if (found.length === 0) return { ok: false, message: { code: "library.noPlayableTracks" } };
        this.halt();
        this.tracks = found.map((t) => ({
          name: t.title,
          sampleRate: t.sampleRate ?? 44100,
          seconds: t.seconds ?? 200,
          truePeakDb: t.analysis === "ok" ? t.truePeakDb : null,
          container: t.container,
          genres: mockGenres(t.albumKey),
          albumKey: t.albumKey ?? "",
          album: t.album,
        }));
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
          ...found.map((t) => ({
            name: t.title,
            sampleRate: t.sampleRate ?? 44100,
            seconds: t.seconds ?? 200,
            truePeakDb: t.analysis === "ok" ? t.truePeakDb : null,
            container: t.container,
            genres: mockGenres(t.albumKey),
            albumKey: t.albumKey ?? "",
            album: t.album,
          })),
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
    this.updateStages();
  }

  private repeat: RepeatMode = "off";
  async setRepeat(mode: RepeatMode) {
    this.repeat = mode;
  }

  private applyListening(l: ListeningSettings) {
    this.listening = l;
    this.updateStages();
  }

  // The stages the settings and the volume make run (made-up values for the mock).
  private mockStages() {
    const l = this.listening;
    const stages: { name: string; gainDb: number | "-inf" | null; detail?: string }[] = [];
    const processed = l?.mode === "processed";
    if (processed && l.replayGain !== "off") stages.push({ name: "replaygain", gainDb: -4.2 });
    if (processed && l.eqEnabled && l.eqBands.some((b) => b.enabled && (b.gainDb !== 0 || b.type.endsWith("Pass"))))
      stages.push({ name: "eq", gainDb: -3.0 });
    if (processed && (l.toneBassDb !== 0 || l.toneTrebleDb !== 0))
      stages.push({ name: "tone", gainDb: -Math.max(0, l.toneBassDb, l.toneTrebleDb) });
    if (processed && l.tubeEnabled)
      stages.push({
        name: "tube",
        gainDb: l.tubeDriveDb,
        detail:
          (this.mockTubeAuto()?.choice?.type ?? l.tubeType) +
          (l.tubeWarmUp ? `;warmup=${Math.floor(this.mockWarmUpSeconds())}` : "") + // task 9-8
          (l.tubeUnits ? `;unit=${l.tubeUnit}` : ""),
      });
    if (processed && l.spaceRoom && l.spaceRoom !== "off" && l.spaceMixPercent > 0) {
      const i = ["club", "hall", "arena"].indexOf(l.spaceRoom);
      const readable = mockIrReadable(l.spaceIr[i]);
      const source = playingSpaceSource(l, l.spaceRoom, { ...MOCK_IR_STATUS, path: l.spaceIr[i], loaded: readable });
      const tag = source === "synth" ? "" : `;${source}`;
      stages.push({ name: "space", gainDb: null, detail: `${l.spaceRoom};${Math.round(l.spaceMixPercent)}${tag}` });
    }
    if (processed && l.crossfeedEnabled) stages.push({ name: "crossfeed", gainDb: null, detail: l.crossfeedLevel });
    if (this.volumeDb !== 0) stages.push({ name: "volume", gainDb: this.volumeDb });
    const isp = l ? (processed ? l.ispSafeProcessed : l.ispSafeOriginal) : "off";
    if (isp !== "off") stages.push({ name: "isp-safe", gainDb: isp === "fixed" ? -3 : -1.2 });
    // The output rate (task 10-4): the mock's DAC takes every rate.
    const src = this.status.sourceFormat?.sampleRate;
    const to = processed && src ? mockOutputRate(l, src) : src;
    const converting = !!src && !!to && to !== src;
    if (stages.length > 0 || converting) stages.push({ name: "limiter", gainDb: null });
    if (converting) stages.push({ name: "resampler", gainDb: null, detail: `${src};${to};setting` });
    return stages;
  }

  // The current track's album and tube choice (task 9-7), as the app reports them.
  private mockTubeAuto() {
    const t = this.status.state === "empty" ? undefined : this.tracks[this.status.queueIndex];
    if (!t) return undefined;
    return {
      album: t.albumKey ? { key: t.albumKey, title: t.album ?? "" } : null,
      genres: t.genres ?? [],
      choice: this.tubeAuto ? chooseTubeType(this.tubeAuto, t.genres ?? [], t.albumKey ?? "") : null,
    };
  }

  private updateStages() {
    const stages = this.mockStages();
    const processing = stages.length > 0;
    const s = this.status;
    const l = this.listening;
    const mode = l?.mode ?? "original";
    this.update({
      ...this.withQueue(s),
      mode,
      ispSafe: l ? (mode === "processed" ? l.ispSafeProcessed : l.ispSafeOriginal) : "off",
      stages,
      tubeAuto: this.mockTubeAuto(),
      tubeWarmUpSeconds: this.mockWarmUpSeconds(),
      // The output at the converted rate (task 10-4).
      outputFormat: s.outputFormat && {
        ...s.outputFormat,
        sampleRate: Number(stages.find((x) => x.name === "resampler")?.detail?.split(";")[1] ?? s.sourceFormat?.sampleRate ?? s.outputFormat.sampleRate),
      },
      path: s.path && { ...s.path, processing, processingStages: stages.map((x) => x.name), dither: processing },
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
      trackTruePeakDb: t.truePeakDb ?? null,
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
    this.updateStages();
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
      // What follows (as Player::nextIndexLocked).
      const to =
        this.repeat === "one" ? from : this.repeat === "all" ? (from + 1) % this.tracks.length : from + 1;
      if (to >= this.tracks.length) {
        this.halt();
        this.update({
          ...this.status,
          state: "ended",
          positionFrames: this.status.totalFrames,
          meter: { ...this.status.meter, state: "idle" },
        });
        return;
      }
      const fields = this.trackFields(to);
      const gapless = fields.sourceFormat.sampleRate === this.status.sourceFormat?.sampleRate;
      this.update({
        ...this.status,
        ...fields,
        outputFormat: { ...fields.sourceFormat, containerBits: 32 },
        path: mockPath(this.tracks[to]),
        bitPerfect: mockVerdict(this.tracks[to]),
        // A new output session when the format changes; gapless keeps counting.
        delivery: gapless ? this.status.delivery : { matchedFrames: 0, mismatchedFrames: 0 },
        transitions: [
          ...this.status.transitions,
          {
            fromIndex: from,
            toIndex: to,
            kind: gapless ? "gapless" : "reopened",
            fromFormat: this.status.sourceFormat!,
            toFormat: fields.sourceFormat,
            switchMilliseconds: gapless ? 0 : 40,
          },
        ],
      });
      this.updateStages();
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
  // In a plain browser the reader is just another tab; its "app:" links have nowhere to go.
  async openArticle(id = "", section = "") {
    window.open(readerAddress(id, section, currentLang()), "ap-articles");
    return { ok: true };
  }
  onArticleCommand() {}

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

// The rate the mock sends in 音作りモード (its DAC takes every rate; task 10-4).
function mockOutputRate(l: ListeningSettings, src: number): number {
  switch (l.outputRateMode) {
    case "fixed":
      return l.outputRateHz;
    case "atleast":
      return Math.max(src, l.outputRateHz);
    case "multiple":
      return src * l.outputRateMultiple;
    default:
      return src;
  }
}
