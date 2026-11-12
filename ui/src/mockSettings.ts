// The settings for the mock backend (a plain browser, no app). The app works
// these out in the engine (app/src/SettingsBridge.cpp); here a copy of the
// same biquad formulas draws the EQ curve, so the page can be worked on.

import {
  defaultListening,
  defaultOutput,
  type EqBand,
  type EqCurve,
  type EqImport,
  type ListeningSettings,
  type OutputChoice,
  type OutputsInfo,
  type SetResult,
  type SettingsInfo,
} from "./settings";

function biquad(b: EqBand, rate: number): number[] | null {
  if (!(b.frequency > 0) || b.frequency >= 0.49 * rate || !(b.q > 0)) return null;
  const w0 = (2 * Math.PI * b.frequency) / rate;
  const cs = Math.cos(w0);
  const alpha = Math.sin(w0) / (2 * b.q);
  const a = Math.pow(10, b.gainDb / 40);
  const sa = 2 * Math.sqrt(a) * alpha;
  let c: number[];
  switch (b.type) {
    case "peak":
      c = [1 + alpha * a, -2 * cs, 1 - alpha * a, 1 + alpha / a, -2 * cs, 1 - alpha / a];
      break;
    case "lowShelf":
      c = [
        a * (a + 1 - (a - 1) * cs + sa),
        2 * a * (a - 1 - (a + 1) * cs),
        a * (a + 1 - (a - 1) * cs - sa),
        a + 1 + (a - 1) * cs + sa,
        -2 * (a - 1 + (a + 1) * cs),
        a + 1 + (a - 1) * cs - sa,
      ];
      break;
    case "highShelf":
      c = [
        a * (a + 1 + (a - 1) * cs + sa),
        -2 * a * (a - 1 + (a + 1) * cs),
        a * (a + 1 + (a - 1) * cs - sa),
        a + 1 - (a - 1) * cs + sa,
        2 * (a - 1 - (a + 1) * cs),
        a + 1 - (a - 1) * cs - sa,
      ];
      break;
    case "lowPass":
      c = [(1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha];
      break;
    case "highPass":
      c = [(1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha];
      break;
  }
  return c.map((x) => x / c[3]);
}

function responseDb(bands: EqBand[], hz: number, rate: number): number {
  const w = (2 * Math.PI * hz) / rate;
  let db = 0;
  for (const b of bands) {
    if (!b.enabled) continue;
    const c = biquad(b, rate);
    if (!c) continue;
    const [b0, b1, b2, , a1, a2] = c;
    // |H(e^jw)|^2 for a normalized biquad.
    const re = (x0: number, x1: number, x2: number) => x0 + x1 * Math.cos(w) + x2 * Math.cos(2 * w);
    const im = (x1: number, x2: number) => -(x1 * Math.sin(w) + x2 * Math.sin(2 * w));
    const num = re(b0, b1, b2) ** 2 + im(b1, b2) ** 2;
    const den = re(1, a1, a2) ** 2 + im(a1, a2) ** 2;
    db += 10 * Math.log10(num / den);
  }
  return db;
}

export function mockEqCurve(bands: EqBand[], rate = 48000): EqCurve {
  const top = Math.min(20000, 0.49 * rate);
  const points: [number, number][] = [];
  for (let i = 0; i < 160; i++) {
    const hz = 20 * Math.pow(top / 20, i / 159);
    points.push([hz, responseDb(bands, hz, rate)]);
  }
  const peak = Math.max(0, ...points.map(([, db]) => db), ...bands.map((b) => responseDb(bands, b.frequency, rate)));
  const skipped = bands.filter((b) => b.enabled && biquad(b, rate) === null).length;
  return { sampleRate: rate, preampDb: peak > 0 ? -peak : 0, skipped, points };
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

  constructor(private readonly onListening: (l: ListeningSettings) => void = () => {}) {}

  async get(): Promise<SettingsInfo> {
    return structuredClone({ output: this.output, listening: this.listening });
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
  onOutputsChanged(listener: () => void) {
    this.outputsListeners.push(listener);
  }
}
