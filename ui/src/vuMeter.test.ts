import { describe, expect, it } from "vitest";
import { needleAngle, needleFraction, VU_MARKS, VU_REFERENCE_DBFS, vuOf, VU_SWEEP } from "./vuMeter";

describe("VU scale (task 6-8)", () => {
  it("0 VU is -18 dBFS (a sine's RMS)", () => {
    expect(VU_REFERENCE_DBFS).toBe(-18);
    expect(vuOf(-18)).toBe(0);
    expect(vuOf(-21)).toBe(-3);
    expect(vuOf("-inf")).toBe(-Infinity);
  });
  it("positions follow the voltage, as on a VU meter: 0 VU at about 71% of the scale", () => {
    expect(needleFraction(-Infinity)).toBe(0);
    expect(needleFraction(0)).toBeCloseTo(1 / Math.pow(10, 3 / 20), 6);
    expect(needleFraction(3)).toBe(1);
    expect(needleFraction(10)).toBe(1); // pinned
    expect(needleFraction(-20)).toBeCloseTo(0.1 / Math.pow(10, 3 / 20), 6);
  });
  it("the needle sweeps symmetrically around the top", () => {
    expect(needleAngle(-Infinity)).toBe(-VU_SWEEP / 2);
    expect(needleAngle(3)).toBe(VU_SWEEP / 2);
    expect(needleAngle(0)).toBeGreaterThan(0);
  });
  it("the marks run from -20 to +3, in order", () => {
    expect(VU_MARKS[0]).toBe(-20);
    expect(VU_MARKS.at(-1)).toBe(3);
    for (let i = 1; i < VU_MARKS.length; i++) expect(needleAngle(VU_MARKS[i])).toBeGreaterThan(needleAngle(VU_MARKS[i - 1]));
  });
});
