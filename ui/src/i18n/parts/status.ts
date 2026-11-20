// The page's texts, part "status" (task 11-2): merged into the tables by
// ../ja.ts and ../en.ts. Keys start with "status." so the parts never clash.
//
// English: written in task 11-4, with the glossary (docs/i18n-glossary.md).

export const ja = {
  // The player's state (messages.ts stateLabel).
  "status.state.empty": "ファイルを開いてください",
  "status.state.stopped": "停止中",
  "status.state.playing": "再生中",
  "status.state.ended": "最後まで再生しました",
  "status.state.error": "再生できません",

  // A format's bits ("24bit 浮動小数点", "24bit（32bit コンテナ）").
  "status.format.bits": "{bits}bit",
  "status.format.float": "{bits}bit 浮動小数点",
  // The top bar's short tags of the format ("24 bit", "32bit float").
  "status.tag.bits": "{bits} bit",
  "status.tag.float": "32bit float",
  "status.format.padded": "{valid}bit（{container}bit コンテナ）",
  // A format's line: the rate, the bits, the channels. After a closing bracket, no space.
  "status.format.line": "{rate} / {bits} / {channels}ch",
  "status.format.linePadded": "{rate} / {bits}/ {channels}ch",
  "status.queue.position": "{index} / {count} 曲目",

  // Between two tracks.
  "status.transition.gapless": "{from} → {to} 曲目：ギャップレス（形式が同じなので出力を開き直さずにつなげました）",
  "status.transition.processingChanged":
    "{track} 曲目：処理経路と原音経路の切り替え（音量などが変わったため、今の位置で出力を開き直しました。約 {ms} ms）",
  "status.transition.format":
    "{from} → {to} 曲目：形式の切り替え {fromFormat} → {toFormat}（出力を開き直しました。前の曲のデータが途切れてから次の曲を送り始めるまで約 {ms} ms）",

  // The input line.
  "status.dsd.rate48": "DSD{multiple}（48 kHz 系）",
  "status.input.container": "{format}（{container}）",
  "status.input.dsd": "{dsd}（{mhz} MHz）/ {channels}ch（{container}）",

  // The processing stages, by name, in the status lines and the verdict's list.
  "status.stage.volume": "デジタル音量",
  "status.stage.eq": "パラメトリック EQ",
  "status.stage.tone": "トーン",
  "status.stage.tube": "真空管",
  "status.stage.space": "空間",
  "status.stage.crossfeed": "クロスフィード",
  "status.stage.ispSafe": "ISP セーフ",
  "status.stage.limiter": "保険のリミッター",
  "status.stage.resampler": "リサンプラー",
  "status.stage.comparison": "聴き比べ",
  "status.stage.dither": "TPDF ディザ",
  "status.stage.quantize": "浮動小数点の曲を整数に変換",
  "status.stage.detail": "{name}（{detail}）",
  "status.stage.preamp": "{name}（プリアンプ {db}）",

  // The BIT-PERFECT line.
  "status.bitPerfect.yes": "BIT-PERFECT（アプリからドライバーまで値をそのまま）",
  "status.bitPerfect.yesNotes": "BIT-PERFECT（アプリからドライバーまで値をそのまま。{notes}）",
  "status.bitPerfect.no": "BIT-PERFECT ではありません：{reasons}",
  "status.bitPerfect.deviceVolume":
    "{text}。デバイス側の音量 {volume}：DAC に届いたあとで信号が変わります（この判定はドライバーまで）",
  "status.bitPerfect.deviceMuted": "ミュート",
  "status.bitPerfect.dop": "DSD を DoP でそのまま",
  "status.bitPerfect.repacked": "コンテナの詰め替えあり、値は同じ",
  "status.bitPerfect.floatSource": "非可逆の曲をデコードした値のまま",
  "status.bitPerfect.dummyOutput": "ダミー出力（ファイル）",
  "status.bitPerfect.comparison": "聴き比べ中（原音側も値は同じだが処理経路を通る）",
  "status.bitPerfect.mismatch": "自己確認で不一致（{frames} フレーム）",

  // The processing line.
  "status.processing.dop": "なし（DoP の経路では処理をかけられません）",
  "status.processing.direct": "なし（原音経路：音量・変換・ディザをかけません）",
  "status.processing.toInteger": "浮動小数点の曲を整数にする（このデバイスは浮動小数点を受け付けないため）",
  "status.processing.on": "あり（64bit 浮動小数点の処理経路：{stages}）",
  "status.dop.notice":
    "DSD を DoP（PCM {rate} / 24bit に包む形）で送っています。DoP に対応していない DAC では、大きなノイズになります。DAC の表示が DSD になっているか確かめてください。",

  // How the output layer delivers the data.
  "status.outputMode.period": "{frames} フレーム（{ms} ms）",
  "status.outputMode.wasapi": "排他 WASAPI・1 回に {period}",
  "status.outputMode.wasapiEvent": "排他 WASAPI（イベント駆動）・1 回に {period}",
  "status.outputMode.asio": "ASIO {driver}{type}{aligned}・バッファー {period}",
  "status.outputMode.asioDriver": "{name}（版 {version}）",
  "status.outputMode.asioType": "・ドライバーの形式 {type}",
  "status.outputMode.asioRightAligned": "（32bit の枠の下位に右詰めで入れて送ります。値は変わりません）",
  "status.outputMode.dummy": "ダミー出力（ファイルに書き出すだけ）・1 回に {period}",
  "status.rateChange.asio": "この曲に合わせて、ドライバーのレートを {from} → {to} に切り替えました",

  // Windows' volume for the device.
  "status.volume.software": "{percent}%（このデバイスはソフトウェア音量なので、排他モードではかかりません）",
  "status.volume.softwareMuted": "{percent}%・ミュート（このデバイスはソフトウェア音量なので、排他モードではかかりません）",
  "status.volume.muted": "ミュート（このデバイスは音量をデバイスの中でかけるため、音が出ません）",
  "status.volume.max": "100%（このデバイスは音量をデバイスの中でかけます。今は最大です）",
  "status.volume.lowered":
    "{percent}%：このデバイスは音量をデバイスの中でかけるため、DAC に届いたあとで信号が変わります。原音のまま出すには 100% にしてください（アプリからドライバーまでは原音のままです）",

  // Lengths of time, and how much was checked or measured ("3 分 5 秒分").
  "status.duration.minutes": "{minutes} 分 {seconds} 秒",
  "status.duration.seconds": "{seconds} 秒",
  "status.amount.time": "{duration}分",
  "status.amount.frames": "{frames} フレーム分",

  // The self-check of the direct path.
  "status.selfCheck.processing": "処理経路では行いません（値を変える経路のため。原音経路で、渡した値と読み出した値を比べます）",
  "status.selfCheck.mismatch":
    "この出力を開いてから、ドライバーに渡した値のうち {frames} フレーム分（1 フレーム＝全チャンネル 1 組の値）が、ファイルから読み出した値と違っていました。アプリの不具合と考えられます",
  "status.selfCheck.checking": "確認中（最初のデータを渡すと結果が出ます）",
  "status.selfCheck.matched":
    "この出力を開いてから {amount}、ドライバーに渡した値はすべて、ファイルから読み出した値と一致しました。確かめられるのはアプリからドライバーまでです。ドライバーの中、USB などの転送、DAC の中は確かめられません",

  // The meters.
  "status.meter.notPcm": "DoP の経路では測りません（DSD のデータで、PCM の音の値ではないため）",
  "status.meter.over": "{values} dBTP（0 dBTP を超えています）",
  "status.meter.loudness": "瞬時 {momentary} LUFS ／ 短時間 {shortTerm} LUFS",
  "status.meter.span": "（この曲の {duration}分）",
  "status.meter.integratedWaiting": "測定中（-70 LUFS より大きい音が来ると出ます）{span}",
  "status.meter.note": "出力に渡す値を測っています（処理経路では処理の後の値）。測るだけで、音は変えません",
  "status.meter.noteDropped":
    "出力に渡す値を測っています（処理経路では処理の後の値）。測るだけで、音は変えません。メーターの計算が追いつかず、{amount}を測れませんでした（音には影響しません）",

  // The checksum of the track.
  "status.integrity.passed": "MD5 一致（デコードした音声データが、ファイルに記録された値と同じです）",
  "status.integrity.pending": "照合中（ファイルを最後まで読むと結果が出ます）",
  "status.integrity.skipped": "照合できません（途中にシークしたため。最初から再生し直すと照合します）",
  "status.integrity.decodeErrors":
    "デコーダーがファイルの中の {count} か所でデータの誤りを見つけました。ファイルが壊れている可能性があります",
  "status.integrity.failed": "MD5 が一致しません。ファイルが壊れている可能性があります",
  "status.integrity.notAvailable":
    "このファイルには照合用の値（MD5）が記録されていません（WAV・AIFF・ALAC・MP3・AAC・DSD と、MD5 のない FLAC）",
  "status.integrity.failures": "{tracks} 曲目は MD5 が一致しませんでした（ファイルが壊れている可能性があります）",

  // Dropouts.
  "status.dropout.count": "{count} 回",
  "status.dropout.detail": "{count} 回（{parts}）",
  "status.dropout.late": "デバイスへの受け渡しの遅れ {count}",
  "status.dropout.read": "ファイルの読み込みの遅れ {count}",
  "status.dropout.timeout": "デバイスの応答なし {count}",

  // The playback thread's priority.
  "status.priority.realtime": "MMCSS（Pro Audio）で優先度を上げています（音切れを防ぐため）",
  "status.priority.normal": "通常の優先度（MMCSS が使えませんでした）",

  // The repacking of the samples.
  "status.repack.none": "なし（ファイルのバイト列をそのまま送ります）",
  "status.repack.wider": "{from}bit → {to}bit コンテナ（有効 {valid}bit、下位は 0。値は変わりません）",
  "status.repack.widerUnknown": "コンテナの詰め替え（値は変わりません）",
  "status.repack.zeroPadded": "{from}bit → {to}bit（下位 {pad}bit は 0。値は変わりません）",
  "status.repack.zeroPaddedUnknown": "ゼロ詰め（値は変わりません）",

  // Why a track does not play. unknownRate stands for the rate when the
  // format is not known ("このデバイスは排他モードで この に…").
  "status.error.unknownRate": "この",
  "status.error.sampleRate":
    "このデバイスは排他モードで {rate} に対応していません。レートの変換はしません。{accepted}デバイスの設定（オーディオインターフェースのコントロールパネルなど）でレートを {rate} にすると再生できる場合があります。",
  "status.error.acceptedRates": "このデバイスが今受け付けるのは {rates} です。",
  "status.error.bitDepth": "このデバイスは {rate} には対応していますが、この形式（{format}）には対応していません。",
  "status.error.channels": "このデバイスは {channels} チャンネルに対応していません。",
  "status.error.floatOnly":
    "このデバイスは整数の形式に対応していません（浮動小数点だけ）。原音経路では変換しないため、この曲は再生できません。",
  "status.error.sampleType": "このデバイスはこの形式（整数 / 浮動小数点）に対応していません。",
  "status.error.format": "このデバイスはこの形式に対応していません。",
  "status.error.deviceNotFound": "出力デバイスが見つかりません。つなぎ直すか、設定の「出力」で選び直してください。",
  "status.error.deviceInUse": "他のアプリがこのデバイスを排他モードで使っています。そのアプリを止めてから試してください。",
  "status.error.exclusiveModeNotAllowed":
    "このデバイスは排他モードが許可されていません。サウンドの設定 → このデバイスのプロパティ → 詳細 で「アプリケーションによりこのデバイスを排他的に制御できるようにする」をオンにしてください。",
  "status.error.formatRejected": "デバイスがこの形式を受け付けませんでした。",
  "status.error.deviceInvalidated": "デバイスが外されたか、設定が変わりました。",
  "status.error.timeout": "デバイスがデータを要求しなくなりました。",
  "status.error.device": "デバイスのエラーです（{detail}）。",
  "status.error.load": "{track} 曲目のファイルを開けません: {detail}",
  "status.error.dopNotAllowed":
    "{track} 曲目は DSD です。DSD を DoP で送る設定がオフなので再生しません。DoP に対応していない DAC では大きな雑音になるため、対応している DAC のときだけ、設定の「出力」でオンにしてください。",
  "status.error.other": "エラー: {detail}",

  // The output field's tags (the top bar).
  "status.tag.comparison": "聴き比べ中",
  "status.tag.mismatch": "自己確認で不一致",
  "status.tag.resample": "レート変換",
  "status.tag.processing": "処理あり",

  // The output check's diagram (outputCheck.ts pathModel).
  "status.path.file": "ファイル",
  "status.path.decoder": "デコード",
  "status.path.processing": "処理",
  "status.path.output": "出力",
  "status.path.decoderOwn": "自前",
  "status.path.muted": "ミュート",
  "status.path.md5Passed": "MD5 一致",
  "status.path.md5Failed": "MD5 不一致",
  "status.path.md5Pending": "MD5 照合中",
  "status.path.dsdRate": "{dsd}（{mhz} MHz）",
  "status.path.format": "{bits} / {rate}",
  "status.path.formatPadded": "{bits}/ {rate}",
  "status.path.tubeValue": "{type}・DRIVE {drive}",
  "status.path.dopWrap":"DoP に包む（値はそのまま）",
  "status.path.integer": "整数のまま",
  "status.path.preamp": "プリアンプ {db}",
  "status.path.byAlbum": "アルバムの指定で選択",
  "status.path.byGenre": "ジャンル（{genre}）で選択",
  "status.path.limiterEvents": "{count} 回・最大 {db}",
  "status.path.limiterZero": "0 回",
  "status.path.badgeDop": "DoP：処理をかけられません",
  "status.path.badgeFloat": "64bit 浮動小数点",
  "status.path.badgeBypass": "BYPASS（原音経路）",
  "status.path.toInteger": "浮動小数点の曲を整数にする（このデバイスは浮動小数点を受け付けないため）",
  "status.path.wasapi": "WASAPI 排他",
  "status.path.wasapiEvent": "WASAPI 排他（イベント駆動）",
  "status.path.asioDriver": "ASIO（{name}）",
  "status.path.dummy": "ダミー出力（ファイル）",
  "status.path.dopFrame": "DoP（PCM {rate} の枠）",
  "status.path.repacked": "詰め替え（値は同じ）",
  "status.path.deviceVolume": "機器の音量 {volume}",
  // The rows of the processing box.
  "status.pathRow.tone": "トーン",
  "status.pathRow.tube": "真空管",
  "status.pathRow.space": "空間",
  "status.pathRow.crossfeed": "クロスフィード",
  "status.pathRow.volume": "デジタル音量",
  "status.pathRow.ispSafe": "ISP セーフ",
  "status.pathRow.limiter": "保険のリミッター",
  "status.pathRow.resampler": "リサンプラー",
  "status.pathRow.dither": "ディザ",
  "status.pathRow.comparison": "聴き比べ",

  // The output check's BIT-PERFECT judgement (outputCheck.ts verdictModel).
  "status.verdict.notBitPerfect": "BIT-PERFECT ではありません",
  "status.verdict.volume": "音量 100%",
  "status.verdict.volumeDop": "DoP には音量をかけません",
  "status.verdict.processing": "処理なし",
  "status.verdict.direct": "原音経路（処理のコードを通りません）",
  "status.verdict.processingPath": "処理経路",
  "status.verdict.format": "形式がファイルと一致",
  "status.verdict.formatProcessed": "処理経路の形式（{format}）で送ります",
  "status.verdict.formatProcessedNone": "処理経路の形式で送ります",
  "status.verdict.formatDop": "DoP（DSD のビットはそのまま）",
  "status.verdict.formatWider": "{bits}bit コンテナに詰め替え（値は同じ）",
  "status.verdict.formatZeroPadded": "下位を 0 で埋めて {bits}bit に（値は同じ）",
  "status.verdict.formatFloat": "デコードした浮動小数点の値のまま",
  "status.verdict.formatSame": "ファイルの形式のまま",
  "status.verdict.exclusive": "排他モード",
  "status.verdict.exclusiveWasapi": "WASAPI 排他（ほかのアプリの音は混ざりません）",
  "status.verdict.exclusiveAsio": "ASIO（Windows のミキサーを通りません）",
  "status.verdict.exclusiveDummy": "ダミー出力（ファイルに書き出すだけ）",
  "status.verdict.windowsVolume": "Windows の音量",
  "status.verdict.windowsVolumeSoftware": "ソフトウェアの音量なので、排他モードではかかりません",
  "status.verdict.windowsVolumeMuted": "ミュート（DAC の中でかかります）",
  "status.verdict.windowsVolumeLowered": "{percent}%（DAC に届いたあとで信号が変わります。この判定はドライバーまで）",
  "status.verdict.selfCheck": "自己確認",
  "status.verdict.selfCheckProcessing": "処理経路では行いません（値を変える経路のため）",
  "status.verdict.selfCheckMismatch": "{frames} フレームが、ファイルの値と違っていました",
  "status.verdict.selfCheckMatched": "{amount}、ドライバーに渡した値がファイルの値と一致",
  "status.verdict.selfCheckChecking": "確認中（データを渡すと結果が出ます）",
  "status.verdict.scope":
    "判定と自己確認の範囲は、アプリからドライバーまでです。ドライバーの中、USB などの転送、DAC の中は確かめられません。",
  "status.isp.warning":
    "この曲の True Peak は {truePeak} で、0 dBTP を超えています（取り込み時に測った値）。DAC がオーバーサンプリングするとき、サンプルの間のピークがクリップすることがあります。ISP セーフをオンにすると、−0.5 dBTP 以下になるように音量を線形に下げます（処理経路になるので、BIT-PERFECT ではなくなります）。",

  // The output check dialog (outputView.ts).
  "status.view.verdictEmpty": "再生を始めると、出力を開いたときの経路から判定します。",
  "status.view.max": "最大 {value}",
  // Where a stage's value may break onto the next line (between its parts).
  "status.view.partSeparator": "・",

  // The output rate (outputRate.ts).
  "status.rateMode.track": "曲のまま",
  "status.rateMode.fixed": "固定",
  "status.rateMode.atleast": "下限",
  "status.rateMode.multiple": "整数倍",
  "status.rateNote.fixed": "どの曲も {rate} にして出します（それより高い曲は下げます）。",
  "status.rateNote.atleast": "{rate} 未満の曲を {rate} に上げます。{rate} 以上の曲はそのままです。",
  "status.rateNote.multiple":
    "曲のレートの {multiple} 倍で出します（44.1 → {from44} kHz、48 → {from48} kHz）。DAC が受け付けないときは、受け付けるいちばん大きい倍数です。",
  "status.rateNote.track": "曲のレートのまま出します（DAC が受け付けないレートの曲だけ、受け付けるレートへ変換します）。",
  "status.rateSummary.fixed": "出力レート（固定 {rate}）",
  "status.rateSummary.atleast": "出力レート（{rate} 以上）",
  "status.rateSummary.multiple": "出力レート（曲の {multiple} 倍）",
  "status.resample.setting": "{from} → {to}（設定）",
  "status.resample.unsupported": "{from} → {to}（DAC が {from} を受け付けないため）",

  // The tube's warm-up and units (tubeCharacter.ts), and what each type is.
  "status.tube.unitTube": "エミッション {emission}・μ {mu}",
  "status.tube.unitPair": "{first} と {second}",
  "status.tube.unitChannels": "左：{left}、右：{right}",
  "status.tube.unit": "球 No.{number}",
  "status.tube.warm": "暖まった状態（起動から {minutes} 分を過ぎました）",
  "status.tube.warmingUp": "ウォームアップ中：起動から {minutes} 分（暖まりきるまで、あと約 {left} 分）",
  "status.tube.warmShort": "暖まった状態",
  "status.tube.warmingUpShort": "ウォームアップ中（起動から {minutes} 分）",
  "status.tube.noteSe300b":
    "300B 1 本のシングルエンド（カソードバイアス）。主に 2 次の倍音を加えます（測定例：2 次 −37 dB、3 次 −53 dB）。",
  "status.tube.notePpel34":
    "EL34 2 本のプッシュプル（AB 級、固定バイアス）。偶数次が打ち消し合い、主に 3 次の倍音を加えます（測定例：3 次 −26 dB）。",
  "status.tube.notePpclassa": "EL34 2 本のプッシュプル（A 級）。同じ DRIVE なら倍音は少なめです（測定例：3 次 −40 dB）。",

  // The space (settings.ts).
  "status.space.text": "{room}{tag}・MIX {mix}%",
  "status.space.roomNumbers": "{size}・残響 {seconds} 秒",
  "status.space.reverb": "残響 {seconds} 秒",
  "status.space.hallPlace": "St Paul's Hall（英国、ハダースフィールド大学のコンサートホール）",
  "status.space.hallCredit":
    "St Paul's Hall の IR：MAIR（Microphone Array Impulse Response Library。Hyunkook Lee、Connor Millns、University of Huddersfield Applied Psychoacoustics Lab）、CC BY 4.0（https://creativecommons.org/licenses/by/4.0/）。ステージ正面中央の音源を AB（無指向性マイク 2 本）で録った 2 本を 1 本のステレオにまとめ、直接音の 1 ms 前から使っています。",
  "status.space.measuredTag": "（実測）",
  "status.space.userTag": "（自分の IR）",
  "status.space.lowCutNone": "なし",
  "status.space.builtInMeasured": "実測：{place}・{numbers}",
  "status.space.builtInSynth": "合成：{numbers}",
  "status.space.fallbackMeasured": "内蔵（実測）",
  "status.space.fallbackSynth": "内蔵（合成）",
  "status.space.noFile": "自分のファイルはまだ選んでいません。{fallback}を使っています",
  "status.space.unreadable": "{name} を読めませんでした（{error}）。{fallback}を使っています",
  "status.space.unknownError": "理由不明",
  "status.space.mono": "モノラル",
  "status.space.stereo": "ステレオ",
  "status.space.firstTwo": "{channels}ch のうち最初の 2ch",
  "status.space.truncated": "5 秒で切りました",
  "status.space.userIr": "{name}（{seconds} 秒・{channels}・{rate} kHz）",
  "status.space.userIrExtra": "{name}（{seconds} 秒・{channels}・{rate} kHz・{extra}）",
  "status.space.extraSeparator": "・",

  // The crossfeed's levels.
  "status.crossfeed.numbers": "{hz} Hz・{db} dB",
  "status.crossfeed.default": "標準（{numbers}）",
  "status.crossfeed.cmoy": "やや弱め（{numbers}）",
  "status.crossfeed.jmeier": "弱め（{numbers}）",

  // The EQ's filter types.
  "status.filter.peak": "ピーク",
  "status.filter.lowShelf": "ローシェルフ",
  "status.filter.highShelf": "ハイシェルフ",
  "status.filter.lowPass": "ローパス",
  "status.filter.highPass": "ハイパス",
  "status.filter.bandPass": "バンドパス",
  "status.filter.notch": "ノッチ",

  // ISP セーフ and ReplayGain's choices.
  "status.isp.off": "オフ",
  "status.isp.measured": "計測した値で下げる",
  "status.isp.fixed": "固定で −3 dB",
  "status.replayGain.off": "オフ",
  "status.replayGain.track": "トラック（曲ごと）",
  "status.replayGain.album": "アルバム（アルバムの中の音量差を保つ）",

  // What the chosen mode does (settings.ts modeSummary).
  "status.mode.original":
    "原音モード：音量 100% なら何も処理しません。ReplayGain・EQ・トーン・真空管・空間・クロスフィードはかけません（設定は残ります）。",
  "status.mode.originalIsp":
    "原音モード：音量 100% なら、ISP セーフ（{isp}）のほかは何も処理しません。ReplayGain・EQ・トーン・真空管・空間・クロスフィードはかけません（設定は残ります）。",
  "status.mode.replayGainTrack": "ReplayGain（トラック）",
  "status.mode.replayGainAlbum": "ReplayGain（アルバム）",
  "status.mode.eq": "EQ（{bands} バンド）",
  "status.mode.tone": "トーン（{parts}）",
  "status.mode.tube": "真空管（{type}、{details}）",
  "status.mode.tubeAuto": "真空管（ジャンルで自動・対応表にないときは {type}、{details}）",
  "status.mode.warmUp": "ウォームアップの演出",
  "status.mode.space": "空間（{space}）",
  "status.mode.crossfeed": "クロスフィード（{numbers}）",
  "status.mode.ispSafe": "ISP セーフ（{isp}）",
  "status.mode.none": "音作りモード：かける処理がありません。音量 100% なら原音モードと同じ出力です。",
  "status.mode.processed": "音作りモード：{parts}を 64bit 浮動小数点でかけます。",

  // The output chosen in the settings.
  "status.output.asio": "ASIO：{name}",
  "status.output.wasapiDefault": "WASAPI 排他：Windows の既定のデバイス",
  "status.output.wasapi": "WASAPI 排他：{name}",
  "status.output.default": "Windows の既定のデバイス",
  "status.output.defaultNow": "Windows の既定のデバイス（今は {name}）",
  "status.output.missing": "{name}（今は見つかりません）",
} as const;

export const en: Record<keyof typeof ja, string> = {
  "status.state.empty": "Open a file",
  "status.state.stopped": "Stopped",
  "status.state.playing": "Playing",
  "status.state.ended": "Played to the end",
  "status.state.error": "Cannot play",

  "status.format.bits": "{bits}-bit",
  "status.format.float": "{bits}-bit float",
  "status.tag.bits": "{bits}-bit",
  "status.tag.float": "32-bit float",
  "status.format.padded": "{valid}-bit ({container}-bit container)",
  "status.format.line": "{rate} / {bits} / {channels}ch",
  "status.format.linePadded": "{rate} / {bits} / {channels}ch",
  "status.queue.position": "Track {index} of {count}",

  "status.transition.gapless": "Tracks {from} → {to}: gapless (same format, so the output was not reopened)",
  "status.transition.processingChanged":
    "Track {track}: switched between the processing path and the direct path (the volume or another setting changed, so the output was reopened at the current position; about {ms} ms)",
  "status.transition.format":
    "Tracks {from} → {to}: format change {fromFormat} → {toFormat} (the output was reopened; about {ms} ms from the end of the previous track's data to the start of the next track's)",

  "status.dsd.rate48": "DSD{multiple} (48 kHz family)",
  "status.input.container": "{format} ({container})",
  "status.input.dsd": "{dsd} ({mhz} MHz) / {channels}ch ({container})",

  "status.stage.volume": "Digital volume",
  "status.stage.eq": "Parametric EQ",
  "status.stage.tone": "Tone",
  "status.stage.tube": "Tube",
  "status.stage.space": "Space",
  "status.stage.crossfeed": "Crossfeed",
  "status.stage.ispSafe": "ISP-safe",
  "status.stage.limiter": "Safety limiter",
  "status.stage.resampler": "Resampler",
  "status.stage.comparison": "Blind comparison",
  "status.stage.dither": "TPDF dither",
  "status.stage.quantize": "Float track converted to integer",
  "status.stage.detail": "{name} ({detail})",
  "status.stage.preamp": "{name} (preamp {db})",

  "status.bitPerfect.yes": "BIT-PERFECT (values unchanged from the app to the driver)",
  "status.bitPerfect.yesNotes": "BIT-PERFECT (values unchanged from the app to the driver; {notes})",
  "status.bitPerfect.no": "Not BIT-PERFECT: {reasons}",
  "status.bitPerfect.deviceVolume":
    "{text}. Device volume {volume}: the signal changes after it reaches the DAC (this check covers up to the driver)",
  "status.bitPerfect.deviceMuted": "muted",
  "status.bitPerfect.dop": "DSD unchanged via DoP",
  "status.bitPerfect.repacked": "container repacked with the same values",
  "status.bitPerfect.floatSource": "values as decoded from a lossy track",
  "status.bitPerfect.dummyOutput": "dummy output (file)",
  "status.bitPerfect.comparison":
    "Blind comparison in progress (the original side keeps the same values but goes through the processing path)",
  "status.bitPerfect.mismatch": "Self-check mismatch ({frames} frames)",

  "status.processing.dop": "None (no processing can be applied on the DoP path)",
  "status.processing.direct": "None (direct path: no volume, conversion, or dither)",
  "status.processing.toInteger": "Float track converted to integer (this device does not accept float)",
  "status.processing.on": "On (64-bit float processing path: {stages})",
  "status.dop.notice":
    "Sending DSD as DoP (wrapped in PCM {rate} / 24-bit). On a DAC that does not support DoP, this comes out as loud noise. Check that the DAC's display shows DSD.",

  "status.outputMode.period": "{frames} frames ({ms} ms)",
  "status.outputMode.wasapi": "WASAPI exclusive, {period} per buffer",
  "status.outputMode.wasapiEvent": "WASAPI exclusive (event-driven), {period} per buffer",
  "status.outputMode.asio": "ASIO {driver}{type}{aligned}, buffer {period}",
  "status.outputMode.asioDriver": "{name} (version {version})",
  "status.outputMode.asioType": ", driver format {type}",
  "status.outputMode.asioRightAligned": " (sent right-aligned in the low bits of 32-bit words; values unchanged)",
  "status.outputMode.dummy": "Dummy output (writes to a file only), {period} per buffer",
  "status.rateChange.asio": "Switched the driver's rate {from} → {to} for this track",

  "status.volume.software": "{percent}% (this device uses software volume, which does not apply in exclusive mode)",
  "status.volume.softwareMuted": "{percent}%, muted (this device uses software volume, which does not apply in exclusive mode)",
  "status.volume.muted": "Muted (this device applies its volume inside the device, so there is no sound)",
  "status.volume.max": "100% (this device applies its volume inside the device; it is at maximum now)",
  "status.volume.lowered":
    "{percent}%: this device applies its volume inside the device, so the signal changes after it reaches the DAC. Set it to 100% to output the values unchanged (from the app to the driver, the values are unchanged)",

  "status.duration.minutes": "{minutes} min {seconds} s",
  "status.duration.seconds": "{seconds} s",
  "status.amount.time": "{duration}",
  "status.amount.frames": "{frames} frames",

  "status.selfCheck.processing":
    "Not done on the processing path (it changes the values). On the direct path, the values passed to the driver are compared with the values read from the file",
  "status.selfCheck.mismatch":
    "Since this output opened, {frames} frames of the values passed to the driver (1 frame = one value for every channel) differed from the values read from the file. This points to a bug in the app",
  "status.selfCheck.checking": "Checking (the result appears once the first data is passed)",
  "status.selfCheck.matched":
    "Since this output opened, all values passed to the driver for {amount} matched the values read from the file. This checks only from the app to the driver; it cannot check inside the driver, the transfer (USB or other), or inside the DAC",

  "status.meter.notPcm": "Not measured on the DoP path (it carries DSD data, not PCM sample values)",
  "status.meter.over": "{values} dBTP (above 0 dBTP)",
  "status.meter.loudness": "Momentary {momentary} LUFS / Short-term {shortTerm} LUFS",
  "status.meter.span": " (over {duration} of this track)",
  "status.meter.integratedWaiting": "Measuring (shown once the signal is louder than -70 LUFS){span}",
  "status.meter.note":
    "Measures the values passed to the output (on the processing path, the values after processing). It only measures and does not change the sound",
  "status.meter.noteDropped":
    "Measures the values passed to the output (on the processing path, the values after processing). It only measures and does not change the sound. The meter calculation fell behind, and {amount} could not be measured (the sound is not affected)",

  "status.integrity.passed": "MD5 match (the decoded audio data matches the value stored in the file)",
  "status.integrity.pending": "Checking (the result appears once the whole file is read)",
  "status.integrity.skipped": "Cannot check (there was a seek; play from the start to check)",
  "status.integrity.decodeErrors": "The decoder found data errors at {count} places in the file. The file may be damaged",
  "status.integrity.failed": "MD5 does not match. The file may be damaged",
  "status.integrity.notAvailable":
    "This file stores no MD5 to check against (WAV, AIFF, ALAC, MP3, AAC, DSD, and FLAC without an MD5)",
  "status.integrity.failures": "Tracks with an MD5 mismatch: {tracks} (the file may be damaged)",

  "status.dropout.count": "{count}",
  "status.dropout.detail": "{count} ({parts})",
  "status.dropout.late": "late device delivery {count}",
  "status.dropout.read": "late file reads {count}",
  "status.dropout.timeout": "device timeouts {count}",

  "status.priority.realtime": "Priority raised with MMCSS (Pro Audio) to prevent dropouts",
  "status.priority.normal": "Normal priority (MMCSS was not available)",

  "status.repack.none": "None (the file's bytes are sent as they are)",
  "status.repack.wider": "{from}-bit → {to}-bit container ({valid}-bit valid, low bits 0; values unchanged)",
  "status.repack.widerUnknown": "Container repacking (values unchanged)",
  "status.repack.zeroPadded": "{from}-bit → {to}-bit (low {pad} bits are 0; values unchanged)",
  "status.repack.zeroPaddedUnknown": "Zero padding (values unchanged)",

  "status.error.unknownRate": "this rate",
  "status.error.sampleRate":
    "This device does not support {rate} in exclusive mode. The rate is not converted. {accepted}Setting the device's rate to {rate} (in the audio interface's control panel, for example) may let it play.",
  "status.error.acceptedRates": "It currently accepts {rates}. ",
  "status.error.bitDepth": "This device supports {rate} but not this format ({format}).",
  "status.error.channels": "This device does not support {channels} channels.",
  "status.error.floatOnly":
    "This device does not support integer formats (float only). The direct path does not convert, so this track cannot play.",
  "status.error.sampleType": "This device does not support this sample type (integer / float).",
  "status.error.format": "This device does not support this format.",
  "status.error.deviceNotFound": "Output device not found. Reconnect it, or choose it again in the Output tab of the settings.",
  "status.error.deviceInUse": "Another app is using this device in exclusive mode. Stop that app and try again.",
  "status.error.exclusiveModeNotAllowed":
    "This device does not allow exclusive mode. In Sound settings → this device's Properties → Advanced, turn on \"Allow applications to take exclusive control of this device\".",
  "status.error.formatRejected": "The device did not accept this format.",
  "status.error.deviceInvalidated": "The device was removed or its settings changed.",
  "status.error.timeout": "The device stopped requesting data.",
  "status.error.device": "Device error ({detail}).",
  "status.error.load": "Cannot open the file of track {track}: {detail}",
  "status.error.dopNotAllowed":
    "Track {track} is DSD. Sending DSD as DoP is turned off, so it is not played. On a DAC that does not support DoP this comes out as loud noise; turn it on in the Output tab of the settings only for a DAC that supports DoP.",
  "status.error.other": "Error: {detail}",

  "status.tag.comparison": "Comparing (ABX)",
  "status.tag.mismatch": "Self-check mismatch",
  "status.tag.resample": "Rate conversion",
  "status.tag.processing": "Processing",

  "status.path.file": "File",
  "status.path.decoder": "Decoder",
  "status.path.processing": "Processing",
  "status.path.output": "Output",
  "status.path.decoderOwn": "Built-in",
  "status.path.muted": "Muted",
  "status.path.md5Passed": "MD5 match",
  "status.path.md5Failed": "MD5 mismatch",
  "status.path.md5Pending": "Checking MD5",
  "status.path.dsdRate": "{dsd} ({mhz} MHz)",
  "status.path.format": "{bits} / {rate}",
  "status.path.formatPadded": "{bits} / {rate}",
  "status.path.tubeValue": "{type} · DRIVE {drive}",
  "status.path.dopWrap": "Wrapped in DoP (values unchanged)",
  "status.path.integer": "Integer, unchanged",
  "status.path.preamp": "Preamp {db}",
  "status.path.byAlbum": "Chosen by per-album choice",
  "status.path.byGenre": "Chosen by genre ({genre})",
  "status.path.limiterEvents": "{count}× · max {db}",
  "status.path.limiterZero": "0×",
  "status.path.badgeDop": "DoP: no processing possible",
  "status.path.badgeFloat": "64-bit float",
  "status.path.badgeBypass": "BYPASS (direct path)",
  "status.path.toInteger": "Float track converted to integer (this device does not accept float)",
  "status.path.wasapi": "WASAPI exclusive",
  "status.path.wasapiEvent": "WASAPI exclusive (event-driven)",
  "status.path.asioDriver": "ASIO ({name})",
  "status.path.dummy": "Dummy output (file)",
  "status.path.dopFrame": "DoP (carried as PCM {rate})",
  "status.path.repacked": "Repacked (same values)",
  "status.path.deviceVolume": "Device volume {volume}",
  "status.pathRow.tone": "Tone",
  "status.pathRow.tube": "Tube",
  "status.pathRow.space": "Space",
  "status.pathRow.crossfeed": "Crossfeed",
  "status.pathRow.volume": "Digital volume",
  "status.pathRow.ispSafe": "ISP-safe",
  "status.pathRow.limiter": "Safety limiter",
  "status.pathRow.resampler": "Resampler",
  "status.pathRow.dither": "Dither",
  "status.pathRow.comparison": "Blind comparison",

  "status.verdict.notBitPerfect": "Not BIT-PERFECT",
  "status.verdict.volume": "Volume 100%",
  "status.verdict.volumeDop": "No volume is applied to DoP",
  "status.verdict.processing": "No processing",
  "status.verdict.direct": "Direct path (does not go through the processing code)",
  "status.verdict.processingPath": "Processing path",
  "status.verdict.format": "Format matches the file",
  "status.verdict.formatProcessed": "Sent in the processing path's format ({format})",
  "status.verdict.formatProcessedNone": "Sent in the processing path's format",
  "status.verdict.formatDop": "DoP (DSD bits unchanged)",
  "status.verdict.formatWider": "Repacked into a {bits}-bit container (same values)",
  "status.verdict.formatZeroPadded": "Padded to {bits}-bit with zeros in the low bits (same values)",
  "status.verdict.formatFloat": "Decoded float values, unchanged",
  "status.verdict.formatSame": "Same as the file's format",
  "status.verdict.exclusive": "Exclusive mode",
  "status.verdict.exclusiveWasapi": "WASAPI exclusive (no other app's sound is mixed in)",
  "status.verdict.exclusiveAsio": "ASIO (does not go through the Windows mixer)",
  "status.verdict.exclusiveDummy": "Dummy output (writes to a file only)",
  "status.verdict.windowsVolume": "Windows volume",
  "status.verdict.windowsVolumeSoftware": "Software volume, so it does not apply in exclusive mode",
  "status.verdict.windowsVolumeMuted": "Muted (applied inside the DAC)",
  "status.verdict.windowsVolumeLowered":
    "{percent}% (the signal changes after it reaches the DAC; this check covers up to the driver)",
  "status.verdict.selfCheck": "Self-check",
  "status.verdict.selfCheckProcessing": "Not done on the processing path (it changes the values)",
  "status.verdict.selfCheckMismatch": "{frames} frames differed from the file's values",
  "status.verdict.selfCheckMatched": "Values passed to the driver matched the file for {amount}",
  "status.verdict.selfCheckChecking": "Checking (the result appears once data is passed)",
  "status.verdict.scope":
    "The check and the self-check cover the app up to the driver. They cannot check inside the driver, the transfer (USB or other), or inside the DAC.",
  "status.isp.warning":
    "This track's True Peak is {truePeak}, above 0 dBTP (measured on import). When the DAC oversamples, peaks between samples can clip. Turning on ISP-safe lowers the volume with a linear gain so the output stays at or below −0.5 dBTP (this uses the processing path, so the output is no longer BIT-PERFECT).",

  "status.view.verdictEmpty": "Once playback starts, the check is made from the path used when the output opened.",
  "status.view.max": "Max {value}",
  "status.view.partSeparator": " · ",

  "status.rateMode.track": "As the track",
  "status.rateMode.fixed": "Fixed",
  "status.rateMode.atleast": "At least",
  "status.rateMode.multiple": "Integer multiple",
  "status.rateNote.fixed": "Every track is output at {rate} (tracks at a higher rate are converted down).",
  "status.rateNote.atleast": "Tracks below {rate} are converted up to {rate}. Tracks at {rate} or above stay as they are.",
  "status.rateNote.multiple":
    "Outputs at {multiple}× the track's rate (44.1 → {from44} kHz, 48 → {from48} kHz). If the DAC does not accept it, the largest multiple it accepts is used.",
  "status.rateNote.track":
    "Outputs at the track's rate (only tracks at a rate the DAC does not accept are converted to a rate it accepts).",
  "status.rateSummary.fixed": "output rate (fixed {rate})",
  "status.rateSummary.atleast": "output rate ({rate} or higher)",
  "status.rateSummary.multiple": "output rate ({multiple}× the track)",
  "status.resample.setting": "{from} → {to} (setting)",
  "status.resample.unsupported": "{from} → {to} (the DAC does not accept {from})",

  "status.tube.unitTube": "emission {emission}, μ {mu}",
  "status.tube.unitPair": "{first} and {second}",
  "status.tube.unitChannels": "Left: {left}; right: {right}",
  "status.tube.unit": "Tube No. {number}",
  "status.tube.warm": "Warmed up (more than {minutes} min since launch)",
  "status.tube.warmingUp": "Warming up: {minutes} min since launch (about {left} min until fully warm)",
  "status.tube.warmShort": "Warmed up",
  "status.tube.warmingUpShort": "Warming up ({minutes} min since launch)",
  "status.tube.noteSe300b":
    "One 300B, single-ended (cathode bias). Adds mainly the 2nd harmonic (measured example: 2nd −37 dB, 3rd −53 dB).",
  "status.tube.notePpel34":
    "Two EL34s, push-pull (class AB, fixed bias). Even orders cancel; adds mainly the 3rd harmonic (measured example: 3rd −26 dB).",
  "status.tube.notePpclassa": "Two EL34s, push-pull (class A). At the same DRIVE, lower harmonics (measured example: 3rd −40 dB).",

  "status.space.text": "{room}{tag} · MIX {mix}%",
  "status.space.roomNumbers": "{size}, reverb {seconds} s",
  "status.space.reverb": "reverb {seconds} s",
  "status.space.hallPlace": "St Paul's Hall (a concert hall at the University of Huddersfield, UK)",
  "status.space.hallCredit":
    "St Paul's Hall IR: MAIR (Microphone Array Impulse Response Library; Hyunkook Lee, Connor Millns, University of Huddersfield Applied Psychoacoustics Lab), CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). The two AB recordings (a pair of omnidirectional microphones) of a source at the front center of the stage are combined into one stereo IR, used from 1 ms before the direct sound.",
  "status.space.measuredTag": " (measured)",
  "status.space.userTag": " (your IR)",
  "status.space.lowCutNone": "None",
  "status.space.builtInMeasured": "Measured: {place}, {numbers}",
  "status.space.builtInSynth": "Synthetic: {numbers}",
  "status.space.fallbackMeasured": "the built-in measured IR",
  "status.space.fallbackSynth": "the built-in synthetic IR",
  "status.space.noFile": "No file of your own chosen yet. Using {fallback}",
  "status.space.unreadable": "Could not read {name} ({error}). Using {fallback}",
  "status.space.unknownError": "unknown reason",
  "status.space.mono": "mono",
  "status.space.stereo": "stereo",
  "status.space.firstTwo": "first 2 of {channels} channels",
  "status.space.truncated": "cut at 5 s",
  "status.space.userIr": "{name} ({seconds} s, {channels}, {rate} kHz)",
  "status.space.userIrExtra": "{name} ({seconds} s, {channels}, {rate} kHz, {extra})",
  "status.space.extraSeparator": ", ",

  "status.crossfeed.numbers": "{hz} Hz · {db} dB",
  "status.crossfeed.default": "Standard ({numbers})",
  "status.crossfeed.cmoy": "Slightly weaker ({numbers})",
  "status.crossfeed.jmeier": "Weaker ({numbers})",

  "status.filter.peak": "Peak",
  "status.filter.lowShelf": "Low shelf",
  "status.filter.highShelf": "High shelf",
  "status.filter.lowPass": "Low-pass",
  "status.filter.highPass": "High-pass",
  "status.filter.bandPass": "Band-pass",
  "status.filter.notch": "Notch",

  "status.isp.off": "Off",
  "status.isp.measured": "Lower by the measured value",
  "status.isp.fixed": "Fixed −3 dB",
  "status.replayGain.off": "Off",
  "status.replayGain.track": "Track (per track)",
  "status.replayGain.album": "Album (keeps the level differences within the album)",

  "status.mode.original":
    "Pure mode: at 100% volume, nothing is processed. ReplayGain, EQ, tone, tube, space, and crossfeed are not applied (their settings are kept).",
  "status.mode.originalIsp":
    "Pure mode: at 100% volume, nothing is processed except ISP-safe ({isp}). ReplayGain, EQ, tone, tube, space, and crossfeed are not applied (their settings are kept).",
  "status.mode.replayGainTrack": "ReplayGain (track)",
  "status.mode.replayGainAlbum": "ReplayGain (album)",
  "status.mode.eq": "EQ ({bands}-band)",
  "status.mode.tone": "tone ({parts})",
  "status.mode.tube": "tube ({type}, {details})",
  "status.mode.tubeAuto": "tube (auto by genre; {type} when the genre is not in the table; {details})",
  "status.mode.warmUp": "warm-up simulation",
  "status.mode.space": "space ({space})",
  "status.mode.crossfeed": "crossfeed ({numbers})",
  "status.mode.ispSafe": "ISP-safe ({isp})",
  "status.mode.none": "Processing mode: nothing to apply. At 100% volume, the output is the same as in Pure mode.",
  "status.mode.processed": "Processing mode: applies {parts} in 64-bit float.",

  "status.output.asio": "ASIO: {name}",
  "status.output.wasapiDefault": "WASAPI exclusive: Windows default device",
  "status.output.wasapi": "WASAPI exclusive: {name}",
  "status.output.default": "Windows default device",
  "status.output.defaultNow": "Windows default device (currently {name})",
  "status.output.missing": "{name} (not found now)",
};
