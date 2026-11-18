// The transport's symbols (task 9-9, 2026-11-18): shuffle, previous, play,
// pause, stop, next, repeat. Drawn here (SVG) in the shapes of the concept
// image UI-AMP-PP, instead of characters from a font (thin, and different
// from machine to machine). CLAUDE.md chapter 8: what is printed on a part is
// drawn by the program, so one bare button picture serves every state.
//
// Each symbol sits in a 24 x 24 box and is as large as the button's font
// size (style.css: `.transport button.icon svg`), in the button's colour.
//
// Measured on the concept image (its pixels; the small buttons are 57 across):
//   previous / next  28 wide, 17.5 high: a 2.5 bar and two triangles
//   shuffle          29 wide, two 2.2 lines 12.5 apart that cross, arrowheads 6 x 7.5
//   repeat           25 wide, 2.2 lines 14.5 apart with round corners, arrowheads
//   pause            two bars 6.2 wide, 8 apart, 26.7 high (the large button, 78 across)
// One unit of the box is about 1.3 of those pixels, so the lines are 1.7 wide.
// Play and stop are not in the image: play is as high as pause, stop as high
// as the triangles.

import type { RepeatMode } from "./playModes";

export type IconName = "shuffle" | "prev" | "play" | "pause" | "stop" | "next" | "repeat" | "repeatOne";

export interface IconShape {
  d: string; // absolute path commands only
  stroke?: boolean; // a line (round ends); otherwise filled
}

export const ICON_BOX = 24;
export const ICON_STROKE = 1.7;

// Left and right swapped: M, L, H, V and Z only (enough for previous and next).
export function mirrored(d: string): string {
  return d.replace(/([MLH])\s*(-?[\d.]+)/g, (_, c: string, x: string) => `${c}${+(ICON_BOX - Number(x)).toFixed(2)}`);
}

const PREV: IconShape[] = [
  { d: "M1 5.2H2.9V18.8H1Z" }, // the bar
  { d: "M12.7 5.2V18.8L3.1 12Z" },
  { d: "M23 5.2V18.8L13.2 12Z" },
];

const REPEAT: IconShape[] = [
  // Up the left side, round the corner, along the top to its arrowhead.
  { d: "M2.9 11.6V10.2A4 4 0 0 1 6.9 6.2H18", stroke: true },
  { d: "M17.6 3.2L22.6 6.2L17.6 9.2Z" },
  // Down the right side, round the corner, back along the bottom.
  { d: "M21.1 12.4V13.8A4 4 0 0 1 17.1 17.8H6", stroke: true },
  { d: "M6.4 14.8L1.4 17.8L6.4 20.8Z" },
];

export const ICONS: Record<IconName, IconShape[]> = {
  shuffle: [
    // From the upper left down to the lower right, and the other way, crossing in the middle.
    { d: "M1.8 7.2H6.5C10.2 7.2 10.8 16.8 14.5 16.8H18.6", stroke: true },
    { d: "M1.8 16.8H6.5C10.2 16.8 10.8 7.2 14.5 7.2H18.6", stroke: true },
    { d: "M18.2 4.2L23 7.2L18.2 10.2Z" },
    { d: "M18.2 13.8L23 16.8L18.2 19.8Z" },
  ],
  prev: PREV,
  next: PREV.map((s) => ({ ...s, d: mirrored(s.d) })),
  play: [{ d: "M6.5 2V22L23 12Z" }],
  pause: [{ d: "M4.4 2H9V22H4.4Z" }, { d: "M15 2H19.6V22H15Z" }],
  stop: [{ d: "M5.5 5.5H18.5V18.5H5.5Z" }],
  repeat: REPEAT,
  repeatOne: [...REPEAT, { d: "M10.6 10.1L12.3 9H13.4V15H11.9V10.9L10.6 11.7Z" }],
};

// While playing, the button pauses.
export function playIcon(playing: boolean): IconName {
  return playing ? "pause" : "play";
}

export function repeatIcon(mode: RepeatMode): IconName {
  return mode === "one" ? "repeatOne" : "repeat";
}

const SVG_NS = "http://www.w3.org/2000/svg";

// Puts the symbol on the button (its only content). The button keeps its
// name in aria-label; the drawing is hidden from screen readers.
export function setIcon(button: HTMLElement, name: IconName) {
  if (button.dataset.icon === name) return;
  const svg = document.createElementNS(SVG_NS, "svg");
  svg.setAttribute("viewBox", `0 0 ${ICON_BOX} ${ICON_BOX}`);
  svg.setAttribute("aria-hidden", "true");
  svg.setAttribute("focusable", "false");
  for (const shape of ICONS[name]) {
    const path = document.createElementNS(SVG_NS, "path");
    path.setAttribute("d", shape.d);
    if (shape.stroke) {
      path.setAttribute("fill", "none");
      path.setAttribute("stroke", "currentColor");
      path.setAttribute("stroke-width", String(ICON_STROKE));
      path.setAttribute("stroke-linecap", "round");
      path.setAttribute("stroke-linejoin", "round");
    } else {
      path.setAttribute("fill", "currentColor");
    }
    svg.append(path);
  }
  button.replaceChildren(svg);
  button.dataset.icon = name;
}
