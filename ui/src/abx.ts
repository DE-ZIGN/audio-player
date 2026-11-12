// The ABX screen's data and texts (task 6-6). The test itself runs in the
// engine (AbxTest, task 4-1): results stay hidden until the last trial,
// "distinguished" means p < 0.05, and the texts never say which sounds better.

export type AbxKind = "processed" | "16bit";
export type AbxChoice = "A" | "B" | "X";

export interface AbxResultInfo {
  finished: boolean;
  aborted: boolean;
  trials: number;
  correct: number | null; // only when finished
  pValue: number | null; // only when finished
  distinguished: boolean;
  levelDifferenceDb: number | null;
  levelMatched: boolean;
}

export interface AbxStatus {
  running: boolean;
  kind: AbxKind;
  trial: number; // from 1
  trials: number;
  answered: number;
  listening: AbxChoice;
  startFrames: number;
  lengthFrames: number;
  rate: number;
  matchDb: number; // the gain on B to match A's loudness
  result: AbxResultInfo | null; // when it has ended
}

export interface AbxRequest {
  kind: AbxKind;
  startSeconds: number;
  seconds: number;
  trials: number;
}

export const KINDS: Record<AbxKind, { title: string; a: string; b: string }> = {
  processed: { title: "原音 と 音作り（今の設定）", a: "原音", b: "音作り" },
  "16bit": { title: "原音 と 16bit（TPDF ディザ）", a: "原音", b: "16bit" },
};

function choose(n: number, k: number): number {
  let c = 1;
  for (let i = 1; i <= k; i++) c = (c * (n - k + i)) / i;
  return c;
}

// P(at least `correct` of `trials` by guessing): the p of the test.
export function upperTailP(correct: number, trials: number): number {
  let sum = 0;
  for (let k = Math.max(0, correct); k <= trials; k++) sum += choose(trials, k);
  return Math.min(1, sum / Math.pow(2, trials));
}

// P(at most `correct` by guessing): for the "too few correct" note.
export function lowerTailP(correct: number, trials: number): number {
  return upperTailP(trials - correct, trials);
}

const signed = (x: number, digits: number) => {
  const r = Number(x.toFixed(digits));
  return r === 0 ? `+${(0).toFixed(digits)}` : `${r > 0 ? "+" : "−"}${Math.abs(r).toFixed(digits)}`;
};

export interface ResultText {
  headline: string;
  p: string;
  verdict: string;
  level: string;
  low: string; // "too few correct", or ""
}

export function resultText(r: AbxResultInfo): ResultText {
  const level =
    r.levelDifferenceDb === null
      ? "音量差：測れませんでした（流した時間が短すぎます。比べ始めの 3 秒は測りません）"
      : `音量差 ${signed(r.levelDifferenceDb, 2)} dB（基準は 0.1 dB 以内）` +
        (r.levelMatched ? "" : "：基準を超えたので、結果は参考です（音量の違いで聞き分けた可能性があります）");
  if (!r.finished || r.correct === null || r.pValue === null)
    return {
      headline: "中断しました",
      p: "",
      verdict: "途中で中断したので、結果は出しません（途中の結果で判断すると、p の意味が崩れるため）。",
      level: "",
      low: "",
    };
  const verdict = r.distinguished
    ? "p < 0.05：偶然とは考えにくい結果です。A と B の違いを聞き分けられた可能性が高いと言えます。"
    : "p ≥ 0.05：偶然でも十分に起こりうる結果です。聞き分けられたとは言えません（違いがないという意味ではありません）。";
  const lower = lowerTailP(r.correct, r.trials);
  const low =
    lower < 0.05
      ? `正解が少なすぎます（当てずっぽうでこれ以下になる確率は ${(lower * 100).toFixed(1)}%）。` +
        "A と B の違いは聞こえていて、取り違えている可能性があります。" +
        "この検定は「当たりが多いか」だけを見るので、これを「聞き分けられた」とは数えません。もう一度試すと確かめられます。"
      : "";
  return {
    headline: `${r.trials} 回中 ${r.correct} 回正解`,
    p: `p = ${r.pValue.toFixed(4)}（当てずっぽうでこれ以上当たる確率）`,
    verdict,
    level,
    low,
  };
}

function clock(seconds: number): string {
  const s = Math.floor(seconds);
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export function regionText(startFrames: number, lengthFrames: number, rate: number): string {
  if (rate <= 0) return "";
  const a = startFrames / rate, len = lengthFrames / rate;
  return `${clock(a)}〜${clock(a + len)}（${Math.round(len)} 秒）をくり返し`;
}

// The region to start with: from the current position, moved back so it fits.
export function defaultRegion(positionSeconds: number, totalSeconds: number, lengthSeconds: number) {
  const length = Math.min(lengthSeconds, Math.floor(totalSeconds));
  const start = Math.max(0, Math.min(Math.floor(positionSeconds), Math.floor(totalSeconds - length)));
  return { start, length };
}

export type KeyAction = AbxChoice | "answerA" | "answerB" | "abort";

export function keyAction(key: string): KeyAction | null {
  switch (key.toLowerCase()) {
    case "a":
      return "A";
    case "b":
      return "B";
    case "x":
      return "X";
    case "1":
      return "answerA";
    case "2":
      return "answerB";
    case "escape":
    case "q":
      return "abort";
    default:
      return null;
  }
}
