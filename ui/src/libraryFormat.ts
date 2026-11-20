// User-facing text for the library screen (task 6-1). The words are in the
// tables (i18n/parts/controls.ts).

import { t } from "./i18n";
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
      return { text: t("controls.library.peak.pending"), over: false, title: t("controls.library.peak.pendingTitle") };
    case "not_pcm":
      return { text: "DSD", over: false, title: t("controls.library.peak.notPcmTitle") };
    case "failed":
      return { text: t("controls.library.peak.failed"), over: false, title: t("controls.library.peak.failedTitle") };
    case "ok":
      break;
  }
  if (r.truePeakDb === null) return { text: "—", over: false, title: "" };
  const over = r.truePeakDb > 0;
  return {
    text: dbtpText(r.truePeakDb),
    over,
    title: over ? t("controls.library.peak.overTitle") : "",
  };
}

const n = (x: number) => x.toLocaleString("en-US");

export function countsText(c: LibraryCounts): string {
  if (c.problem) return c.problem;
  if (c.folders === 0) return t("controls.library.counts.noFolders");
  const parts = [t("controls.library.counts.tracks", { n: n(c.tracks) })];
  if (c.albums > 0) parts.push(t("controls.library.counts.albums", { n: n(c.albums) }));
  parts.push(t("controls.library.counts.folders", { n: c.folders }));
  if (c.scanning) parts.push(t("controls.library.counts.scanning"));
  if (c.toMeasure > 0) parts.push(t("controls.library.counts.toMeasure", { n: n(c.toMeasure) }));
  if (c.missing > 0) parts.push(t("controls.library.counts.missing", { n: n(c.missing) }));
  return parts.join(t("controls.library.separator"));
}
