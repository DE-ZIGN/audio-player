// The tube type by genre (task 9-7): the settings' shape, the texts, and the
// page's copy of the engine's choice (engine/src/dsp/tube_auto.cpp), used by
// the mock backend only; in the app the engine chooses and reports it.

import { t } from "./i18n";
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
    .replace(/[！-～]/g, (c) => String.fromCharCode(c.charCodeAt(0) - 0xfee0)) // full-width ！ to ～
    .replace(/[　\t ]+/g, " ") // the full-width space too
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
  if (rules.length > MAX_GENRE_RULES) return t("controls.tubeAuto.problem.tooManyRules", { max: MAX_GENRE_RULES });
  const seen = new Map<string, string>();
  for (const r of rules) {
    const k = normalizedGenre(r.genre);
    if (k === "") return t("controls.tubeAuto.problem.emptyGenre");
    if (r.genre.length > MAX_GENRE_LENGTH) return t("controls.tubeAuto.problem.genreTooLong", { max: MAX_GENRE_LENGTH });
    const before = seen.get(k);
    if (before !== undefined) return t("controls.tubeAuto.problem.sameGenre", { first: before, second: r.genre.trim() });
    seen.set(k, r.genre.trim());
  }
  return null;
}

// The line that says which type plays for the current track and why.
export function choiceText(s: TubeAutoSettings, st: TubeAutoStatus | null | undefined, manual: TubeType): string {
  const m = { manual: TUBE_LABELS[manual] };
  if (!s.enabled) return t("controls.tubeAuto.choice.off", m);
  if (!st) return t("controls.tubeAuto.choice.notPlaying", m);
  const c = st.choice;
  if (c?.reason === "album") return t("controls.tubeAuto.choice.album", { album: c.matched, type: TUBE_LABELS[c.type] });
  if (c) return t("controls.tubeAuto.choice.genre", { genre: c.matched, type: TUBE_LABELS[c.type] });
  if (st.genres.length === 0) return t("controls.tubeAuto.choice.noGenre", m);
  return t("controls.tubeAuto.choice.unknownGenre", { ...m, genres: st.genres.join(t("controls.tubeAuto.genreSeparator")) });
}
