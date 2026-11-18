import { describe, expect, it } from "vitest";
import css from "./style.css?raw";

// The top bar in a narrow window (2026-11-18): the track's length ran over
// the volume bar in the library view at 1115 px. A slider inside a flex row
// keeps its built-in width (129 px) unless told it may shrink, so the seek
// row was wider than its box.

// The first rule with exactly this selector (at the start of a line).
function rule(selector: string): string {
  const lines = css.replace(/\r\n/g, "\n").split("\n");
  const start = lines.indexOf(`${selector} {`);
  if (start < 0) return "";
  const end = lines.indexOf("}", start);
  return lines.slice(start + 1, end).join("\n");
}

const px = (text: string, property: string) => Number(new RegExp(`(?:^|\\n)\\s*${property}:\\s*(\\d+)px`).exec(text)?.[1]);

describe("the top bar's rows give way instead of running over each other", () => {
  it("the seek bar may shrink inside its row; the row keeps room for the two times and a short bar", () => {
    expect(rule(".seek input")).toMatch(/min-width:\s*0;/);
    const time = px(rule(".time"), "width");
    expect(time).toBe(44);
    expect(px(rule('[data-look="flat"] .seek'), "min-width")).toBeGreaterThanOrEqual(2 * time + 2 * 4 + 60); // times, gaps, at least 60 px of bar
  });
  it("the library view's volume row narrows before anything overlaps, down to a slider of 50 px", () => {
    expect(rule('[data-look="flat"] .volume-box')).toMatch(/flex:\s*0 1 178px;/);
    expect(px(rule('[data-look="flat"] .volume-box'), "min-width")).toBeGreaterThanOrEqual(22 + 4 + 50 + 4 + 48); // mute, slider, the number
    expect(px(rule(".volume-box input"), "min-width")).toBe(50);
  });
  it("the standard and amp views, laid out at one size, keep their widths", () => {
    expect(rule(".volume-box")).toMatch(/flex:\s*none;/);
    expect(px(rule('[data-look="real"] .volume-box'), "width")).toBe(130);
    expect(px(rule(".seek"), "min-width")).toBe(140);
  });
});
