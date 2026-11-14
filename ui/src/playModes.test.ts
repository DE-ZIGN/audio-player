import { describe, expect, it } from "vitest";
import {
  loadPlayModes,
  nextRepeat,
  nextTrackIndex,
  originalAfterInsert,
  prevTrackIndex,
  repeatLabel,
  restoreOrder,
  savePlayModes,
  shuffleStart,
  shuffled,
  shuffleLabel,
} from "./playModes";

// A fixed "random" sequence, so the order is known.
const seq = (...xs: number[]) => {
  let i = 0;
  return () => xs[i++ % xs.length];
};

const memory = () => {
  const m = new Map<string, string>();
  return { getItem: (k: string) => m.get(k) ?? null, setItem: (k: string, v: string) => void m.set(k, v) };
};

describe("repeat (task 9-9)", () => {
  it("the button goes off → all → one → off", () => {
    expect(nextRepeat("off")).toBe("all");
    expect(nextRepeat("all")).toBe("one");
    expect(nextRepeat("one")).toBe("off");
    expect(repeatLabel("off")).toBe("リピート：オフ");
    expect(repeatLabel("all")).toBe("リピート：全曲");
    expect(repeatLabel("one")).toBe("リピート：1 曲");
  });
  it("next and previous go round when repeat is on", () => {
    expect(nextTrackIndex(1, 3, "off")).toBe(2);
    expect(nextTrackIndex(2, 3, "off")).toBeNull();
    expect(nextTrackIndex(2, 3, "all")).toBe(0);
    expect(nextTrackIndex(2, 3, "one")).toBe(0); // the buttons still move on
    expect(nextTrackIndex(0, 0, "all")).toBeNull();
    expect(prevTrackIndex(1, 3, "off")).toBe(0);
    expect(prevTrackIndex(0, 3, "off")).toBeNull();
    expect(prevTrackIndex(0, 3, "all")).toBe(2);
  });
});

describe("shuffle (task 9-9)", () => {
  it("mixes a list (Fisher–Yates), keeping every entry", () => {
    const xs = [1, 2, 3, 4, 5];
    const out = shuffled(xs, seq(0.99, 0.1, 0.6, 0.3));
    expect([...out].sort()).toEqual(xs);
    expect(out).not.toEqual(xs);
    expect(xs).toEqual([1, 2, 3, 4, 5]); // not changed in place
  });
  it("playing with shuffle on: the chosen track first, the rest mixed", () => {
    const out = shuffleStart([10, 20, 30, 40], 2, seq(0, 0, 0));
    expect(out[0]).toBe(30);
    expect([...out.slice(1)].sort()).toEqual([10, 20, 40]);
  });
  it("shuffle off: what is left goes back to the order it had", () => {
    const original = [1, 2, 3, 4, 5, 6];
    // Playing 3, the rest was mixed to 6, 1, 5, 2, 4; 6 has played since.
    expect(restoreOrder([1, 5, 2, 4], original)).toEqual([1, 2, 4, 5]);
    // A track added while mixed and not in the order: at the end.
    expect(restoreOrder([9, 5, 2], original)).toEqual([2, 5, 9]);
    // The same track twice.
    expect(restoreOrder([2, 1, 2], [2, 1, 2])).toEqual([2, 1, 2]);
  });
  it("tracks added while mixed take their place in the order to come back to", () => {
    const original = [1, 2, 3, 4];
    // Queue now 3, 1, 4, 2 with 1 playing (index 1).
    expect(originalAfterInsert(original, [3, 1, 4, 2], 1, [8, 9], "next")).toEqual([1, 8, 9, 2, 3, 4]);
    expect(originalAfterInsert(original, [3, 1, 4, 2], 1, [8], "end")).toEqual([1, 2, 3, 4, 8]);
    // The current track twice in the order: its occurrence by count.
    expect(originalAfterInsert([5, 6, 5], [5, 5, 6], 1, [7], "next")).toEqual([5, 6, 5, 7]);
  });
  it("labels say what it does", () => {
    expect(shuffleLabel(false)).toBe("シャッフル：オフ");
    expect(shuffleLabel(true)).toBe("シャッフル：オン");
  });
});

describe("the modes are kept (task 9-9)", () => {
  it("off, off at first; then as they were left", () => {
    const s = memory();
    expect(loadPlayModes(s)).toEqual({ shuffle: false, repeat: "off" });
    savePlayModes(s, { shuffle: true, repeat: "one" });
    expect(loadPlayModes(s)).toEqual({ shuffle: true, repeat: "one" });
  });
  it("unreadable values and no storage give the defaults", () => {
    const s = memory();
    s.setItem("repeat", "sometimes");
    expect(loadPlayModes(s).repeat).toBe("off");
    expect(loadPlayModes(null)).toEqual({ shuffle: false, repeat: "off" });
  });
});
