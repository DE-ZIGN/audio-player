// Settings of the mockup overlay (task 6-8, development only): a concept image
// from the work folder's imagegen/accepted/ laid over the page, to line the
// standard view up with it. Read from and written to the page's query, so a
// reload keeps them. Nothing here ships in the app.

export type MockupMode = "overlay" | "diff" | "wipe";

export interface MockupParams {
  name: string | null; // the image's file name; null = no overlay
  mode: MockupMode;
  opacity: number; // 0..1, for "overlay"
  dx: number; // where the image's top-left corner goes, in CSS pixels
  dy: number;
  wipe: number; // 0..1, where the line is for "wipe" (the mockup is left of it)
  // The screen scaling the image is taken to be drawn at: 1 = one image pixel
  // per CSS pixel (a 100% screen); 1.5 = a 150% screen, so the image is shown
  // at 2/3 of its size.
  zoom: number;
}

// The concept images include a drawn window title bar (y 0..32 of 1672×941).
// The app uses Windows' own title bar, outside the page, so the page starts at
// the image's y = 33. Measured on UI-STANDARD-v1 and UI-STANDARD-LIST-v1.
export const MOCKUP_TITLE_BAR = 33;

const MODES: readonly MockupMode[] = ["overlay", "diff", "wipe"];

// A file name in the mockup folder: no path, png or jpeg only.
export function safeMockupName(raw: string): string | null {
  const name = raw.includes(".") ? raw : `${raw}.png`; // "UI-STANDARD-v1" → .png
  return /^[A-Za-z0-9_-][A-Za-z0-9._-]*\.(png|jpe?g)$/i.test(name) && !name.includes("..") ? name : null;
}

const clamp = (v: number, lo: number, hi: number) => Math.min(hi, Math.max(lo, v));

function num(q: URLSearchParams, key: string, fallback: number): number {
  const raw = q.get(key);
  if (raw === null || raw.trim() === "") return fallback;
  const v = Number(raw);
  return Number.isFinite(v) ? v : fallback;
}

export function parseMockupParams(search: string): MockupParams {
  const q = new URLSearchParams(search);
  const raw = q.get("mockup");
  const mode = q.get("mode") as MockupMode | null;
  const z = num(q, "zoom", 1);
  const zoom = z > 0 ? clamp(z, 0.25, 4) : 1;
  return {
    name: raw ? safeMockupName(raw) : null,
    mode: mode && MODES.includes(mode) ? mode : "overlay",
    opacity: clamp(num(q, "opacity", 0.5), 0, 1),
    dx: Math.round(num(q, "dx", 0)),
    dy: Math.round(num(q, "dy", -MOCKUP_TITLE_BAR / zoom)),
    wipe: clamp(num(q, "wipe", 0.5), 0, 1),
    zoom,
  };
}

export function writeMockupParams(search: string, p: MockupParams): string {
  const q = new URLSearchParams(search);
  if (p.name) q.set("mockup", p.name);
  else q.delete("mockup");
  q.set("mode", p.mode);
  q.set("opacity", String(p.opacity));
  q.set("dx", String(p.dx));
  q.set("dy", String(p.dy));
  q.set("wipe", String(p.wipe));
  q.set("zoom", String(p.zoom));
  return `?${q.toString()}`;
}

export function mockupClip(mode: MockupMode, wipe: number): string {
  return mode === "wipe" ? `inset(0 ${Math.round((1 - wipe) * 1000) / 10}% 0 0)` : "none";
}
