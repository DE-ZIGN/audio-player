// Shapes of the library data the app sends (task 6-1). Must match
// app/src/LibraryBridge.cpp.

export interface TrackRow {
  id: number;
  title: string; // the file name when there is no tag
  artist: string;
  album: string;
  albumKey: string | null;
  trackNo: number | null;
  discNo: number | null;
  seconds: number | null;
  container: string; // "FLAC", "MP3", ... ("" until read)
  sampleRate: number | null;
  dsdRate: number | null;
  bits: number | null;
  // "ok" (truePeakDb set), "pending" (not measured yet), "not_pcm" (DSD), "failed"
  analysis: "ok" | "pending" | "not_pcm" | "failed";
  truePeakDb: number | null;
  loudnessLufs: number | null;
  plays: number;
  rating: number | null;
  present: boolean;
  path: string;
}

export interface AlbumRow {
  key: string;
  album: string;
  artist: string;
  year: string | null;
  tracks: number;
  seconds: number | null;
  coverTrack: number;
  truePeakDb: number | null;
  container: string; // shared by all tracks, else ""
  sampleRate: number | null; // shared by all tracks
}

export interface Page<T> {
  rows: T[];
  total: number;
}

export type TrackSort = "artist" | "album" | "title" | "length" | "format" | "rate" | "truePeak" | "plays" | "added";
export type AlbumSort = "artist" | "album" | "year" | "added";

export interface TrackQuery {
  search?: string;
  sort?: TrackSort;
  descending?: boolean;
  albumKey?: string;
  offset: number;
  limit: number;
}

export interface AlbumQuery {
  search?: string;
  sort?: AlbumSort;
  descending?: boolean;
  offset: number;
  limit: number;
}

export interface Folder {
  id: number;
  path: string;
  tracks: number;
  missing: number;
}

export interface LibraryCounts {
  tracks: number;
  missing: number;
  albums: number;
  toMeasure: number; // waiting for the analysis
  folders: number;
  scanning: boolean;
  problem: string; // why the library could not be opened (empty: fine)
}

export interface ActionResult {
  ok: boolean;
  cancelled?: boolean;
  message?: string;
}

export const emptyCounts: LibraryCounts = {
  tracks: 0,
  missing: 0,
  albums: 0,
  toMeasure: 0,
  folders: 0,
  scanning: false,
  problem: "",
};
