import { describe, expect, it } from "vitest";
import type { Page, TrackQuery, TrackRow } from "./library";
import { ALBUM_LIMIT, PLAY_LIMIT, playFromRow, sameTrackTwice, type PressedRow } from "./libraryPlay";
import view from "./libraryView.ts?raw";

// The track pressed is the track played (found 2026-11-22 while taking the
// Store screenshots: a row's index was applied to whatever the list had become).

const track = (id: number, present = true) => ({ id, present }) as TrackRow;
const range = (from: number, to: number) => Array.from({ length: to - from }, (_, i) => from + i);

// The app's libraryTracks over a list as it is now: at most 2000 rows at once.
function library(now: TrackRow[]) {
  const asked: TrackQuery[] = [];
  const tracks = async (q: TrackQuery): Promise<Page<TrackRow>> => {
    asked.push(q);
    const from = q.offset ?? 0;
    return { rows: now.slice(from, from + Math.min(q.limit ?? 200, 2000)), total: now.length };
  };
  return { tracks, asked };
}

const inList = (index: number, id: number): PressedRow => ({
  list: { search: "", sort: "artist", descending: false },
  index,
  id,
});
const inAlbum = (index: number, id: number): PressedRow => ({
  list: { search: "", sort: "artist", descending: false, albumKey: "album" },
  index,
  id,
});

async function played(now: TrackRow[], pressed: PressedRow) {
  const r = await playFromRow(library(now).tracks, pressed);
  return r && r.ids[r.start];
}

describe("playing from a row of an unchanged list", () => {
  it("queues the list from that row on", async () => {
    const now = range(100, 110).map((id) => track(id));
    expect(await playFromRow(library(now).tracks, inList(3, 103))).toEqual({ ids: range(103, 110), start: 0 });
  });

  it("queues an album whole, from that track", async () => {
    const now = range(100, 107).map((id) => track(id));
    expect(await playFromRow(library(now).tracks, inAlbum(2, 102))).toEqual({ ids: range(100, 107), start: 2 });
  });

  it("queues up to the limit, asking once", async () => {
    const lib = library(range(0, 5000).map((id) => track(id)));
    expect((await playFromRow(lib.tracks, inList(40, 40)))?.ids).toEqual(range(40, 40 + PLAY_LIMIT));
    expect(lib.asked).toHaveLength(1);
  });

  it("asks with the list the row was drawn in", async () => {
    const lib = library([track(7)]);
    const list = { search: "ferry", sort: "title", descending: true } as const;
    await playFromRow(lib.tracks, { list, index: 0, id: 7 });
    expect(lib.asked[0]).toMatchObject(list);
  });

  it("leaves out missing files, and still starts at the track pressed", async () => {
    const now = [track(1, false), track(2), track(3, false), track(4), track(5)];
    expect(await playFromRow(library(now).tracks, inAlbum(3, 4))).toEqual({ ids: [2, 4, 5], start: 1 });
  });

  it("plays nothing from a row shown missing: not the list's next one, not an album's first", async () => {
    const now = [track(1), track(2, false), track(3)];
    expect(await played(now, inList(1, 2))).toBeNull();
    expect(await played(now, inAlbum(1, 2))).toBeNull();
  });

  it("plays a row shown missing once its file is back", async () => {
    expect(await played([track(1), track(2), track(3)], inAlbum(1, 2))).toBe(2);
  });
});

describe("playing from a row of a list that has changed since it was drawn", () => {
  it("plays the track pressed when a track came in above it", async () => {
    const now = [99, ...range(100, 110)].map((id) => track(id));
    expect(await played(now, inList(3, 103))).toBe(103);
  });

  it("plays it when one above it went", async () => {
    const now = range(101, 110).map((id) => track(id));
    expect(await played(now, inList(3, 103))).toBe(103);
  });

  it("plays it when it moved far up (by plays: just played)", async () => {
    const now = [7000, ...range(0, 7000)].map((id) => track(id));
    expect(await played(now, inList(6500, 7000))).toBe(7000);
  });

  it("plays it when it moved far down, with a whole queue after it", async () => {
    const now = range(0, 9000).map((id) => track(id));
    const r = await playFromRow(library(now).tracks, inList(5, 2999));
    expect(r).toEqual({ ids: range(2999, 2999 + PLAY_LIMIT), start: 0 });
  });

  it("plays it when the album's order changed", async () => {
    const now = [104, 100, 101, 102, 103].map((id) => track(id));
    expect(await played(now, inAlbum(2, 102))).toBe(102);
  });

  it("plays it past the most an album gives at once", async () => {
    const now = range(0, ALBUM_LIMIT + 700).map((id) => track(id));
    expect(await played(now, inAlbum(ALBUM_LIMIT + 300, ALBUM_LIMIT + 300))).toBe(ALBUM_LIMIT + 300);
  });

  it("never falls back to an album's first track (the row of the list before: index 7, 7 tracks)", async () => {
    const now = range(100, 107).map((id) => track(id));
    expect(await played(now, inAlbum(7, 999))).toBeNull();
  });

  it("plays nothing when the track has left the list", async () => {
    const now = range(100, 110).map((id) => track(id));
    expect(await played(now, inList(3, 999))).toBeNull();
  });

  it("plays nothing when its file went missing after it was drawn", async () => {
    const now = [track(1), track(2, false), track(3)];
    expect(await played(now, inList(1, 2))).toBeNull();
    expect(await played(now, inAlbum(1, 2))).toBeNull();
  });
});

describe("a double click", () => {
  it("plays when both clicks were on one track", () => {
    expect(sameTrackTwice(5, 5)).toBe(true);
  });

  it("does not when the list came in between them, or the first was on an album's tile", () => {
    expect(sameTrackTwice(4, 5)).toBe(false);
    expect(sameTrackTwice(null, 5)).toBe(false);
  });
});

// There is no DOM in these tests, so the view's wiring is read from its source.
describe("the track list", () => {
  it("hands the row as drawn to playing, not an index", () => {
    expect(view).toMatch(/playFromRow\(/);
    expect(view).not.toMatch(/playFrom\(i\)|playFrom\(this\.selected\)/);
  });

  it("takes the rows away as soon as another list is asked for", () => {
    const reload = /private reload\(resetScroll: boolean\) \{[\s\S]*?\n {2}\}/.exec(view)?.[0];
    expect(reload).toMatch(/this\.els\.body\.replaceChildren\(this\.els\.bodySpacer\)/);
  });

  it("plays on a double click only if both clicks were on one track", () => {
    expect(view).toMatch(/sameTrackTwice\(this\.firstClicked, r\.id\)/);
  });
});
