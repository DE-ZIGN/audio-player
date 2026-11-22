// What to play when a row of the track list is pressed: the track the row
// showed. The list may have changed since it was drawn (a scan, the play
// counts, another sort), so the track is found by its id; its index is only
// where to look first.

import type { Page, TrackQuery, TrackRow } from "./library";

export interface PlayRequest {
  ids: number[];
  start: number;
}

// A row as it was drawn: the list it belongs to and the track it showed.
export interface PressedRow {
  list: Omit<TrackQuery, "offset" | "limit">;
  index: number;
  id: number;
}

export const PLAY_LIMIT = 1000; // tracks put in the queue from a long list
export const ALBUM_LIMIT = 2000; // the most the app gives at once

type Tracks = (q: TrackQuery) => Promise<Page<TrackRow>>;

// An album whole (from that track), or the list from that row on. Null when
// the track is no longer there to play: another one is never played for it.
export async function playFromRow(tracks: Tracks, pressed: PressedRow): Promise<PlayRequest | null> {
  const ask = (offset: number, limit: number) => tracks({ ...pressed.list, offset, limit });

  if (pressed.list.albumKey !== undefined) {
    const { rows } = await ask(0, ALBUM_LIMIT);
    const at = rows.findIndex((r) => r.id === pressed.id);
    if (at >= 0) return request(rows, at);
    // Not among those given at once: an album that long is played as a list is.
  }

  const here = await ask(pressed.index, PLAY_LIMIT);
  if (here.rows[0]?.id === pressed.id) return request(here.rows, 0);

  // It has moved. The pages overlap by half: taken from the half that has a whole queue after it.
  for (let offset = 0; ; offset += PLAY_LIMIT) {
    const { rows, total } = await ask(offset, PLAY_LIMIT * 2);
    const last = rows.length === 0 || offset + rows.length >= total;
    const at = rows.findIndex((r) => r.id === pressed.id);
    if (at >= 0 && (at < PLAY_LIMIT || last)) return request(rows.slice(at, at + PLAY_LIMIT), 0);
    if (last) return null;
  }
}

function request(rows: TrackRow[], at: number): PlayRequest | null {
  const found = rows[at];
  if (!found.present) return null; // its file is missing, shown so or since
  const playable = rows.filter((r) => r.present);
  return { ids: playable.map((r) => r.id), start: playable.indexOf(found) };
}

// Whether a double click's two clicks were on one track: the rows can arrive
// between them, and the first can have been on an album's tile.
export function sameTrackTwice(firstClicked: number | null, id: number): boolean {
  return firstClicked === id;
}
