import { describe, expect, it } from "vitest";
import { loadUiView, otherView, saveUiView, UI_VIEW_KEY, VIEW_LABELS, type StorageLike } from "./uiView";

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
    s.setItem(UI_VIEW_KEY, "amp");
    expect(loadUiView(s)).toBe("standard");
    expect(loadUiView(new BrokenStorage())).toBe("standard");
    expect(() => saveUiView(new BrokenStorage(), "library")).not.toThrow();
  });
  it("names and the switch", () => {
    expect(VIEW_LABELS.standard).toBe("標準ビュー");
    expect(VIEW_LABELS.library).toBe("ライブラリビュー");
    expect(otherView("standard")).toBe("library");
    expect(otherView("library")).toBe("standard");
  });
});
