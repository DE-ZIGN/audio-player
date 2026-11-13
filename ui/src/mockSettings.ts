// The settings for the mock backend (a plain browser, no app). The app does
// this in the engine (app/src/SettingsBridge.cpp); here the page's own copy of
// the EQ formulas (eqResponse.ts) stands in, so the page can be worked on.

import type { EqPreset } from "./eqPresets";
import { autoPreampDb, responsePoints, skippedBands } from "./eqResponse";
import {
  defaultListening,
  defaultOutput,
  type EqBand,
  type EqCurve,
  type EqImport,
  type ListeningSettings,
  type OutputChoice,
  type OutputsInfo,
  type PresetsResult,
  type SetResult,
  type SettingsInfo,
} from "./settings";

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

  constructor(
    private readonly onListening: (l: ListeningSettings) => void = () => {},
    private readonly onSolo: (band: number | null) => void = () => {},
  ) {}

  async get(): Promise<SettingsInfo> {
    return structuredClone({ output: this.output, listening: this.listening, eqPresets: this.eqPresets });
  }
  async setListening(l: ListeningSettings): Promise<SetResult> {
    this.listening = structuredClone(l);
    this.onListening(this.listening);
    const c = mockEqCurve(l.eqBands);
    return { ok: true, eq: { sampleRate: c.sampleRate, preampDb: c.preampDb, skipped: c.skipped } };
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
