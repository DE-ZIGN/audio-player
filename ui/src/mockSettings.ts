// The settings for the mock backend (a plain browser, no app). The app does
// this in the engine (app/src/SettingsBridge.cpp); here the page's own copy of
// the EQ formulas (eqResponse.ts) stands in, so the page can be worked on.

import type { EqPreset } from "./eqPresets";
import type { TubeAutoSettings, TubeGenreRule } from "./tubeAuto";
import { autoPreampDb, responsePoints, skippedBands } from "./eqResponse";
import {
  defaultListening,
  defaultOutput,
  type EqBand,
  type EqCurve,
  type EqImport,
  type IrChoice,
  type ListeningSettings,
  type OutputChoice,
  type OutputsInfo,
  type PresetsResult,
  type SetResult,
  type SettingsInfo,
  type SpaceIrStatus,
  type TubeAutoResult,
} from "./settings";

export const MOCK_IR_STATUS: SpaceIrStatus = { path: "", loaded: false, seconds: 0, sampleRate: 0, channels: 0, fileChannels: 0, truncated: false };

// Whether the mock "reads" a user IR file (task 9-6): not when the path says "bad" or "no such".
export function mockIrReadable(path: string): boolean {
  return path !== "" && !path.includes("bad") && !path.includes("no such");
}

// A part of the engine's default table (engine/src/dsp/tube_auto.cpp), for the mock.
const MOCK_TUBE_RULES: TubeGenreRule[] = [
  { genre: "Jazz", type: "se300b" },
  { genre: "Vocal", type: "se300b" },
  { genre: "ジャズ", type: "se300b" },
  { genre: "Rock", type: "ppel34" },
  { genre: "Pop", type: "ppel34" },
  { genre: "ロック", type: "ppel34" },
  { genre: "Classical", type: "ppclassa" },
  { genre: "クラシック", type: "ppclassa" },
];

export function mockEqCurve(bands: EqBand[], rate = 48000): EqCurve {
  return {
    sampleRate: rate,
    preampDb: autoPreampDb(bands, rate),
    skipped: skippedBands(bands, rate),
    points: responsePoints(bands, rate),
  };
}

const MOCK_OUTPUTS: OutputsInfo = {
  wasapi: [
    { id: "{mock-speakers}", name: "スピーカー（模擬）", isDefault: true },
    { id: "{mock-dac}", name: "USB DAC（模擬）" },
  ],
  asio: [
    { id: "{MOCK-ASIO}", name: "模擬 ASIO ドライバー" },
    { id: "{MOCK-CRASH}", name: "読み込むと落ちる ASIO ドライバー（模擬）" },
  ],
};

// An AutoEQ-like preset for the import button.
const MOCK_PRESET: EqBand[] = [
  { type: "lowShelf", frequency: 105, gainDb: 5.5, q: 0.7, enabled: true },
  { type: "peak", frequency: 180, gainDb: -2.6, q: 0.9, enabled: true },
  { type: "peak", frequency: 2900, gainDb: 3.1, q: 2.1, enabled: true },
  { type: "peak", frequency: 5800, gainDb: -4.4, q: 3.5, enabled: true },
  { type: "highShelf", frequency: 10000, gainDb: -2, q: 0.7, enabled: true },
];

export class MockSettings {
  private output: OutputChoice = { ...defaultOutput };
  private listening: ListeningSettings = { ...defaultListening, eqBands: [] };
  private outputsListeners: (() => void)[] = [];
  private eqPresets: EqPreset[] = [
    { name: "テスト用（低音 +5.5）", bands: structuredClone(MOCK_PRESET), autoPreamp: true, manualPreampDb: 0 },
  ];

  private tubeAuto: TubeAutoSettings = { enabled: false, rules: structuredClone(MOCK_TUBE_RULES), albums: [] };

  constructor(
    private readonly onListening: (l: ListeningSettings) => void = () => {},
    private readonly onSolo: (band: number | null) => void = () => {},
    private readonly onTubeAuto: (t: TubeAutoSettings) => void = () => {},
  ) {}

  async get(): Promise<SettingsInfo> {
    return structuredClone({
      output: this.output,
      listening: this.listening,
      eqPresets: this.eqPresets,
      tubeAuto: this.tubeAuto,
      tubeAutoDefaultRules: MOCK_TUBE_RULES,
      spaceIrs: this.spaceIrs(),
    });
  }
  // The user IR files (task 9-6), as the app would report them: a path with
  // "bad" in it cannot be read.
  private spaceIrs(): SpaceIrStatus[] {
    return (this.listening.spaceIr ?? ["", "", ""]).map((path) =>
      path === ""
        ? { path, loaded: false, seconds: 0, sampleRate: 0, channels: 0, fileChannels: 0, truncated: false }
        : !mockIrReadable(path)
          ? { path, loaded: false, error: "このファイルは IR として読めません（模擬）", seconds: 0, sampleRate: 0, channels: 0, fileChannels: 0, truncated: false }
          : { path, loaded: true, seconds: 2.84, sampleRate: 44100, channels: 2, fileChannels: 2, truncated: false },
    );
  }
  private irCount = 0;
  async chooseIr(): Promise<IrChoice> {
    this.irCount += 1;
    return { ok: true, path: `C:\\Users\\me\\IR\\hall-${this.irCount}.wav` };
  }
  async setTubeAuto(t: TubeAutoSettings): Promise<TubeAutoResult> {
    this.tubeAuto = structuredClone(t);
    this.onTubeAuto(structuredClone(t));
    return { ok: true, tubeAuto: structuredClone(t) };
  }
  async setListening(l: ListeningSettings): Promise<SetResult> {
    this.listening = structuredClone(l);
    this.onListening(this.listening);
    const c = mockEqCurve(l.eqBands);
    return { ok: true, eq: { sampleRate: c.sampleRate, preampDb: c.preampDb, skipped: c.skipped }, spaceIrs: this.spaceIrs() };
  }
  async outputs(): Promise<OutputsInfo> {
    return structuredClone(MOCK_OUTPUTS);
  }
  async setOutput(o: OutputChoice): Promise<SetResult> {
    if (o.kind === "asio") await new Promise((r) => setTimeout(r, 600)); // the check in a child process
    if (o.id === "{MOCK-CRASH}")
      return {
        ok: false,
        message:
          "この ASIO ドライバーは使えません。読み込むと異常終了します（確かめるための別のプロセスで起きたので、このアプリは無事です）",
      };
    this.output = { ...o };
    return { ok: true };
  }
  async importEq(): Promise<EqImport> {
    return {
      ok: true,
      bands: structuredClone(MOCK_PRESET),
      warnings: ["Filter 6: OFF のフィルターは読み飛ばしました（模擬）"],
      filePreampDb: -6.2,
    };
  }
  async eqCurve(bands: EqBand[]): Promise<EqCurve> {
    return mockEqCurve(bands);
  }
  async eqSolo(band: number | null): Promise<void> {
    this.onSolo(band);
  }
  async setEqPresets(list: EqPreset[]): Promise<PresetsResult> {
    this.eqPresets = structuredClone(list);
    return { ok: true, eqPresets: structuredClone(this.eqPresets) };
  }
  onOutputsChanged(listener: () => void) {
    this.outputsListeners.push(listener);
  }
}
