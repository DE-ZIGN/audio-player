import { describe, expect, it } from "vitest";
import { MOCKUP_TITLE_BAR, mockupClip, parseMockupParams, safeMockupName, writeMockupParams } from "./mockupParams";

describe("safeMockupName", () => {
  it("accepts a plain image file name", () => {
    expect(safeMockupName("UI-STANDARD-v1.png")).toBe("UI-STANDARD-v1.png");
    expect(safeMockupName("ui_list.v2.jpg")).toBe("ui_list.v2.jpg");
  });
  it("adds .png when there is no extension", () => {
    expect(safeMockupName("UI-STANDARD-LIST-v1")).toBe("UI-STANDARD-LIST-v1.png");
  });
  it("refuses paths and other file types", () => {
    for (const bad of ["../secret.png", "a/b.png", "a\\b.png", "..", "x.txt", "", "%2e%2e.png", "a b.png"]) {
      expect(safeMockupName(bad)).toBeNull();
    }
  });
});

describe("parseMockupParams", () => {
  it("is off without the mockup parameter", () => {
    expect(parseMockupParams("").name).toBeNull();
    expect(parseMockupParams("?selftest").name).toBeNull();
  });
  it("has defaults that line the mockup's client area up with the page", () => {
    const p = parseMockupParams("?mockup=UI-STANDARD-v1");
    expect(p).toEqual({ name: "UI-STANDARD-v1.png", mode: "overlay", opacity: 0.5, dx: 0, dy: -MOCKUP_TITLE_BAR, wipe: 0.5, zoom: 1 });
  });
  it("reads and clamps the settings", () => {
    const p = parseMockupParams("?mockup=a.png&mode=wipe&opacity=3&dx=-4&dy=10&wipe=-1");
    expect(p).toEqual({ name: "a.png", mode: "wipe", opacity: 1, dx: -4, dy: 10, wipe: 0, zoom: 1 });
    expect(parseMockupParams("?mockup=a&mode=nonsense&opacity=x").mode).toBe("overlay");
    expect(parseMockupParams("?mockup=a&opacity=x").opacity).toBe(0.5);
  });
  it("takes the image as drawn for a scaled screen: 150% shows it at 2/3 size", () => {
    const p = parseMockupParams("?mockup=a&zoom=1.5");
    expect(p.zoom).toBe(1.5);
    expect(p.dy).toBe(-22); // the title bar shrinks with it
    expect(parseMockupParams("?mockup=a&zoom=0").zoom).toBe(1);
    expect(parseMockupParams("?mockup=a&zoom=9").zoom).toBe(4);
  });
  it("round-trips through the query, keeping other parameters", () => {
    const p = { name: "b.png", mode: "diff" as const, opacity: 0.25, dx: 2, dy: -30, wipe: 0.75, zoom: 1.5 };
    const search = writeMockupParams("?selftest=1&mockup=old", p);
    expect(new URLSearchParams(search).get("selftest")).toBe("1");
    expect(parseMockupParams(search)).toEqual(p);
  });
});

describe("mockupClip", () => {
  it("clips the mockup to the left of the line only when wiping", () => {
    expect(mockupClip("wipe", 0.3)).toBe("inset(0 70% 0 0)");
    expect(mockupClip("overlay", 0.3)).toBe("none");
    expect(mockupClip("diff", 0.3)).toBe("none");
  });
});
