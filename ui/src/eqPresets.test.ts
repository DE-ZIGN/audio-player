import { describe, expect, it } from "vitest";
import {
  applyPreset,
  FLAT_PRESET,
  matchingPreset,
  MAX_PRESET_NAME,
  MAX_PRESETS,
  presetFrom,
  presetName,
  withDeleted,
  withRenamed,
  withSaved,
  type EqPreset,
} from "./eqPresets";
import { defaultListening, type EqBand, type ListeningSettings } from "./settings";

const band = (frequency: number, gainDb: number): EqBand => ({ type: "peak", frequency, gainDb, q: 1, enabled: true });
const listening = (part: Partial<ListeningSettings> = {}): ListeningSettings => ({ ...defaultListening, ...part });
const preset = (name: string, bands: EqBand[] = [band(1000, 3)]): EqPreset => ({
  name,
  bands,
  autoPreamp: true,
  manualPreampDb: 0,
});

describe("EQ presets (task 6-9)", () => {
  it("takes the curve from the settings and puts it back, leaving the rest", () => {
    const l = listening({ mode: "processed", eqEnabled: false, eqBands: [band(100, 4)], eqAutoPreamp: false, eqManualPreampDb: -2 });
    const p = presetFrom(l, "低音");
    expect(p).toEqual({ name: "低音", bands: [band(100, 4)], autoPreamp: false, manualPreampDb: -2 });
    const back = applyPreset(listening(), p);
    expect(back.eqBands).toEqual([band(100, 4)]);
    expect(back.eqAutoPreamp).toBe(false);
    expect(back.eqManualPreampDb).toBe(-2);
    expect(back.mode).toBe("original");
    expect(back.eqEnabled).toBe(true);
    // A copy: changing the settings does not change the preset.
    back.eqBands[0].gainDb = 9;
    expect(p.bands[0].gainDb).toBe(4);
  });

  it("names: trimmed, one line, not empty, at most 60 characters", () => {
    expect(presetName("  HD 600  ")).toBe("HD 600");
    expect(presetName("a\nb\tc")).toBe("a b c");
    expect(presetName("   ")).toBeNull();
    expect(presetName("あ".repeat(80))).toBe("あ".repeat(MAX_PRESET_NAME));
  });

  it("tells which preset the current EQ is (the curve and the preamp)", () => {
    const list = [preset("A", [band(1000, 3)]), preset("B", [band(2000, -2)])];
    expect(matchingPreset(listening({ eqBands: [band(2000, -2)] }), list)).toBe("B");
    expect(matchingPreset(listening({ eqBands: [band(2000, -1.9)] }), list)).toBeNull();
    // The manual preamp counts only when it is used.
    const manual = { ...preset("M", [band(500, 2)]), autoPreamp: false, manualPreampDb: -3 };
    expect(matchingPreset(listening({ eqBands: [band(500, 2)], eqAutoPreamp: false, eqManualPreampDb: -3 }), [manual])).toBe("M");
    expect(matchingPreset(listening({ eqBands: [band(500, 2)], eqAutoPreamp: false, eqManualPreampDb: -2 }), [manual])).toBeNull();
    expect(matchingPreset(listening({ eqBands: [band(1000, 3)], eqManualPreampDb: -7 }), list)).toBe("A");
  });

  it("when several presets are the current EQ, the one last chosen or saved is shown", () => {
    const list = [preset("A", [band(1000, 3)]), preset("B", [band(1000, 3)])];
    const l = listening({ eqBands: [band(1000, 3)] });
    expect(matchingPreset(l, list)).toBe("A");
    expect(matchingPreset(l, list, "B")).toBe("B");
    expect(matchingPreset(l, list, "gone")).toBe("A");
    expect(matchingPreset(listening(), list, "B")).toBeNull(); // B is not the current EQ
  });

  it("saving under a name in use replaces that preset in its place; a new name goes last", () => {
    const list = [preset("A"), preset("B")];
    const replaced = withSaved(list, preset("A", [band(50, 1)]));
    expect(replaced.ok && replaced.list.map((p) => p.name)).toEqual(["A", "B"]);
    expect(replaced.ok && replaced.list[0].bands).toEqual([band(50, 1)]);
    const added = withSaved(list, preset("C"));
    expect(added.ok && added.list.map((p) => p.name)).toEqual(["A", "B", "C"]);
    expect(list.map((p) => p.name)).toEqual(["A", "B"]); // not changed in place
  });

  it("saving: a name is needed, and at most 100 presets", () => {
    expect(withSaved([], preset("  ")).ok).toBe(false);
    const full = Array.from({ length: MAX_PRESETS }, (_, i) => preset(`P${i}`));
    const r = withSaved(full, preset("one more"));
    expect(r.ok).toBe(false);
    expect(!r.ok && r.message).toContain("100");
    expect(withSaved(full, preset("P5")).ok).toBe(true); // replacing is fine
  });

  it("renaming keeps the place; a name in use by another is refused", () => {
    const list = [preset("A"), preset("B")];
    const r = withRenamed(list, "A", " Z ");
    expect(r.ok && r.list.map((p) => p.name)).toEqual(["Z", "B"]);
    expect(withRenamed(list, "A", "B").ok).toBe(false);
    expect(withRenamed(list, "A", "A").ok).toBe(true);
    expect(withRenamed(list, "nope", "C").ok).toBe(false);
    expect(withRenamed(list, "A", "").ok).toBe(false);
  });

  it("deleting", () => {
    expect(withDeleted([preset("A"), preset("B")], "A").map((p) => p.name)).toEqual(["B"]);
    expect(withDeleted([preset("A")], "nope").map((p) => p.name)).toEqual(["A"]);
  });

  it("the flat choice for ABX: no bands", () => {
    expect(FLAT_PRESET.bands).toEqual([]);
    expect(FLAT_PRESET.autoPreamp).toBe(true);
  });
});
