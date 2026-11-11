import { describe, expect, it } from "vitest";
import { afterRemove, afterMove, afterInsert, dbToSlider, sliderToDb, volumeLabel } from "./queueOps";

describe("Volume slider (task 6-2)", () => {
  it("is 0 dB at the top, muted at the bottom, about -12 dB halfway", () => {
    expect(sliderToDb(1)).toBe(0);
    expect(sliderToDb(0)).toBe("-inf");
    expect(sliderToDb(0.5)).toBeCloseTo(-12.0, 1);
    expect(dbToSlider(0)).toBe(1);
    expect(dbToSlider("-inf")).toBe(0);
    expect(dbToSlider(sliderToDb(0.3) as number)).toBeCloseTo(0.3, 2); // dB is rounded to 0.1
  });
  it("says 100% only when nothing is changed (bit-perfect possible)", () => {
    expect(volumeLabel(0)).toBe("100%");
    expect(volumeLabel(-6)).toBe("-6.0 dB");
    expect(volumeLabel(-0.04)).toBe("-0.0 dB"); // not 100%: the processing path
    expect(volumeLabel("-inf")).toBe("ミュート");
  });
});

describe("Editing what comes next (task 6-2)", () => {
  // Queue ids; the current track is at index 1.
  const ids = [10, 11, 12, 13, 14];
  it("inserts after the current track or at the end", () => {
    expect(afterInsert(ids, 1, [20, 21], "next")).toEqual([20, 21, 12, 13, 14]);
    expect(afterInsert(ids, 1, [20], "end")).toEqual([12, 13, 14, 20]);
    expect(afterInsert([], 0, [20], "end")).toEqual([20]);
  });
  it("removes an upcoming track; never the current or past ones", () => {
    expect(afterRemove(ids, 1, 3)).toEqual([12, 14]);
    expect(afterRemove(ids, 1, 1)).toBeNull();
    expect(afterRemove(ids, 1, 0)).toBeNull();
  });
  it("moves an upcoming track among the upcoming ones", () => {
    expect(afterMove(ids, 1, 4, 2)).toEqual([14, 12, 13]); // last to just after the current
    expect(afterMove(ids, 1, 2, 4)).toEqual([13, 14, 12]);
    expect(afterMove(ids, 1, 0, 3)).toBeNull(); // a past track
    expect(afterMove(ids, 1, 3, 1)).toBeNull(); // onto the current track
  });
});
