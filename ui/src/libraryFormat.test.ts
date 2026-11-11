import { describe, expect, it } from "vitest";
import { countsText, formatText, lengthText, rateText, truePeakCell } from "./libraryFormat";
import { emptyCounts, type TrackRow } from "./library";

const row = (r: Partial<TrackRow>): TrackRow => ({
  id: 1,
  title: "t",
  artist: "",
  album: "",
  albumKey: null,
  trackNo: null,
  discNo: null,
  seconds: 254,
  container: "FLAC",
  sampleRate: 96000,
  dsdRate: null,
  bits: 24,
  analysis: "ok",
  truePeakDb: -0.83,
  loudnessLufs: -14,
  plays: 0,
  rating: null,
  present: true,
  path: "",
  ...r,
});

describe("Track list cells (task 6-1)", () => {
  it("shows lengths as m:ss, and h:mm:ss past an hour", () => {
    expect(lengthText(254)).toBe("4:14");
    expect(lengthText(59.6)).toBe("0:59");
    expect(lengthText(3725)).toBe("1:02:05");
    expect(lengthText(null)).toBe("—");
  });
  it("shows the rate, and DSD by its name", () => {
    expect(rateText(row({}))).toBe("96 kHz");
    expect(rateText(row({ sampleRate: 44100 }))).toBe("44.1 kHz");
    expect(rateText(row({ sampleRate: null, dsdRate: 2822400 }))).toBe("DSD64");
    expect(rateText(row({ sampleRate: null }))).toBe("—");
  });
  it("shows the format with the bit depth when there is one", () => {
    expect(formatText(row({}))).toBe("FLAC 24bit");
    expect(formatText(row({ container: "MP3", bits: null }))).toBe("MP3");
    expect(formatText(row({ container: "" }))).toBe("—");
  });
  it("shows the true peak, and marks what goes above 0 dBTP", () => {
    expect(truePeakCell(row({}))).toEqual({ text: "-0.8 dBTP", over: false, title: "" });
    const over = truePeakCell(row({ truePeakDb: 2.96 }));
    expect(over.text).toBe("+3.0 dBTP");
    expect(over.over).toBe(true);
    expect(over.title).toContain("0 dBTP を超えています");
  });
  it("says why there is no true peak", () => {
    expect(truePeakCell(row({ analysis: "pending", truePeakDb: null })).text).toBe("測定待ち");
    expect(truePeakCell(row({ analysis: "not_pcm", truePeakDb: null })).text).toBe("DSD");
    const failed = truePeakCell(row({ analysis: "failed", truePeakDb: null }));
    expect(failed.text).toBe("測れません");
    expect(failed.title).toContain("デコードできませんでした");
  });
});

describe("Status bar (task 6-1)", () => {
  it("counts the library and says what is going on", () => {
    expect(countsText({ ...emptyCounts, tracks: 12480, albums: 1024, folders: 2 })).toBe(
      "12,480 曲 ・ アルバム 1,024 ・ 監視中のフォルダー 2",
    );
    const busy = countsText({ ...emptyCounts, tracks: 10, folders: 1, scanning: true, toMeasure: 7, missing: 3 });
    expect(busy).toContain("フォルダーを確認中");
    expect(busy).toContain("測定待ち 7 曲");
    expect(busy).toContain("見つからない 3 曲");
  });
  it("asks for a folder when there is none", () => {
    expect(countsText(emptyCounts)).toBe("監視するフォルダーを加えてください（左の「＋」）");
  });
});
