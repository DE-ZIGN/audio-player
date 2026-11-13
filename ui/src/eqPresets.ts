// EQ presets (task 6-9): an EQ curve - the bands and the preamp - under a
// name. Whether the EQ is used, and the mode, stay with the settings. The
// list is kept by the app (settingsSetEqPresets, saved with the settings);
// the page edits it as a whole with these pure functions.

import type { EqBand, ListeningSettings } from "./settings";

export interface EqPreset {
  name: string;
  bands: EqBand[];
  autoPreamp: boolean;
  manualPreampDb: number;
}

export const MAX_PRESETS = 100;
export const MAX_PRESET_NAME = 60;

// For the ABX's 設定 A と設定 B: the EQ left out.
export const FLAT_PRESET: EqPreset = { name: "EQ なし（平ら）", bands: [], autoPreamp: true, manualPreampDb: 0 };

export function presetName(raw: string): string | null {
  const name = [...raw.replace(/\s+/g, " ").trim()].slice(0, MAX_PRESET_NAME).join("").trim();
  return name === "" ? null : name;
}

export function presetFrom(l: ListeningSettings, name: string): EqPreset {
  return { name, bands: structuredClone(l.eqBands), autoPreamp: l.eqAutoPreamp, manualPreampDb: l.eqManualPreampDb };
}

export function applyPreset(l: ListeningSettings, p: EqPreset): ListeningSettings {
  return { ...l, eqBands: structuredClone(p.bands), eqAutoPreamp: p.autoPreamp, eqManualPreampDb: p.manualPreampDb };
}

function sameBands(a: EqBand[], b: EqBand[]) {
  return (
    a.length === b.length &&
    a.every(
      (x, i) =>
        x.type === b[i].type && x.frequency === b[i].frequency && x.gainDb === b[i].gainDb && x.q === b[i].q && x.enabled === b[i].enabled,
    )
  );
}

// The name of the preset the current EQ is, or null. When several are,
// `preferred` (the one last chosen or saved) if it is among them.
export function matchingPreset(l: ListeningSettings, presets: EqPreset[], preferred = ""): string | null {
  const same = presets.filter(
    (p) =>
      sameBands(p.bands, l.eqBands) &&
      p.autoPreamp === l.eqAutoPreamp &&
      (p.autoPreamp || p.manualPreampDb === l.eqManualPreampDb),
  );
  return (same.find((p) => p.name === preferred) ?? same[0])?.name ?? null;
}

export type ListResult = { ok: true; list: EqPreset[] } | { ok: false; message: string };

// Saves under the preset's name: a name in use is replaced in its place.
export function withSaved(list: EqPreset[], p: EqPreset): ListResult {
  const name = presetName(p.name);
  if (!name) return { ok: false, message: "名前を入れてください" };
  const saved = { ...structuredClone(p), name };
  const at = list.findIndex((q) => q.name === name);
  if (at >= 0) return { ok: true, list: list.map((q, i) => (i === at ? saved : q)) };
  if (list.length >= MAX_PRESETS) return { ok: false, message: `プリセットは ${MAX_PRESETS} 個までです。使わないものを消してください` };
  return { ok: true, list: [...list, saved] };
}

export function withRenamed(list: EqPreset[], from: string, to: string): ListResult {
  const name = presetName(to);
  if (!name) return { ok: false, message: "名前を入れてください" };
  const at = list.findIndex((p) => p.name === from);
  if (at < 0) return { ok: false, message: "そのプリセットは見つかりません" };
  if (name !== from && list.some((p) => p.name === name)) return { ok: false, message: `「${name}」はもうあります` };
  return { ok: true, list: list.map((p, i) => (i === at ? { ...p, name } : p)) };
}

export function withDeleted(list: EqPreset[], name: string): EqPreset[] {
  return list.filter((p) => p.name !== name);
}
