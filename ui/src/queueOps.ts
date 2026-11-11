// Pure helpers for the playback controls (task 6-2).

export type Db = number | "-inf";

// The volume slider (0..1) and the digital volume in dB. The curve is
// amplitude = position squared (40 log10), so the slider feels even:
// halfway is about -12 dB. The top is exactly 0 dB (100%: no processing).
export function sliderToDb(x: number): Db {
  if (x >= 1) return 0;
  if (x <= 0) return "-inf";
  return Math.round(40 * Math.log10(x) * 10) / 10;
}

export function dbToSlider(db: Db): number {
  if (db === "-inf") return 0;
  if (db >= 0) return 1;
  return Math.pow(10, db / 40);
}

export function volumeLabel(db: Db): string {
  if (db === "-inf") return "ミュート";
  if (db === 0) return "100%";
  return `${db > 0 ? "+" : ""}${db.toFixed(1)} dB`;
}

// --- what comes next --------------------------------------------------------
// The queue is a list of library ids; `current` is the index playing. Each
// helper gives the new list of ids after the current track (for
// replaceUpcoming), or null when the change is not allowed.

export function upcoming(ids: number[], current: number): number[] {
  return ids.slice(current + 1);
}

export function afterInsert(ids: number[], current: number, add: number[], where: "next" | "end"): number[] {
  const rest = ids.length === 0 ? [] : upcoming(ids, current);
  return where === "next" ? [...add, ...rest] : [...rest, ...add];
}

export function afterRemove(ids: number[], current: number, index: number): number[] | null {
  if (index <= current || index >= ids.length) return null;
  return ids.filter((_, i) => i > current && i !== index);
}

export function afterMove(ids: number[], current: number, from: number, to: number): number[] | null {
  if (from <= current || to <= current || from >= ids.length || to >= ids.length) return null;
  const rest = upcoming(ids, current);
  const [moved] = rest.splice(from - current - 1, 1);
  rest.splice(to - current - 1, 0, moved);
  return rest;
}
