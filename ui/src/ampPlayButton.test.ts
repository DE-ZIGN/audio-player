import { describe, expect, it } from "vitest";
import css from "./style.css?raw";
import view from "./ampView.ts?raw";

// The SE amp's play button (2026-11-19, the user saw a band of other wood
// round its ring): its picture is cut round the ring, transparent past it,
// and the page hangs the shadow and the glow on that shape, not on the
// button's own circle (3 px out from the ring). The PP amp's is as it was.

function rule(selector: string): string {
  const lines = css.replace(/\r\n/g, "\n").split("\n");
  const start = lines.indexOf(`${selector} {`);
  if (start < 0) return "";
  const end = lines.indexOf("}", start);
  return lines.slice(start + 1, end).join("\n");
}

const SE_PLAY = '[data-look="real"] .amp-stage[data-look="se"] .transport button.play';

describe("the SE amp's play button, cut round its ring", () => {
  it("the picture is the cut one (transparent round the ring); the PP amp keeps its own", () => {
    expect(view).toMatch(/import sePlay from "\.\/textures\/amp\/play\.png";/);
    expect(view).toMatch(/import ppPlay from "\.\/textures\/amp\/pp-play\.jpg";/);
  });
  it("the button itself draws nothing: no picture clipped to its circle, no shadow on its circle", () => {
    const button = rule(SE_PLAY);
    expect(button).toMatch(/background:\s*none;/);
    expect(button).toMatch(/box-shadow:\s*none;/);
    expect(button).toMatch(/isolation:\s*isolate;/); // the picture stays behind the symbol, in front of the wood
    expect(rule(`${SE_PLAY}:active:not(:disabled)`)).toMatch(/box-shadow:\s*none;/);
  });
  it("the picture is behind the symbol, and the shadow and the glow follow its shape", () => {
    const picture = rule(`${SE_PLAY}::before`);
    expect(picture).toMatch(/background:\s*var\(--button-play\) center \/ cover;/);
    expect(picture).toMatch(/z-index:\s*-1;/);
    expect(picture).toMatch(/filter:\s*drop-shadow\([^)]*\)\) drop-shadow\([^)]*\)\);/);
    expect(picture).not.toMatch(/box-shadow/);
    expect(rule(`${SE_PLAY}:active:not(:disabled)::before`)).toMatch(/filter:\s*drop-shadow/);
  });
  it("only the SE amp is drawn so", () => {
    expect(css).not.toMatch(/\.amp-stage\[data-look="pp"\] \.transport button\.play/);
    expect(css).not.toMatch(/\.amp-stage \.transport button\.play::before/);
  });
});
