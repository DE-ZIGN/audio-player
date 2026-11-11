// User-facing text for the library screen (task 6-1). Japanese first.

import type { LibraryCounts, TrackRow } from "./library";

export function lengthText(seconds: number | null): string {
  if (seconds === null || !Number.isFinite(seconds)) return "—";
  const s = Math.floor(seconds);
  const h = Math.floor(s / 3600);
  const m = Math.floor((s % 3600) / 60);
  const sec = String(s % 60).padStart(2, "0");
  return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${sec}` : `${m}:${sec}`;
}

export function dsdName(rate: number): string {
  return `DSD${Math.round(rate / 44100)}`;
}

export function rateText(r: Pick<TrackRow, "sampleRate" | "dsdRate">): string {
  if (r.dsdRate) return dsdName(r.dsdRate);
  if (!r.sampleRate) return "—";
  return `${Number((r.sampleRate / 1000).toFixed(1))} kHz`;
}

export function formatText(r: Pick<TrackRow, "container" | "bits" | "dsdRate">): string {
  if (!r.container) return "—";
  if (r.dsdRate || !r.bits || r.bits <= 1) return r.container;
  return `${r.container} ${r.bits}bit`;
}

export interface PeakCell {
  text: string;
  over: boolean; // above 0 dBTP: the ISP warning (CLAUDE.md chapter 8)
  title: string; // a tooltip
}

export function dbtpText(db: number): string {
  return `${db > 0 ? "+" : ""}${db.toFixed(1)} dBTP`;
}

export function truePeakCell(r: Pick<TrackRow, "analysis" | "truePeakDb">): PeakCell {
  switch (r.analysis) {
    case "pending":
      return { text: "測定待ち", over: false, title: "True Peak とラウドネスを裏で測っています" };
    case "not_pcm":
      return { text: "DSD", over: false, title: "DSD は PCM ではないので測りません" };
    case "failed":
      return { text: "測れません", over: false, title: "デコードできませんでした（ファイルが壊れている可能性があります）" };
    case "ok":
      break;
  }
  if (r.truePeakDb === null) return { text: "—", over: false, title: "" };
  const over = r.truePeakDb > 0;
  return {
    text: dbtpText(r.truePeakDb),
    over,
    title: over
      ? "True Peak が 0 dBTP を超えています。原音経路のままだと、DAC の中でクリップする可能性があります（ISP セーフで下げられます）"
      : "",
  };
}

const n = (x: number) => x.toLocaleString("en-US");

export function countsText(c: LibraryCounts): string {
  if (c.problem) return c.problem;
  if (c.folders === 0) return "監視するフォルダーを加えてください（左の「＋」）";
  const parts = [`${n(c.tracks)} 曲`];
  if (c.albums > 0) parts.push(`アルバム ${n(c.albums)}`);
  parts.push(`監視中のフォルダー ${c.folders}`);
  if (c.scanning) parts.push("フォルダーを確認中");
  if (c.toMeasure > 0) parts.push(`測定待ち ${n(c.toMeasure)} 曲`);
  if (c.missing > 0) parts.push(`見つからない ${n(c.missing)} 曲`);
  return parts.join(" ・ ");
}
