// User-facing text (Japanese first). Describes what the player does; never
// claims that something sounds better.

import type { DropoutCounts, FormatInfo, PlaybackStatus, PlayerState, TrackTransition } from "./status";

function rateText(rate: number): string {
  return `${Number((rate / 1000).toFixed(3))} kHz`;
}

export function describeFormat(f: FormatInfo): string {
  let bits: string;
  if (f.sampleType === "float") bits = `${f.containerBits}bit 浮動小数点`;
  else if (f.validBits === f.containerBits) bits = `${f.validBits}bit`;
  else bits = `${f.validBits}bit（${f.containerBits}bit コンテナ）`;
  return `${rateText(f.sampleRate)} / ${bits}${bits.endsWith("）") ? "" : " "}/ ${f.channels}ch`;
}

export function formatTime(frames: number, sampleRate: number): string {
  if (!sampleRate || frames <= 0) return "0:00";
  const total = Math.floor(frames / sampleRate);
  const h = Math.floor(total / 3600);
  const m = Math.floor((total % 3600) / 60);
  const s = total % 60;
  const ss = String(s).padStart(2, "0");
  return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${ss}` : `${m}:${ss}`;
}

export function stateLabel(state: PlayerState): string {
  switch (state) {
    case "empty":
      return "ファイルを開いてください";
    case "stopped":
      return "停止中";
    case "playing":
      return "再生中";
    case "ended":
      return "最後まで再生しました";
    case "error":
      return "再生できません";
  }
}

export function queueText(s: PlaybackStatus): string {
  return s.queueLength > 1 ? `${s.queueIndex + 1} / ${s.queueLength} 曲目` : "";
}

export function transitionText(t: TrackTransition): string {
  const tracks = `${t.fromIndex + 1} → ${t.toIndex + 1} 曲目`;
  if (t.kind === "gapless") return `${tracks}：ギャップレス（形式が同じなので出力を開き直さずにつなげました）`;
  return (
    `${tracks}：形式の切り替え ${describeFormat(t.fromFormat)} → ${describeFormat(t.toFormat)}` +
    `（出力を開き直しました。前の曲のデータが途切れてから次の曲を送り始めるまで約 ${Math.round(t.switchMilliseconds)} ms）`
  );
}

export function dropoutText(d: DropoutCounts): string {
  const total = d.lateBuffers + d.deviceTimeouts + d.readStarvations;
  if (total === 0) return "0 回";
  const parts: string[] = [];
  if (d.lateBuffers > 0) parts.push(`デバイスへの受け渡しの遅れ ${d.lateBuffers}`);
  if (d.readStarvations > 0) parts.push(`ファイルの読み込みの遅れ ${d.readStarvations}`);
  if (d.deviceTimeouts > 0) parts.push(`デバイスの応答なし ${d.deviceTimeouts}`);
  return `${total} 回（${parts.join("、")}）`;
}

export function priorityText(realtime: boolean): string {
  return realtime
    ? "MMCSS（Pro Audio）で優先度を上げています（音切れを防ぐため）"
    : "通常の優先度（MMCSS が使えませんでした）";
}

export function repackText(s: PlaybackStatus): string {
  const src = s.sourceFormat;
  const out = s.outputFormat;
  switch (s.repack) {
    case "none":
      return "なし（ファイルのバイト列をそのまま送ります）";
    case "widerContainer":
      return src && out
        ? `${src.containerBits}bit → ${out.containerBits}bit コンテナ（有効 ${out.validBits}bit、下位は 0。値は変わりません）`
        : "コンテナの詰め替え（値は変わりません）";
    case "zeroPadded":
      return src && out
        ? `${src.validBits}bit → ${out.validBits}bit（下位 ${out.validBits - src.validBits}bit は 0。値は変わりません）`
        : "ゼロ詰め（値は変わりません）";
  }
}

export function errorText(s: PlaybackStatus): string {
  const e = s.error;
  if (e.kind === "none") return "";
  const fmt = s.sourceFormat;
  const rate = fmt ? rateText(fmt.sampleRate) : "この";

  if (e.kind === "formatNotSupported") {
    switch (e.reason) {
      case "sampleRate": {
        const accepted = e.acceptedRates ?? [];
        const now = accepted.length > 0 ? `このデバイスが今受け付けるのは ${accepted.map(rateText).join("、")} です。` : "";
        return (
          `このデバイスは排他モードで ${rate} に対応していません。レートの変換はしません。${now}` +
          `デバイスの設定（オーディオインターフェースのコントロールパネルなど）でレートを ${rate} にすると再生できる場合があります。`
        );
      }
      case "bitDepth":
        return `このデバイスは ${rate} には対応していますが、この形式（${fmt ? describeFormat(fmt) : ""}）には対応していません。`;
      case "channels":
        return `このデバイスは ${fmt?.channels ?? ""} チャンネルに対応していません。`;
      case "sampleType":
        return "このデバイスは浮動小数点の形式に対応していません。";
      default:
        return "このデバイスはこの形式に対応していません。";
    }
  }

  if (e.kind === "output") {
    switch (e.outputError) {
      case "deviceNotFound":
        return "出力デバイスが見つかりません。";
      case "deviceInUse":
        return "他のアプリがこのデバイスを排他モードで使っています。そのアプリを止めてから試してください。";
      case "exclusiveModeNotAllowed":
        return (
          "このデバイスは排他モードが許可されていません。サウンドの設定 → このデバイスのプロパティ → 詳細 で" +
          "「アプリケーションによりこのデバイスを排他的に制御できるようにする」をオンにしてください。"
        );
      case "formatNotSupported":
        return "デバイスがこの形式を受け付けませんでした。";
      case "deviceInvalidated":
        return "デバイスが外されたか、設定が変わりました。";
      case "timeout":
        return "デバイスがデータを要求しなくなりました。";
      default:
        return `デバイスのエラーです（${e.detail}）。`;
    }
  }

  if (e.kind === "load") return `${s.queueIndex + 1} 曲目のファイルを開けません: ${e.detail}`;

  return `エラー: ${e.detail}`;
}
