// The views (task 6-7, CLAUDE.md chapter 8): the standard view (realistic,
// wood and amber light; its photo-like parts come with 6-8), the library
// view (modern and flat, two colour themes) and the amp view (task 9-9: a
// tube amp on top, the standard view's panels under it). Only the look and
// the layout differ: the page is the same parts with the same functions and
// information, switched by attributes on the root element and the CSS
// variables they set. The first start is the standard view; then the view
// last used.

import { t } from "./i18n";

export type UiView = "standard" | "library" | "amp";
export const UI_VIEWS: UiView[] = ["standard", "amp", "library"];

export const UI_VIEW_KEY = "uiView";

// Each read gives the name in the language in use (i18n.ts).
export const VIEW_LABELS: Record<UiView, string> = {
  get standard() {
    return t("page.view.standard");
  },
  get library() {
    return t("page.view.library");
  },
  get amp() {
    return t("page.view.amp");
  },
};

// The realistic look (wood, metal, amber light) is shared by the standard
// and the amp views: their panels, lists and type are the same.
export type UiLook = "real" | "flat";

export function lookOf(view: UiView): UiLook {
  return view === "library" ? "flat" : "real";
}

export interface StorageLike {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export function loadUiView(storage: StorageLike): UiView {
  try {
    const v = storage.getItem(UI_VIEW_KEY);
    return v === "library" || v === "amp" ? v : "standard";
  } catch {
    return "standard";
  }
}

export function saveUiView(storage: StorageLike, view: UiView) {
  try {
    storage.setItem(UI_VIEW_KEY, view);
  } catch {
    // no storage: the view lasts until the window closes
  }
}

// The views the side bar offers to switch to: the other two, in a fixed order.
export function otherViews(view: UiView): UiView[] {
  return UI_VIEWS.filter((v) => v !== view);
}

export function currentUiView(): UiView {
  const v = document.documentElement.dataset.uiView;
  return v === "library" || v === "amp" ? v : "standard";
}

// Shows the view (without saving it). Listeners redraw what depends on sizes.
const listeners: ((v: UiView) => void)[] = [];

export function applyUiView(view: UiView) {
  document.documentElement.dataset.uiView = view;
  document.documentElement.dataset.look = lookOf(view);
  for (const l of listeners) l(view);
}

export function onUiViewChange(listener: (v: UiView) => void) {
  listeners.push(listener);
}

// A size the current view sets as a CSS variable, in pixels.
export function cssPixels(name: string, fallback: number): number {
  const v = parseFloat(getComputedStyle(document.documentElement).getPropertyValue(name));
  return Number.isFinite(v) && v > 0 ? v : fallback;
}

// The standard view is the concept image scaled to the window (task 6-8,
// 2026-11-13): its layout is made for 1115 x 605 CSS px (the 1672 x 908 image
// read at 150%), and the whole view is zoomed so that it fills the window the
// way the image would - a maximised 1920 x 1080 window shows it at about
// 1.67 times. The smaller of the two ratios decides, so nothing is pushed out.
export const STANDARD_DESIGN = { width: 1115, height: 605 };
const MIN_ZOOM = 0.75;
const MAX_ZOOM = 4;

export function standardZoom(width: number, height: number): number {
  if (!Number.isFinite(width) || !Number.isFinite(height)) return 1;
  const z = Math.min(width / STANDARD_DESIGN.width, height / STANDARD_DESIGN.height);
  return Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, z));
}

// Sets --ui-zoom on the root element (the standard and amp views' CSS use
// it; the amp view is drawn for the same 1115 x 605, from its own concept
// images at the same 150%).
export function applyStandardZoom() {
  const z = lookOf(currentUiView()) === "real" ? standardZoom(window.innerWidth, window.innerHeight) : 1;
  document.documentElement.style.setProperty("--ui-zoom", z.toFixed(4));
}
