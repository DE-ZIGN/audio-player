// User-facing text (Japanese first). Describes what the player does; never
// claims that something sounds better.

import type { DropoutCounts, FormatInfo, PlaybackStatus, PlayerState, SignalPath, StageState, TrackTransition } from "./status";

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
  if (t.kind === "processingChanged")
    return (
      `${t.fromIndex + 1} 曲目：処理経路と原音経路の切り替え（音量などが変わったため、今の位置で出力を開き直しました。` +
      `約 ${Math.round(t.switchMilliseconds)} ms）`
    );
  return (
    `${tracks}：形式の切り替え ${describeFormat(t.fromFormat)} → ${describeFormat(t.toFormat)}` +
    `（出力を開き直しました。前の曲のデータが途切れてから次の曲を送り始めるまで約 ${Math.round(t.switchMilliseconds)} ms）`
  );
}

export function dsdRateName(rate: number): string {
  if (rate % 44100 === 0 && (rate / 44100) % 64 === 0) return `DSD${rate / 44100}`;
  if (rate % 48000 === 0 && (rate / 48000) % 64 === 0) return `DSD${rate / 48000}（48 kHz 系）`;
  return `DSD ${rate} Hz`;
}

// The "input" line: DSD files are described as DSD, not as their DoP wrapper.
export function inputText(s: PlaybackStatus): string {
  if (!s.sourceFormat) return "—";
  if (!s.dop) return describeFormat(s.sourceFormat) + (s.path?.container ? `（${s.path.container}）` : "");
  const mhz = Number((s.dop.dsdRate / 1_000_000).toFixed(4));
  return `${dsdRateName(s.dop.dsdRate)}（${mhz} MHz）/ ${s.sourceFormat.channels}ch（${s.dop.container}）`;
}

const stageNames: Record<string, string> = {
  volume: "デジタル音量",
  replaygain: "ReplayGain",
  eq: "パラメトリック EQ",
  "isp-safe": "ISP セーフ",
  limiter: "保険のリミッター",
  comparison: "聴き比べ",
};

// The BIT-PERFECT line (CLAUDE.md chapter 8): shown only when the engine
// judged it from the facts of the path; otherwise why not, with the values
// (e.g. "DIGITAL VOL -6.0 dB").
export function bitPerfectText(s: PlaybackStatus): string {
  const v = s.bitPerfect;
  if (!v) return "—";
  const deviceNote =
    v.deviceVolume !== null
      ? `。デバイス側の音量 ${v.deviceMuted ? "ミュート" : `${Math.round(v.deviceVolume * 100)}%`}：DAC に届いたあとで信号が変わります（この判定はドライバーまで）`
      : "";
  if (v.bitPerfect) {
    const notes: string[] = [];
    if (v.dop) notes.push("DSD を DoP でそのまま");
    if (v.repacked) notes.push("コンテナの詰め替えあり、値は同じ");
    if (v.floatSource) notes.push("非可逆の曲をデコードした値のまま");
    if (v.dummyOutput) notes.push("ダミー出力（ファイル）");
    return `BIT-PERFECT（アプリからドライバーまで値をそのまま${notes.length ? "。" + notes.join("、") : ""}）${deviceNote}`;
  }
  const reasons = s.stages
    .filter((st) => st.name !== "limiter" && st.name !== "comparison")
    .map((st) => (st.name === "volume" && st.gainDb !== null ? `DIGITAL VOL ${dbText(st.gainDb)}` : stageText(st)));
  if (v.quantized) reasons.push("浮動小数点の曲を整数に変換");
  if (v.comparison) reasons.push("聴き比べ中（原音側も値は同じだが処理経路を通る）");
  if (v.dither) reasons.push("TPDF ディザ");
  if (v.deliveryMismatch && s.delivery) reasons.push(`自己確認で不一致（${s.delivery.mismatchedFrames} フレーム）`);
  return `BIT-PERFECT ではありません：${reasons.join("、")}${deviceNote}`;
}

function dbText(db: number | "-inf"): string {
  if (db === "-inf") return "-∞ dB";
  return `${db > 0 ? "+" : ""}${db.toFixed(1)} dB`;
}

// One stage with its current value, e.g. "デジタル音量 -6.0 dB".
export function stageText(st: StageState): string {
  const name = stageNames[st.name] ?? st.name;
  if (st.gainDb === null) return name;
  if (st.name === "eq") return `${name}（プリアンプ ${dbText(st.gainDb)}）`;
  return `${name} ${dbText(st.gainDb)}`;
}

export function processingText(s: PlaybackStatus): string {
  if (s.dop) return "なし（DoP の経路では処理をかけられません）";
  const p = s.path;
  if (!p || !p.processing) return "なし（原音経路：音量・変換・ディザをかけません）";
  const parts = s.stages.length > 0 ? s.stages.map(stageText) : p.processingStages.map((n) => stageNames[n] ?? n);
  if (p.processingRequired) parts.push("浮動小数点の曲を整数にする（このデバイスは浮動小数点を受け付けないため）");
  if (p.dither) parts.push("TPDF ディザ");
  return `あり（64bit 浮動小数点の処理経路：${parts.join("、")}）`;
}

export function dopNotice(s: PlaybackStatus): string {
  if (!s.dop || !s.sourceFormat) return "";
  return (
    `DSD を DoP（PCM ${rateText(s.sourceFormat.sampleRate)} / 24bit に包む形）で送っています。` +
    "DoP に対応していない DAC では、大きなノイズになります。DAC の表示が DSD になっているか確かめてください。"
  );
}

function periodText(frames: number, ms: number): string {
  return `${frames} フレーム（${Number(ms.toFixed(2))} ms）`;
}

// How the output layer delivers the data (from what the output reported).
export function outputModeText(path: SignalPath): string {
  const o = path.output;
  switch (o.api) {
    case "wasapiExclusive":
      return `排他 WASAPI${o.eventDriven ? "（イベント駆動）" : ""}・1 回に ${periodText(o.periodFrames, o.periodMilliseconds)}`;
    case "asio": {
      const a = o.asio;
      const driver = a ? `${a.driverName}（版 ${a.driverVersion}）` : "";
      const type = a ? `・ドライバーの形式 ${a.sampleType}` : "";
      const aligned =
        o.layout === "perChannelRightAligned" ? "（32bit の枠の下位に右詰めで入れて送ります。値は変わりません）" : "";
      return `ASIO ${driver}${type}${aligned}・バッファー ${periodText(o.periodFrames, o.periodMilliseconds)}`;
    }
    case "dummy":
      return `ダミー出力（ファイルに書き出すだけ）・1 回に ${periodText(o.periodFrames, o.periodMilliseconds)}`;
  }
}

// ASIO: the driver's rate was switched for this track.
export function rateChangeText(s: PlaybackStatus): string {
  const a = s.path?.output.asio;
  if (!a || !a.rateChanged || !s.outputFormat) return "";
  return `この曲に合わせて、ドライバーのレートを ${rateText(a.rateBefore)} → ${rateText(s.outputFormat.sampleRate)} に切り替えました`;
}

// Windows endpoint volume (WASAPI). Empty when the output has no volume control.
export function volumeText(path: SignalPath): string {
  const v = path.output.endpointVolume;
  if (!v) return "";
  const percent = Math.round(v.scalar * 100);
  if (!v.hardware) {
    return `${percent}%${v.muted ? "・ミュート" : ""}（このデバイスはソフトウェア音量なので、排他モードではかかりません）`;
  }
  if (v.muted) return "ミュート（このデバイスは音量をデバイスの中でかけるため、音が出ません）";
  if (percent >= 100) return "100%（このデバイスは音量をデバイスの中でかけます。今は最大です）";
  return (
    `${percent}%：このデバイスは音量をデバイスの中でかけるため、DAC に届いたあとで信号が変わります。` +
    "原音のまま出すには 100% にしてください（アプリからドライバーまでは原音のままです）"
  );
}

function durationText(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const sec = Math.floor(seconds % 60);
  return m > 0 ? `${m} 分 ${sec} 秒` : `${sec} 秒`;
}

// The self-check of the direct path (task 4-3), with how far it reaches.
export function selfCheckText(s: PlaybackStatus): string {
  if (!s.path) return "—";
  if (s.path.processing) return "処理経路では行いません（値を変える経路のため。原音経路で、渡した値と読み出した値を比べます）";
  const d = s.delivery;
  if (!d) return "—";
  if (d.mismatchedFrames > 0)
    return (
      `ドライバーに渡した値が、ファイルから読み出した値と ${d.mismatchedFrames} フレームで一致しませんでした` +
      "（アプリの不具合の可能性があります。この出力を開いてから）"
    );
  if (d.matchedFrames === 0) return "確認中（最初のデータを渡すと結果が出ます）";
  const rate = s.outputFormat?.sampleRate ?? 0;
  const amount = rate > 0 ? `${durationText(d.matchedFrames / rate)}分` : `${d.matchedFrames} フレーム`;
  return (
    `ドライバーに渡した値が、ファイルから読み出した値とすべて一致（この出力を開いてから ${amount}）。` +
    "確かめられるのはアプリからドライバーまでです。ドライバーの中、USB などの転送、DAC の中は確かめられません"
  );
}

// Checksum check of the current track (FLAC stores the MD5 of its samples).
export function integrityText(s: PlaybackStatus): string {
  const c = s.integrity;
  switch (c.state) {
    case "passed":
      return "MD5 一致（デコードした音声データが、ファイルに記録された値と同じです）";
    case "pending":
      return "照合中（ファイルを最後まで読むと結果が出ます）";
    case "skipped":
      return "照合できません（途中にシークしたため。最初から再生し直すと照合します）";
    case "failed":
      return c.decodeErrors > 0
        ? `デコーダーがファイルの中の ${c.decodeErrors} か所でデータの誤りを見つけました。ファイルが壊れている可能性があります`
        : "MD5 が一致しません。ファイルが壊れている可能性があります";
    case "notAvailable":
      return "このファイルには照合用の値（MD5）が記録されていません（WAV・AIFF・ALAC・MP3・AAC・DSD と、MD5 のない FLAC）";
  }
}

export function integrityFailuresText(s: PlaybackStatus): string {
  if (s.integrityFailures.length === 0) return "";
  return `${s.integrityFailures.map((i) => i + 1).join("、")} 曲目は MD5 が一致しませんでした（ファイルが壊れている可能性があります）`;
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
        return fmt?.sampleType === "int"
          ? "このデバイスは整数の形式に対応していません（浮動小数点だけ）。原音経路では変換しないため、この曲は再生できません。"
          : "このデバイスはこの形式（整数 / 浮動小数点）に対応していません。";
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

  if (e.kind === "dopNotAllowed")
    return (
      `${s.queueIndex + 1} 曲目は DSD です。DSD を DoP で送る設定がオフなので再生しません。` +
      "DoP に対応していない DAC では大きなノイズになるため、対応している DAC のときだけオンにしてください" +
      "（設定画面ができるまでは、起動時に --allow-dop を付けます）。"
    );

  return `エラー: ${e.detail}`;
}
