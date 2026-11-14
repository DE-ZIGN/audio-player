// Shuffle and repeat (task 9-9: the push-pull amp has the buttons; every
// view has them, CLAUDE.md chapter 8).
//
// Repeat is the engine's (Player::setRepeat): what follows the last track,
// or each track. Shuffle is the page's: it mixes the tracks after the current
// one in the play queue (replaceUpcoming), so the queue shows the order they
// will play in; turning it off puts what is left back in the order it had.

export type RepeatMode = "off" | "all" | "one";

export interface PlayModes {
  shuffle: boolean;
  repeat: RepeatMode;
}

export const DEFAULT_PLAY_MODES: PlayModes = { shuffle: false, repeat: "off" };

const REPEAT_ORDER: RepeatMode[] = ["off", "all", "one"];

export function nextRepeat(m: RepeatMode): RepeatMode {
  return REPEAT_ORDER[(REPEAT_ORDER.indexOf(m) + 1) % REPEAT_ORDER.length];
}

export function repeatLabel(m: RepeatMode): string {
  return `リピート：${m === "off" ? "オフ" : m === "all" ? "全曲" : "1 曲"}`;
}

export function shuffleLabel(on: boolean): string {
  return `シャッフル：${on ? "オン" : "オフ"}`;
}

// The next and previous buttons: past either end they go round when repeat is
// on (one or all: the buttons still move from track to track).
export function nextTrackIndex(index: number, length: number, repeat: RepeatMode): number | null {
  if (length <= 0) return null;
  if (index + 1 < length) return index + 1;
  return repeat === "off" ? null : 0;
}

export function prevTrackIndex(index: number, length: number, repeat: RepeatMode): number | null {
  if (length <= 0) return null;
  if (index > 0) return index - 1;
  return repeat === "off" ? null : length - 1;
}

// --- shuffle -----------------------------------------------------------------

export function shuffled<T>(xs: readonly T[], rand: () => number = Math.random): T[] {
  const out = [...xs];
  for (let i = out.length - 1; i > 0; i--) {
    const j = Math.min(i, Math.floor(rand() * (i + 1)));
    [out[i], out[j]] = [out[j], out[i]];
  }
  return out;
}

// Playing a list with shuffle on: the chosen track first, then the others mixed.
export function shuffleStart(ids: readonly number[], start: number, rand: () => number = Math.random): number[] {
  if (ids.length === 0) return [];
  const s = Math.min(Math.max(0, start), ids.length - 1);
  return [ids[s], ...shuffled([...ids.slice(0, s), ...ids.slice(s + 1)], rand)];
}

// Shuffle off: the tracks still to come, in the order `original` had them.
// Each is matched with the first unused place of its id; tracks that were not
// in it (added while mixed) go last, in the order they are in now.
export function restoreOrder(upcoming: readonly number[], original: readonly number[]): number[] {
  const places = new Map<number, number[]>();
  original.forEach((id, i) => {
    const p = places.get(id);
    if (p) p.push(i);
    else places.set(id, [i]);
  });
  const keyed = upcoming.map((id, i) => ({ id, key: places.get(id)?.shift() ?? original.length + i }));
  return keyed.sort((a, b) => a.key - b.key).map((k) => k.id);
}

// Tracks added while mixed ("play next" or "add to the end") also take their
// place in the order to come back to: "next" after the current track there
// (the same occurrence, counted, when it is in the list more than once).
export function originalAfterInsert(
  original: readonly number[],
  queue: readonly number[],
  current: number,
  add: readonly number[],
  where: "next" | "end",
): number[] {
  if (where === "end" || current < 0 || current >= queue.length) return [...original, ...add];
  const id = queue[current];
  const nth = queue.slice(0, current + 1).filter((x) => x === id).length;
  let seen = 0;
  let at = -1;
  for (let i = 0; i < original.length; i++) {
    if (original[i] === id && ++seen === nth) {
      at = i;
      break;
    }
  }
  if (at < 0) return [...original, ...add];
  return [...original.slice(0, at + 1), ...add, ...original.slice(at + 1)];
}

// --- kept between runs ---------------------------------------------------------

interface Store {
  getItem(key: string): string | null;
  setItem(key: string, value: string): void;
}

export function loadPlayModes(storage: Store | null): PlayModes {
  if (!storage) return { ...DEFAULT_PLAY_MODES };
  try {
    const r = storage.getItem("repeat");
    return {
      shuffle: storage.getItem("shuffle") === "on",
      repeat: REPEAT_ORDER.includes(r as RepeatMode) ? (r as RepeatMode) : "off",
    };
  } catch {
    return { ...DEFAULT_PLAY_MODES };
  }
}

export function savePlayModes(storage: Store | null, m: PlayModes): void {
  try {
    storage?.setItem("shuffle", m.shuffle ? "on" : "off");
    storage?.setItem("repeat", m.repeat);
  } catch {
    // not kept
  }
}
