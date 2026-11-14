import { describe, expect, it } from "vitest";
import { loadUiView, lookOf, otherViews, saveUiView, STANDARD_DESIGN, standardZoom, UI_VIEW_KEY, VIEW_LABELS, type StorageLike } from "./uiView";

class MemoryStorage implements StorageLike {
  values = new Map<string, string>();
  getItem(k: string) {
    return this.values.get(k) ?? null;
  }
  setItem(k: string, v: string) {
    this.values.set(k, v);
  }
}

class BrokenStorage implements StorageLike {
  getItem(): string | null {
    throw new Error("no storage");
  }
  setItem(): void {
    throw new Error("no storage");
  }
}

describe("which view the app starts in (CLAUDE.md chapter 8)", () => {
  it("the first start is the standard view", () => {
    expect(loadUiView(new MemoryStorage())).toBe("standard");
  });
  it("then the view last used", () => {
    const s = new MemoryStorage();
    saveUiView(s, "library");
    expect(s.values.get(UI_VIEW_KEY)).toBe("library");
    expect(loadUiView(s)).toBe("library");
    saveUiView(s, "standard");
    expect(loadUiView(s)).toBe("standard");
  });
  it("an unknown value or no storage: the standard view", () => {
    const s = new MemoryStorage();
    s.setItem(UI_VIEW_KEY, "tube");
    expect(loadUiView(s)).toBe("standard");
    expect(loadUiView(new BrokenStorage())).toBe("standard");
    expect(() => saveUiView(new BrokenStorage(), "library")).not.toThrow();
  });
  it("names and the switch", () => {
    expect(VIEW_LABELS.standard).toBe("標準ビュー");
    expect(VIEW_LABELS.library).toBe("ライブラリビュー");
    expect(VIEW_LABELS.amp).toBe("アンプビュー");
    expect(otherViews("standard")).toEqual(["amp", "library"]);
    expect(otherViews("amp")).toEqual(["standard", "library"]);
    expect(otherViews("library")).toEqual(["standard", "amp"]);
  });
  it("the amp view (task 9-9) is kept too, and shares the standard view's realistic look", () => {
    const s = new MemoryStorage();
    saveUiView(s, "amp");
    expect(loadUiView(s)).toBe("amp");
    expect(lookOf("amp")).toBe("real");
    expect(lookOf("standard")).toBe("real");
    expect(lookOf("library")).toBe("flat");
  });
});

describe("the standard view's scale (task 6-8)", () => {
  it("is 1 at the concept image's own size (1115 x 605 CSS px: 1672 x 908 read at 150%)", () => {
    expect(STANDARD_DESIGN).toEqual({ width: 1115, height: 605 });
    expect(standardZoom(1115, 605)).toBe(1);
  });
  it("grows with the window, so a maximised window looks like the concept image", () => {
    // 1920 x 1080 with the taskbar and Windows' title bar: the height decides.
    expect(standardZoom(1920, 1009)).toBeCloseTo(1009 / 605, 6);
    // A wide, short window: the height decides; a narrow, tall one: the width.
    expect(standardZoom(2400, 700)).toBeCloseTo(700 / 605, 6);
    expect(standardZoom(1280, 1200)).toBeCloseTo(1280 / 1115, 6);
  });
  it("stays within limits for very small or odd windows", () => {
    expect(standardZoom(600, 400)).toBe(0.75);
    expect(standardZoom(0, 0)).toBe(0.75);
    expect(standardZoom(Number.NaN, 800)).toBe(1);
  });
});
