import { describe, expect, it } from "vitest";
import {
  bandAt,
  dbAt,
  defaultValue,
  dragKnob,
  dragNode,
  formatParam,
  fromNorm,
  graphX,
  graphY,
  hzAt,
  nodeAt,
  nodePoint,
  parseKnobText,
  roundParam,
  toNorm,
  wheelKnob,
} from "./eqEdit";
import { autoPreampDb, biquad, isNeutral, responseDb, responsePoints } from "./eqResponse";
import type { EqBand } from "./settings";

const frame = { width: 900, height: 300, minHz: 20, maxHz: 20000, rangeDb: 12 };
const peak = (frequency: number, gainDb: number, q = 1): EqBand => ({ type: "peak", frequency, gainDb, q, enabled: true });

describe("knobs", () => {
  it("frequency and Q turn on a log scale, gain and preamp on a linear one", () => {
    expect(toNorm("frequency", 20)).toBe(0);
    expect(toNorm("frequency", Math.sqrt(20 * 20000))).toBeCloseTo(0.5);
    expect(toNorm("gainDb", 0)).toBe(0.5);
    expect(toNorm("q", 20)).toBe(1);
    expect(fromNorm("preamp", 0.5)).toBe(-15);
    expect(fromNorm("frequency", 2)).toBe(20000); // limited
  });
  it("200 px of drag is the whole range; up turns up; fine control is ten times slower", () => {
    expect(dragKnob("gainDb", 0, -100, false)).toBe(24);
    expect(dragKnob("gainDb", 0, 100, false)).toBe(-24);
    expect(dragKnob("gainDb", 0, -10, false)).toBe(2.4);
    expect(dragKnob("gainDb", 0, -10, true)).toBe(0.2);
    expect(dragKnob("frequency", 1000, -1000, false)).toBe(20000);
  });
  it("a wheel notch moves 1/100 of the range, and always at least one shown step", () => {
    expect(wheelKnob("gainDb", 0, -100, false)).toBe(0.5);
    expect(wheelKnob("gainDb", 0, 100, false)).toBe(-0.5);
    expect(wheelKnob("gainDb", 0, -100, true)).toBe(0.1); // 0.048 rounds to 0: one 0.1 dB step
    expect(wheelKnob("q", 1, -100, false)).toBeGreaterThan(1);
    expect(wheelKnob("gainDb", 0, -300, false)).toBe(1.5); // one event of three notches
    expect(wheelKnob("gainDb", 0, -4, false)).toBe(0.5); // a touchpad's small delta: one notch
  });
  it("values are rounded to the shown steps and formatted", () => {
    expect(roundParam("frequency", 31.54)).toBe(31.5);
    expect(roundParam("frequency", 1234.4)).toBe(1234);
    expect(roundParam("frequency", 12345)).toBe(12350);
    expect(roundParam("q", 0.7071)).toBe(0.71);
    expect(roundParam("gainDb", -0.04)).toBe(0);
    expect(formatParam("q", 0.7)).toBe("0.70");
    expect(formatParam("gainDb", -3)).toBe("−3.0 dB");
    expect(formatParam("frequency", 2500)).toBe("2.5 kHz");
  });
  it("double-click resets: 1 kHz, 0 dB, Q 1 (0.71 for shelves and pass filters)", () => {
    expect(defaultValue("frequency", null)).toBe(1000);
    expect(defaultValue("q", { ...peak(100, 0), type: "lowShelf" })).toBe(0.71);
    expect(defaultValue("q", peak(100, 0))).toBe(1);
    expect(defaultValue("preamp", null)).toBe(0);
  });
});

describe("typed values", () => {
  it("units are optional; k means kHz for the frequency", () => {
    expect(parseKnobText("frequency", "6.3k")).toBe(6300);
    expect(parseKnobText("frequency", "2.5 kHz")).toBe(2500);
    expect(parseKnobText("frequency", "120 Hz")).toBe(120);
    expect(parseKnobText("frequency", "5")).toBe(10); // the lowest typed frequency
    expect(parseKnobText("gainDb", "−3,5 dB")).toBe(-3.5);
    expect(parseKnobText("gainDb", "40")).toBe(24);
    expect(parseKnobText("q", "0.7071")).toBe(0.71);
    expect(parseKnobText("preamp", "+3")).toBe(0);
    expect(parseKnobText("preamp", "-6.26")).toBe(-6.3);
    expect(parseKnobText("q", "abc")).toBeNull();
    expect(parseKnobText("q", "")).toBeNull();
  });
});

describe("graph", () => {
  it("maps Hz and dB to pixels and back", () => {
    expect(graphX(20, frame)).toBe(0);
    expect(graphX(20000, frame)).toBeCloseTo(900);
    expect(hzAt(graphX(1234, frame), frame)).toBeCloseTo(1234);
    expect(graphY(0, frame)).toBe(150);
    expect(dbAt(graphY(-7.5, frame), frame)).toBeCloseTo(-7.5);
  });
  it("nodes sit at their gain; pass filters, band-pass and notch on the 0 dB line", () => {
    expect(nodePoint(peak(1000, 6), frame).y).toBe(75);
    expect(nodePoint({ ...peak(1000, 6), type: "notch" }, frame).y).toBe(150);
  });
  it("finds the node under the pointer, the nearest within the radius", () => {
    const bands = [peak(1000, 0), peak(1100, 0), peak(5000, 6)];
    const p = nodePoint(bands[1], frame);
    expect(nodeAt(bands, p.x + 2, p.y, frame)).toBe(1);
    expect(nodeAt(bands, p.x, p.y + 40, frame)).toBe(-1);
  });
  it("double-click on the graph adds a peak there", () => {
    const b = bandAt(graphX(2000, frame), graphY(4, frame), frame);
    expect(b).toEqual({ type: "peak", frequency: 2000, gainDb: 4, q: 1, enabled: true });
  });
  it("dragging a node: sideways the frequency, up and down the gain (the Q for types without one)", () => {
    const moved = dragNode(peak(1000, 0), graphX(2000, frame) - graphX(1000, frame), -25, frame, false);
    expect(moved.frequency).toBe(2000);
    expect(moved.gainDb).toBe(2);
    const hp = dragNode({ ...peak(100, 0), type: "highPass", q: 0.71 }, 0, -60, frame, false);
    expect(hp.q).toBe(1.42);
    expect(hp.gainDb).toBe(0);
    expect(dragNode(peak(1000, 0), 0, -25, frame, true).gainDb).toBe(0.2); // fine
    expect(dragNode(peak(1000, 0), 0, -1000, frame, false).gainDb).toBe(12); // within the view
  });
});

describe("response on the page (same formulas as the engine)", () => {
  const rate = 48000;
  it("a peak reaches its gain at Fc; a band-pass is 0 dB there and a notch nearly silent", () => {
    expect(responseDb([peak(1000, 6, 1.4)], 1000, rate)).toBeCloseTo(6, 9);
    expect(responseDb([{ ...peak(1000, 9, 2), type: "bandPass" }], 1000, rate)).toBeCloseTo(0, 9);
    expect(responseDb([{ ...peak(1000, 9, 2), type: "notch" }], 1000, rate)).toBeLessThan(-100);
  });
  it("the automatic preamp brings the largest boost to 0 dB; cuts need none", () => {
    expect(autoPreampDb([peak(1000, 6)], rate)).toBeCloseTo(-6, 6);
    expect(autoPreampDb([peak(1000, -6)], rate)).toBe(0);
  });
  it("neutral bands and bands that cannot be made", () => {
    expect(isNeutral(peak(1000, 0))).toBe(true);
    expect(isNeutral({ ...peak(1000, 0), type: "notch" })).toBe(false);
    expect(biquad(peak(24000, 3), rate)).toBeNull();
    const pts = responsePoints([peak(1000, 3)], 44100, 10);
    expect(pts[0][0]).toBe(20);
    expect(pts.at(-1)![0]).toBe(20000);
  });
});
