// The tube type by genre (task 9-7): the settings' shape, the texts, and the
// page's copy of the engine's choice (engine/src/dsp/tube_auto.cpp), used by
// the mock backend only; in the app the engine chooses and reports it.

import { TUBE_LABELS, type TubeType } from "./settings";

export interface TubeGenreRule {
  genre: string;
  type: TubeType;
}

export interface TubeAlbumChoice {
  albumKey: string;
  album: string; // the name, for the page
  type: TubeType;
}

export interface TubeAutoSettings {
  enabled: boolean;
  rules: TubeGenreRule[];
  albums: TubeAlbumChoice[];
}

export interface TubeChoice {
  type: TubeType;
  reason: "album" | "genre";
  matched: string; // the genre as the tag has it, or the album's name
}

// With the status: the current track's album, genre tag values, and what chose its type.
export interface TubeAutoStatus {
  album: { key: string; title: string } | null;
  genres: string[];
  choice: TubeChoice | null;
}

export const MAX_GENRE_RULES = 200;
export const MAX_GENRE_LENGTH = 100;

// As the engine compares genres: ASCII lower case, full-width ASCII as
// half-width, runs of spaces as one, no spaces at the ends.
export function normalizedGenre(g: string): string {
  return g
    .replace(/[！-～]/g, (c) => String.fromCharCode(c.charCodeAt(0) - 0xfee0))
    .replace(/[　\t ]+/g, " ")
    .trim()
    .replace(/[A-Z]/g, (c) => c.toLowerCase());
}

// The album's choice, then an exact genre match (in the tag's order), then a
// table genre inside a tag's part (in the table's order). Null: the manual type.
export function chooseTubeType(s: TubeAutoSettings, genres: string[], albumKey: string): TubeChoice | null {
  if (!s.enabled) return null;
  if (albumKey) {
    const a = s.albums.find((x) => x.albumKey === albumKey);
    if (a) return { type: a.type, reason: "album", matched: a.album };
  }
  const parts = genres
    .flatMap((g) => g.split(/[;/,]/))
    .map((p) => ({ asWritten: p.trim(), key: normalizedGenre(p) }))
    .filter((p) => p.key !== "");
  const keys = s.rules.map((r) => normalizedGenre(r.genre));
  for (const p of parts) {
    const i = keys.findIndex((k) => k !== "" && k === p.key);
    if (i >= 0) return { type: s.rules[i].type, reason: "genre", matched: p.asWritten };
  }
  for (const p of parts) {
    const i = keys.findIndex((k) => k !== "" && p.key.includes(k));
    if (i >= 0) return { type: s.rules[i].type, reason: "genre", matched: p.asWritten };
  }
  return null;
}

// Sets (or with null, removes) an album's choice.
export function withAlbumChoice(
  s: TubeAutoSettings,
  album: { key: string; title: string },
  type: TubeType | null,
): TubeAutoSettings {
  const albums = s.albums.filter((a) => a.albumKey !== album.key);
  if (type) albums.push({ albumKey: album.key, album: album.title, type });
  return { ...s, albums };
}

// What would stop the table from being saved as the user has it, or null.
export function rulesProblem(rules: TubeGenreRule[]): string | null {
  if (rules.length > MAX_GENRE_RULES) return `対応表は ${MAX_GENRE_RULES} 行までです`;
  const seen = new Map<string, string>();
  for (const r of rules) {
    const k = normalizedGenre(r.genre);
    if (k === "") return "ジャンルが空の行があります";
    if (r.genre.length > MAX_GENRE_LENGTH) return `ジャンルは ${MAX_GENRE_LENGTH} 文字までです`;
    const before = seen.get(k);
    if (before !== undefined) return `「${before}」と「${r.genre.trim()}」は同じジャンルとして扱われます`;
    seen.set(k, r.genre.trim());
  }
  return null;
}

// The line that says which type plays for the current track and why.
export function choiceText(s: TubeAutoSettings, st: TubeAutoStatus | null | undefined, manual: TubeType): string {
  const manualText = `手動で選んだ種類（${TUBE_LABELS[manual]}）`;
  if (!s.enabled) return `自動で選びません。いつも${manualText}です。`;
  if (!st) return `再生していません。曲ごとに、アルバムの指定 → ジャンルの対応表 → ${manualText}の順で決めます。`;
  const c = st.choice;
  if (c?.reason === "album") return `今の曲：アルバム「${c.matched}」の指定で ${TUBE_LABELS[c.type]}。`;
  if (c) return `今の曲：ジャンル「${c.matched}」が対応表にあるので ${TUBE_LABELS[c.type]}。`;
  if (st.genres.length === 0) return `今の曲：ジャンルのタグがないので、${manualText}。`;
  return `今の曲：ジャンル「${st.genres.join("・")}」は対応表にないので、${manualText}。`;
}
