// The standard view's VU meters (task 6-8). Drawn in SVG: the scale, its
// numbers and the needle are drawn by the program (CLAUDE.md chapter 8: no
// text baked into pictures), only the needle moves. The value is the
// engine's (a sine's RMS with a VU meter's 300 ms ballistics); the needle
// eases between the status updates (10 a second). It measures only, and it
// keeps moving in 原音モード, where only its light is lowered.

import type { Level } from "./status";
// The face behind the scale: cut out of the concept image with its print
// removed (tools/ui_mockup_parts.py).
import faceUrl from "./textures/vu-face.jpg";

export const VU_REFERENCE_DBFS = -18; // 0 VU
export const VU_SWEEP = 100; // degrees, as on a real VU meter (the concept image's needle is not geometric)
export const VU_MARKS = [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3];
const TOP = Math.pow(10, 3 / 20); // +3 VU, the end of the scale

export function vuOf(dbfs: Level): number {
  return dbfs === "-inf" ? -Infinity : dbfs - VU_REFERENCE_DBFS;
}

// Where on the scale (0..1): proportional to the voltage, as a VU meter's
// movement is, so 0 VU sits at about 71%.
export function needleFraction(vu: number): number {
  if (!Number.isFinite(vu)) return vu > 0 ? 1 : 0;
  return Math.min(1, Math.max(0, Math.pow(10, vu / 20) / TOP));
}

export function needleAngle(vu: number): number {
  return -VU_SWEEP / 2 + needleFraction(vu) * VU_SWEEP;
}

// --- drawing ----------------------------------------------------------------------

const SVG = "http://www.w3.org/2000/svg";
// In the face picture's own pixels (137 x 63, shown at 2/3: 91 x 42 CSS px).
// As on a real meter: the pivot at the bottom centre, just under the paper
// (hidden by the bezel), the scale on an arc above it; the numbers sit where
// the concept image has them.
const W = 137, H = 63, CX = 68, CY = 78, R = 52;
const FACE = { x: 6, y: 6, w: 126, h: 52 }; // the paper inside the bezel

function el<K extends keyof SVGElementTagNameMap>(name: K, attrs: Record<string, string | number>) {
  const e = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, String(v));
  return e;
}

function point(angle: number, radius: number) {
  const a = ((angle - 90) * Math.PI) / 180;
  return { x: CX + radius * Math.cos(a), y: CY + radius * Math.sin(a) };
}

export class VuMeter {
  readonly el: HTMLDivElement;
  private readonly needle: SVGLineElement;

  constructor(label: string) {
    const svg = el("svg", { viewBox: `0 0 ${W} ${H}`, class: "vu", role: "img", "aria-label": `VU メーター ${label}` });
    svg.append(el("image", { href: faceUrl, x: 0, y: 0, width: W, height: H, preserveAspectRatio: "none", class: "vu-face" }));
    // The red zone from 0 VU to +3, just inside the arc.
    const a0 = point(needleAngle(0), R - 2.5), a3 = point(needleAngle(3), R - 2.5);
    svg.append(el("path", { d: `M ${a0.x} ${a0.y} A ${R - 2.5} ${R - 2.5} 0 0 1 ${a3.x} ${a3.y}`, class: "vu-red" }));
    const scale = point(needleAngle(-20), R), end = point(needleAngle(3), R);
    svg.append(el("path", { d: `M ${scale.x} ${scale.y} A ${R} ${R} 0 0 1 ${end.x} ${end.y}`, class: "vu-arc" }));
    for (const m of VU_MARKS) {
      const angle = needleAngle(m);
      const p1 = point(angle, R), p2 = point(angle, R + (m % 5 === 0 || m >= -3 ? 6 : 4));
      svg.append(el("line", { x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y, class: m > 0 ? "vu-tick red" : "vu-tick" }));
      if ([-20, -10, -7, -5, -3, -1, 0, 1, 3].includes(m)) {
        const t = point(angle, R + 9);
        const text = el("text", { x: t.x, y: t.y + 3, class: m > 0 ? "vu-num red" : "vu-num" });
        text.textContent = m > 0 ? `+${m}` : String(m);
        svg.append(text);
      }
    }
    const vu = el("text", { x: CX, y: 50, class: "vu-label" });
    vu.textContent = "VU";
    svg.append(vu);
    // The needle shows only on the paper (a clip: a moving needle may be
    // painted on its own layer, above later elements).
    const clipId = `vu-clip-${label}-${Math.random().toString(36).slice(2, 8)}`;
    const clip = el("clipPath", { id: clipId });
    clip.append(el("rect", { x: FACE.x, y: FACE.y, width: FACE.w, height: FACE.h }));
    const holder = el("g", { "clip-path": `url(#${clipId})` });
    this.needle = el("line", { x1: CX, y1: CY, x2: CX, y2: CY - R - 5, class: "vu-needle" });
    holder.append(this.needle);
    svg.append(clip, holder);
    // The channel under the meter, as on the concept image.
    const unit = document.createElement("div");
    unit.className = "vu-unit";
    const ch = document.createElement("span");
    ch.className = "vu-ch";
    ch.textContent = label;
    unit.append(svg, ch);
    this.el = unit;
    this.set("-inf");
  }

  set(dbfs: Level | undefined) {
    const angle = needleAngle(vuOf(dbfs ?? "-inf"));
    this.needle.style.transform = `rotate(${angle.toFixed(2)}deg)`;
  }
}
