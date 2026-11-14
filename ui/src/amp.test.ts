import { describe, expect, it } from "vitest";
import {
  AMP_SIZE,
  AMP_TOP,
  ampState,
  badgeOf,
  BUTTONS,
  dragValue,
  KNOB_SPECS,
  knobAngle,
  KNOBS,
  LAMPS,
  modeChange,
  snap,
  stepValue,
  TOGGLE,
  WORDS,
} from "./amp";
import { defaultListening, type ListeningSettings } from "./settings";
// The cutting script, read as text: the positions must agree.
import py from "../../tools/ui_amp_parts.py?raw";
import { emptyStatus, type PlaybackStatus } from "./status";

const verdict = {
  bitPerfect: true,
  quantized: false,
  dither: false,
  comparison: false,
  repacked: false,
  dop: false,
  floatSource: false,
  dummyOutput: false,
  deviceVolume: null,
  deviceMuted: false,
  deliveryMismatch: false,
};

const fmt = { sampleRate: 96000, channels: 2, containerBits: 24, validBits: 24, sampleType: "int" as const };

const pathOf = (processing: boolean) => ({
  container: "FLAC",
  processing,
  processingStages: [],
  processingRequired: false,
  dither: processing,
  comparison: false,
  output: {
    api: "wasapiExclusive" as const,
    periodFrames: 960,
    periodMilliseconds: 10,
    eventDriven: true,
    layout: "interleaved" as const,
    asio: null,
    endpointVolume: null,
  },
});

// Playing on the direct path, bit-perfect.
const pure: PlaybackStatus = {
  ...emptyStatus,
  state: "playing",
  mode: "original",
  sourceFormat: fmt,
  outputFormat: fmt,
  path: pathOf(false),
  bitPerfect: verdict,
};

// 音作りモード with the tube (the type playing comes in the stage's detail).
const tube = (type: string): PlaybackStatus => ({
  ...pure,
  mode: "processed",
  path: pathOf(true),
  stages: [{ name: "tube", gainDb: -12, detail: type }],
  bitPerfect: { ...verdict, bitPerfect: false, dither: true },
});

const listening = (over: Partial<ListeningSettings> = {}): ListeningSettings => ({ ...defaultListening, ...over });

describe("the amp view's knobs (task 9-9)", () => {
  it("turn through 270 degrees, as the VOLUME knob's scale on the picture", () => {
    const d = KNOB_SPECS.drive;
    expect(knobAngle(d, d.min)).toBe(-135);
    expect(knobAngle(d, d.max)).toBe(135);
    expect(knobAngle(KNOB_SPECS.bass, 0)).toBe(0); // the tone controls' 0 dB straight up
    expect(knobAngle(KNOB_SPECS.volume, 1)).toBe(135); // 100%: all the way up
    expect(knobAngle(d, 99)).toBe(135); // out of range: at the end
  });
  it("a drag of 200 px goes through the whole range; Shift is ten times finer", () => {
    const b = KNOB_SPECS.bass;
    expect(dragValue(b, 0, -100, false)).toBe(10);
    expect(dragValue(b, 0, 100, false)).toBe(-10);
    expect(dragValue(b, 0, -10, false)).toBe(1);
    expect(dragValue(b, 0, -10, true)).toBe(0); // 0.1 dB: below one step of 0.5
    expect(dragValue(b, 0, -50, true)).toBe(0.5);
    expect(dragValue(KNOB_SPECS.drive, -12, -1000, false)).toBe(6); // stops at the end
  });
  it("the wheel and the arrow keys move a step (1 dB, the volume 1%)", () => {
    expect(stepValue(KNOB_SPECS.drive, -12, 1, false)).toBe(-11);
    expect(stepValue(KNOB_SPECS.drive, -12, -1, true)).toBe(-12.5);
    expect(stepValue(KNOB_SPECS.volume, 1, -1, false)).toBe(0.99);
    expect(stepValue(KNOB_SPECS.volume, 1, 1, false)).toBe(1);
    expect(snap(KNOB_SPECS.treble, 3.3)).toBe(3.5);
  });
});

describe("what the amp shows (task 9-9)", () => {
  it("PURE is 原音モード: tubes dark, the processing controls disabled", () => {
    const a = ampState(listening(), pure, false);
    expect(a.mode).toBe("pure");
    expect(a.tubesLit).toBe(false);
    expect(a.processingControls).toBe(false);
    expect(a.badge).toEqual({ text: "BIT-PERFECT", kind: "ok" });
  });
  it("the tubes light only when the tube is in the path", () => {
    const l = listening({ mode: "processed", tubeEnabled: true });
    expect(ampState(l, tube("se300b"), false).tubesLit).toBe(true);
    // 音作りモード with the tube turned off in the settings: the switch says TUBE, the tubes stay dark.
    const noTube: PlaybackStatus = { ...tube("se300b"), stages: [{ name: "volume", gainDb: -6 }] };
    const a = ampState(listening({ mode: "processed", tubeEnabled: false }), noTube, false);
    expect(a.mode).toBe("tube");
    expect(a.tubesLit).toBe(false);
    expect(a.badge).toEqual({ text: "DIGITAL VOL -6.0 dB", kind: "warn" });
  });
  it("TUBE TYPE shows the type playing (chosen by genre), and the look follows it", () => {
    const l = listening({ mode: "processed", tubeType: "se300b" });
    const a = ampState(l, tube("ppclassa"), true);
    expect(a.type).toBe("ppclassa");
    expect(a.look).toBe("pp");
    expect(a.auto).toBe(true);
    expect(ampState(l, tube("se300b"), false).look).toBe("se");
    // Nothing playing: the manual type.
    expect(ampState(listening({ tubeType: "ppel34" }), emptyStatus, false).type).toBe("ppel34");
  });
  it("the badge: BIT-PERFECT, TUBE PROCESSING, or what keeps it from being bit-perfect; nothing when nothing plays", () => {
    expect(badgeOf(pure).kind).toBe("ok");
    expect(badgeOf(tube("ppel34"))).toEqual({ text: "TUBE PROCESSING", kind: "tube" });
    expect(badgeOf(emptyStatus)).toEqual({ text: "", kind: "none" });
    expect(badgeOf(null).kind).toBe("none");
  });
  it("the switch: TUBE is 音作りモード with the tube on; PURE is 原音モード and keeps the tube setting", () => {
    expect(modeChange("tube")).toEqual({ mode: "processed", tubeEnabled: true });
    expect(modeChange("pure")).toEqual({ mode: "original" });
  });
  it("before the settings are read, the switch follows the status", () => {
    expect(ampState(null, tube("se300b"), false).mode).toBe("tube");
    expect(ampState(null, pure, false).mode).toBe("pure");
  });
});

describe("the amp view's layout (task 9-9)", () => {
  const inside = (x: number, y: number) =>
    x >= 0 && x <= AMP_SIZE.width && y - AMP_TOP >= 0 && y - AMP_TOP <= AMP_SIZE.height;
  it("everything sits on the picture", () => {
    for (const k of Object.values(KNOBS)) expect(inside(k.x, k.y)).toBe(true);
    for (const l of Object.values(LAMPS)) expect(inside(l.x, l.y)).toBe(true);
    for (const w of WORDS) expect(inside(w.x, w.y)).toBe(true);
    for (const b of BUTTONS) expect(inside(b.x, b.y)).toBe(true);
    expect(inside(TOGGLE.x + TOGGLE.width, TOGGLE.y + TOGGLE.height)).toBe(true);
  });
  it("every knob stands for a setting, or for the space stage still to come (9-6)", () => {
    expect(Object.keys(KNOBS).sort()).toEqual(["bass", "drive", "mix", "treble", "volume"]);
  });
  it("the lamps, knobs and buttons are where tools/ui_amp_parts.py found them on the picture", () => {
    const num = (s: string) => Number(s);
    for (const [name, l] of Object.entries(LAMPS)) {
      if (name === "crossfeed" || name === "auto") continue; // not on the picture
      const m = py.match(new RegExp(`'${name}': \\(([\\d.]+), ([\\d.]+)\\)`));
      expect(m, name).not.toBeNull();
      expect([num(m![1]), num(m![2])]).toEqual([l.x, l.y]);
    }
    const vol = py.match(/^VOLUME = \(([\d.]+), ([\d.]+)\)/m)!;
    expect([num(vol[1]), num(vol[2])]).toEqual([KNOBS.volume.x, KNOBS.volume.y]);
    const buttons = py.match(/^BUTTONS = \[(.*)\]/m)![1];
    expect([...buttons.matchAll(/\((\d+), (\d+)\)/g)].map((m) => [num(m[1]), num(m[2])])).toEqual(BUTTONS.map((b) => [b.x, b.y]));
    const toggle = py.match(/^TOGGLE = \((\d+), (\d+), (\d+), (\d+)\)/m)!;
    expect([num(toggle[1]), num(toggle[2]), num(toggle[3]) - num(toggle[1]), num(toggle[4]) - num(toggle[2])]).toEqual([
      TOGGLE.x,
      TOGGLE.y,
      TOGGLE.width,
      TOGGLE.height,
    ]);
    expect(py).toMatch(new RegExp(`^TOP = ${AMP_TOP}\\b`, "m"));
  });
});

describe("shuffle and repeat on the SE amp (task 9-9)", () => {
  it("sit on the wood, clear of the badge, the transport and the volume", async () => {
    const { MODE_BUTTONS, VOLUME_READOUT, BADGE } = await import("./amp");
    for (const b of Object.values(MODE_BUTTONS)) {
      expect(b.y - b.r - AMP_TOP).toBeGreaterThan(486); // under the panel (528 - 42)
      for (const t of BUTTONS) expect(Math.abs(b.x - t.x)).toBeGreaterThan(b.r + t.r);
    }
    expect(MODE_BUTTONS.shuffle.x - MODE_BUTTONS.shuffle.r).toBeGreaterThan(BADGE.x + BADGE.width / 2 + 20);
    expect(VOLUME_READOUT.x).toBeGreaterThan(MODE_BUTTONS.repeat.x + MODE_BUTTONS.repeat.r);
  });
});
