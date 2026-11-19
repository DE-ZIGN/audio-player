import { describe, expect, it } from "vitest";
import css from "./style.css?raw";
import view from "./ampView.ts?raw";
import ppParts from "../../tools/ui_amp_pp_parts.py?raw";

// The amps' play buttons (2026-11-19, the user saw a band of other wood round
// the ring, and smeared wood round the PP one).
// SE: the page moves the button from where the picture draws it, so its
// picture is cut round the ring, transparent past it, and the page hangs the
// shadow and the glow on that shape, not on the button's own circle (3 px out
// from the ring).
// PP: the page has the button where the picture draws it, so it stays on the
// amp pictures, ring, glow and wood, and the page draws only the symbol.

function rule(selector: string): string {
  const lines = css.replace(/\r\n/g, "\n").split("\n");
  const start = lines.indexOf(`${selector} {`);
  if (start < 0) return "";
  const end = lines.indexOf("}", start);
  return lines.slice(start + 1, end).join("\n");
}

const SE_PLAY = '[data-look="real"] .amp-stage[data-look="se"] .transport button.play';

describe("the SE amp's play button, cut round its ring", () => {
  it("the picture is the cut one (transparent round the ring)", () => {
    expect(view).toMatch(/import sePlay from "\.\/textures\/amp\/play\.png";/);
    expect(view).toMatch(/se: \{[^}]*play: sePlay \}/);
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
    expect(css).not.toMatch(/\.amp-stage \.transport button\.play::before/);
  });
});

const PP_PLAY = '[data-look="real"] .amp-stage[data-look="pp"] .transport button.play';

describe("the PP amp's play button, left on the amp pictures", () => {
  it("the page has no picture of its own for it", () => {
    expect(view).not.toMatch(/pp-play/);
    expect(view).toMatch(/pp: \{[^}]*play: null \}/);
    expect(view).toMatch(/"--button-play", pic\.play === null \? "none" :/);
  });
  it("the button draws only its symbol: no picture, no shadow", () => {
    const button = rule(PP_PLAY);
    expect(button).toMatch(/background:\s*none;/);
    expect(button).toMatch(/box-shadow:\s*none;/);
    expect(rule(`${PP_PLAY}:active:not(:disabled)`)).toMatch(/box-shadow:\s*none;/);
  });
  it("pressed, the glass inside the ring goes darker; with no track, the ring is dimmed with the symbol", () => {
    expect(rule(`${PP_PLAY}:active:not(:disabled)`)).toMatch(/background:\s*radial-gradient\(/);
    const dimmed = rule(`${PP_PLAY}:disabled::before`);
    expect(dimmed).toMatch(/background:\s*radial-gradient\(/);
    expect(dimmed).toMatch(/z-index:\s*-1;/); // behind the symbol
    expect(dimmed).toMatch(/inset:\s*calc\(var\(--d\) \* -0\.06\);/); // the ring ends outside the button's circle
  });
  it("the pictures keep the play button: only its symbol is removed, the wood round the others is filled as before", () => {
    expect(ppParts).not.toMatch(/pp-play\.(jpg|png)'/);
    expect(ppParts).toMatch(/filled\[kept\] = part\[kept\]/);
    expect(ppParts).toMatch(/without_symbol\(crop\(out, box\), x - box\[0\], y - box\[1\], PLAY_SYMBOL_R\)/);
  });
});
