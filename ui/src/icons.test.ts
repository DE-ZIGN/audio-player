import { describe, expect, it } from "vitest";
import { ICON_BOX, ICONS, mirrored, playIcon, repeatIcon, type IconName } from "./icons";
import html from "../index.html?raw";
import css from "./style.css?raw";

// Task 9-9 (2026-11-18): the transport's symbols are drawn (SVG) in the
// shapes of the concept image, instead of characters from a font.

const numbers = (d: string) => (d.match(/-?\d+(\.\d+)?/g) ?? []).map(Number);
const xs = (name: IconName) =>
  ICONS[name].flatMap((s) => {
    // x of every M, L and H (the paths here use absolute commands only)
    const out: number[] = [];
    for (const m of s.d.matchAll(/([MLH])\s*(-?[\d.]+)/g)) out.push(Number(m[2]));
    return out;
  });

describe("the transport's symbols", () => {
  it("there is one for each button and state", () => {
    expect(Object.keys(ICONS).sort()).toEqual(
      ["next", "pause", "play", "prev", "repeat", "repeatOne", "shuffle", "stop"].sort(),
    );
    for (const [name, shapes] of Object.entries(ICONS)) {
      expect(shapes.length, name).toBeGreaterThan(0);
      for (const s of shapes) {
        expect(s.d, name).toMatch(/^M/);
        expect(s.d, name).not.toMatch(/[a-z]/); // absolute commands only: every number is a place in the box
        for (const n of numbers(s.d)) {
          expect(n, `${name}: ${s.d}`).toBeGreaterThanOrEqual(0);
          expect(n, `${name}: ${s.d}`).toBeLessThanOrEqual(ICON_BOX);
        }
      }
    }
  });
  it("previous has its bar on the left, next is its mirror image", () => {
    expect(ICONS.next).toEqual(ICONS.prev.map((s) => ({ ...s, d: mirrored(s.d) })));
    expect(Math.min(...xs("prev"))).toBe(1);
    expect(Math.max(...xs("next"))).toBe(ICON_BOX - 1);
    expect(mirrored("M1 5H3V19L12 12Z")).toBe("M23 5H21V19L12 12Z");
  });
  it("lines are drawn as strokes, heads and blocks are filled", () => {
    expect(ICONS.shuffle.filter((s) => s.stroke)).toHaveLength(2); // the two crossing lines
    expect(ICONS.shuffle.filter((s) => !s.stroke)).toHaveLength(2); // their arrowheads
    expect(ICONS.repeat.filter((s) => s.stroke)).toHaveLength(2);
    expect(ICONS.repeat.filter((s) => !s.stroke)).toHaveLength(2);
    for (const name of ["prev", "next", "play", "pause", "stop"] as const) {
      expect(ICONS[name].every((s) => !s.stroke), name).toBe(true);
    }
  });
  it("repeat one is repeat with a 1 inside", () => {
    expect(ICONS.repeatOne.slice(0, ICONS.repeat.length)).toEqual(ICONS.repeat);
    expect(ICONS.repeatOne).toHaveLength(ICONS.repeat.length + 1);
  });
  it("which symbol a button shows", () => {
    expect(playIcon(false)).toBe("play");
    expect(playIcon(true)).toBe("pause"); // while playing, the button pauses
    expect(repeatIcon("off")).toBe("repeat");
    expect(repeatIcon("all")).toBe("repeat");
    expect(repeatIcon("one")).toBe("repeatOne");
  });
});

describe("the page leaves the symbols to icons.ts", () => {
  it("the transport's buttons carry no characters, only their names", () => {
    for (const id of ["shuffle", "prev", "play", "stop", "next", "repeat"]) {
      const m = new RegExp(`<button id="${id}"([^>]*)>([^<]*)</button>`).exec(html);
      expect(m, id).not.toBeNull();
      expect(m![2].trim(), id).toBe("");
      expect(m![1], id).toMatch(/aria-label="[^"]+"/);
    }
  });
  it("the 1 of repeat one is part of the drawing, not a character beside it", () => {
    expect(css).not.toMatch(/#repeat\[data-repeat="one"\]::after/);
  });
  it("a symbol is as large as the button's font size, in the button's colour", () => {
    expect(css).toMatch(/\.transport button\.icon svg\s*\{[^}]*width:\s*1em;[^}]*height:\s*1em;/);
  });
});
