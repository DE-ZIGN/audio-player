// A made-up library for working on the page in a plain browser (no app).
// Deterministic: the same tracks and albums every time.

import type {
  ActionResult,
  AlbumQuery,
  AlbumRow,
  Folder,
  LibraryCounts,
  Page,
  TrackQuery,
  TrackRow,
} from "./library";

const ARTISTS = [
  "The Amber Quartet",
  "Linden Grove",
  "Marina Vale",
  "Evelyn Hart",
  "由紀さおり",
  "坂本 ちあき",
  "The Meridian Band",
  "Riverstone Trio",
  "Julian Mercer",
  "Björk Guðmundsdóttir",
  "ｽｷｬｯﾄ・ブラザーズ",
  "Aurora Fields",
];
const WORDS = ["Dawn", "Evening", "River", "Silent", "Glass", "After", "Golden", "Soft", "Night", "Copper", "Open", "Coastal"];
const WORDS2 = ["Breeze", "Glow", "Light", "Harbor", "Flowers", "Rain", "Hour", "Echoes", "fall", "Sky", "Fields", "Air"];
const JP = ["夜明けのスキャット", "手紙", "春の歌", "ひこうき雲", "夕暮れ", "海へ", "ふたり", "星のかけら"];

function rand(seed: number) {
  let s = seed >>> 0;
  return () => {
    s = (s * 1664525 + 1013904223) >>> 0;
    return s / 2 ** 32;
  };
}

function fold(s: string): string {
  // Close enough to the app's search form for a mock: width, case, katakana.
  return s
    .normalize("NFKC")
    .toLowerCase()
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .normalize("NFC")
    .replace(/[\u30a1-\u30f6]/g, (c) => String.fromCharCode(c.charCodeAt(0) - 0x60));
}

export class MockLibrary {
  private tracks: TrackRow[] = [];
  private albums: AlbumRow[] = [];
  private folders: Folder[] = [{ id: 1, path: "D:\\Music", tracks: 0, missing: 0 }];
  private listeners: ((c: LibraryCounts) => void)[] = [];
  private measured = 0;

  constructor(albumCount = 240) {
    const r = rand(7);
    let id = 1;
    for (let a = 0; a < albumCount; a++) {
      const artist = ARTISTS[a % ARTISTS.length];
      const album = a % 5 === 3 ? JP[a % JP.length] : `${WORDS[a % WORDS.length]} ${WORDS2[(a * 7) % WORDS2.length]}`;
      const key = `${artist}\u001f${album} ${a}`;
      const n = 6 + Math.floor(r() * 9);
      const rate = [44100, 48000, 96000, 192000][Math.floor(r() * 4)];
      const container = r() < 0.8 ? "FLAC" : r() < 0.5 ? "ALAC" : "MP3";
      let seconds = 0;
      let albumPeak = -10;
      const first = id;
      for (let t = 0; t < n; t++) {
        const len = 150 + Math.floor(r() * 300);
        const peak = Math.round((r() * 2.5 - 2) * 100) / 100;
        albumPeak = Math.max(albumPeak, peak);
        seconds += len;
        this.tracks.push({
          id,
          title: t % 4 === 2 ? JP[(a + t) % JP.length] : `${WORDS[(a + t) % WORDS.length]} ${WORDS2[(t * 5 + a) % WORDS2.length]}`,
          artist,
          album: `${album} ${a}`,
          albumKey: key,
          trackNo: t + 1,
          discNo: null,
          seconds: len,
          container,
          sampleRate: container === "MP3" ? 44100 : rate,
          dsdRate: null,
          bits: container === "MP3" ? null : rate > 48000 ? 24 : 16,
          analysis: "ok",
          truePeakDb: peak,
          loudnessLufs: -14 + peak,
          plays: Math.floor(r() * 60),
          rating: null,
          present: true,
          path: `D:\\Music\\${artist}\\${album}\\${String(t + 1).padStart(2, "0")}.flac`,
        });
        id++;
      }
      this.albums.push({
        key,
        album: `${album} ${a}`,
        artist,
        year: String(1970 + (a % 55)),
        tracks: n,
        seconds,
        coverTrack: first,
        truePeakDb: albumPeak,
        container,
        sampleRate: container === "MP3" ? 44100 : rate,
      });
    }
    // A few still to be measured, one DSD, one that does not decode.
    for (const t of this.tracks.slice(-30)) {
      t.analysis = "pending";
      t.truePeakDb = null;
    }
    if (this.tracks.length > 9) {
      Object.assign(this.tracks[5], { container: "DSF", sampleRate: null, dsdRate: 2822400, bits: 1, analysis: "not_pcm", truePeakDb: null });
      Object.assign(this.tracks[9], { analysis: "failed", truePeakDb: null });
    }
    if (this.tracks.length === 0) this.folders = [];
    else this.folders[0].tracks = this.tracks.length;
    // The mock "measures" the pending ones over a few seconds.
    const timer = setInterval(() => {
      const next = this.tracks.find((t) => t.analysis === "pending");
      if (!next) return clearInterval(timer);
      next.analysis = "ok";
      next.truePeakDb = -1.5;
      this.measured++;
      this.emit();
    }, 400);
  }

  coverUrl(trackId: number): string {
    // A gradient per album, as an SVG data URL.
    const t = this.tracks[trackId - 1];
    let h = 0;
    for (const c of t?.albumKey ?? "") h = (h * 31 + c.charCodeAt(0)) >>> 0;
    const a = h % 360;
    const b = (a + 40 + (h % 80)) % 360;
    const svg =
      `<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'>` +
      `<stop offset='0' stop-color='hsl(${a},55%,45%)'/><stop offset='1' stop-color='hsl(${b},60%,22%)'/></linearGradient></defs>` +
      `<rect width='100' height='100' fill='url(#g)'/><circle cx='70' cy='35' r='14' fill='hsla(${(a + 180) % 360},70%,75%,.55)'/></svg>`;
    return `data:image/svg+xml,${encodeURIComponent(svg)}`;
  }

  counts(): LibraryCounts {
    const toMeasure = this.tracks.filter((t) => t.analysis === "pending").length;
    return {
      tracks: this.tracks.length,
      missing: 0,
      albums: this.albums.length,
      toMeasure,
      folders: this.folders.length,
      scanning: false,
      problem: "",
    };
  }

  listFolders(): Folder[] {
    return this.folders.map((f) => ({ ...f }));
  }

  addFolder(): ActionResult {
    const id = this.folders.length + 1;
    this.folders.push({ id, path: `E:\\More Music ${id}`, tracks: 0, missing: 0 });
    this.emit();
    return { ok: true };
  }

  removeFolder(id: number) {
    this.folders = this.folders.filter((f) => f.id !== id);
    this.emit();
  }

  trackPage(q: TrackQuery): Page<TrackRow> {
    let rows = this.tracks;
    if (q.albumKey) rows = rows.filter((t) => t.albumKey === q.albumKey);
    const words = fold(q.search ?? "").split(" ").filter(Boolean);
    if (words.length)
      rows = rows.filter((t) => {
        const fields = [t.title, t.artist, t.album, t.path.split("\\").pop() ?? ""].map(fold);
        return words.every((w) => fields.some((f) => f.includes(w)));
      });
    const key = (t: TrackRow): (string | number)[] => {
      if (q.albumKey) return [t.discNo ?? 0, t.trackNo ?? 0];
      switch (q.sort ?? "artist") {
        case "album":
          return [fold(t.album), t.trackNo ?? 0];
        case "title":
          return [fold(t.title)];
        case "length":
          return [t.seconds ?? Infinity];
        case "format":
          return [t.container, t.sampleRate ?? 0];
        case "rate":
          return [t.sampleRate ?? t.dsdRate ?? 0];
        case "truePeak":
          return [t.truePeakDb ?? -Infinity];
        case "plays":
          return [t.plays];
        case "added":
          return [t.id];
        default:
          return [fold(t.artist), fold(t.album), t.trackNo ?? 0];
      }
    };
    const sorted = [...rows].sort((x, y) => {
      const a = key(x),
        b = key(y);
      for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
      return x.id - y.id;
    });
    if (q.descending && !q.albumKey) sorted.reverse();
    return { rows: sorted.slice(q.offset, q.offset + q.limit), total: sorted.length };
  }

  albumPage(q: AlbumQuery): Page<AlbumRow> {
    let rows = this.albums;
    if (q.search) {
      const keys = new Set(this.trackPage({ search: q.search, offset: 0, limit: 1e9 }).rows.map((t) => t.albumKey));
      rows = rows.filter((a) => keys.has(a.key));
    }
    const key = (a: AlbumRow) =>
      q.sort === "album" ? fold(a.album) : q.sort === "year" ? a.year ?? "" : q.sort === "added" ? a.coverTrack : fold(a.artist + " " + a.album);
    const sorted = [...rows].sort((x, y) => (key(x) < key(y) ? -1 : key(x) > key(y) ? 1 : 0));
    if (q.descending) sorted.reverse();
    return { rows: sorted.slice(q.offset, q.offset + q.limit), total: sorted.length };
  }

  track(id: number): TrackRow | undefined {
    return this.tracks[id - 1];
  }

  onChanged(listener: (c: LibraryCounts) => void) {
    this.listeners.push(listener);
  }

  private emit() {
    const c = this.counts();
    for (const l of this.listeners) l(c);
  }
}
