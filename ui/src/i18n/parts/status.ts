// The page's texts, part "status" (task 11-2): merged into the tables by
// ../ja.ts and ../en.ts. Keys start with "status." so the parts never clash.
//
// English: copied from the Japanese until task 11-4 writes it (with the
// glossary the user decides). The type still asks for every key.

export const ja = {
  // The player's state (messages.ts stateLabel).
  "status.state.empty": "ファイルを開いてください",
  "status.state.stopped": "停止中",
  "status.state.playing": "再生中",
  "status.state.ended": "最後まで再生しました",
  "status.state.error": "再生できません",

  // A format's bits ("24bit 浮動小数点", "24bit（32bit コンテナ）").
  "status.format.float": "{bits}bit 浮動小数点",
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
    "原音モード：音量 100% なら何も処理しません、ISP セーフ（{isp}）。ReplayGain・EQ・トーン・真空管・空間・クロスフィードはかけません（設定は残ります）。",
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

export const en: Record<keyof typeof ja, string> = { ...ja };
