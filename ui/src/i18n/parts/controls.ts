// The page's texts, part "controls" (task 11-2): merged into the tables by
// ../ja.ts and ../en.ts. Keys start with "controls." so the parts never clash.
//
// English: copied from the Japanese until task 11-4 writes it (with the
// glossary the user decides). The type still asks for every key.

export const ja = {
  // --- the library (libraryView.ts, libraryFormat.ts) ---
  "controls.library.title.settings": "設定",
  "controls.library.title.albums": "アルバム",
  "controls.library.title.library": "ライブラリ",
  // The sorts (the list above the tracks or the albums).
  "controls.library.sort.artist": "アーティスト順",
  "controls.library.sort.album": "アルバム順",
  "controls.library.sort.title": "曲名順",
  "controls.library.sort.added": "追加した順（新しい順）",
  "controls.library.sort.length": "長さ順",
  "controls.library.sort.format": "形式順",
  "controls.library.sort.rate": "レート順",
  "controls.library.sort.truePeak": "True Peak 順（高い順）",
  "controls.library.sort.plays": "再生回数順（多い順）",
  "controls.library.sort.albumName": "アルバム名順",
  "controls.library.sort.year": "年順（新しい順）",
  // The track list's columns.
  "controls.library.column.title": "曲名",
  "controls.library.column.artist": "アーティスト",
  "controls.library.column.album": "アルバム",
  "controls.library.column.length": "長さ",
  "controls.library.column.format": "形式",
  "controls.library.column.rate": "レート",
  "controls.library.column.plays": "再生",
  // The True Peak column (libraryFormat.ts).
  "controls.library.peak.pending": "測定待ち",
  "controls.library.peak.pendingTitle": "True Peak とラウドネスを裏で測っています",
  "controls.library.peak.notPcmTitle": "DSD は PCM ではないので測りません",
  "controls.library.peak.failed": "測れません",
  "controls.library.peak.failedTitle": "デコードできませんでした（ファイルが壊れている可能性があります）",
  "controls.library.peak.overTitle":
    "True Peak が 0 dBTP を超えています。原音経路のままだと、DAC の中でクリップする可能性があります（ISP セーフで下げられます）",
  // Between the items of one line (the counts, a tile's artist and year).
  "controls.library.separator": " ・ ",
  // The counts under the list (libraryFormat.ts: countsText).
  "controls.library.counts.noFolders": "監視するフォルダーを加えてください（左の「＋」）",
  "controls.library.counts.tracks": "{n} 曲",
  "controls.library.counts.albums": "アルバム {n}",
  "controls.library.counts.folders": "監視中のフォルダー {n}",
  "controls.library.counts.scanning": "フォルダーを確認中",
  "controls.library.counts.toMeasure": "測定待ち {n} 曲",
  "controls.library.counts.missing": "見つからない {n} 曲",
  // An album's head.
  "controls.library.album.tracks": "{n} 曲",
  "controls.library.album.truePeak": "アルバムの True Peak {db}",
  "controls.library.readFailed": "ライブラリを読めませんでした: {error}",
  "controls.library.missingRow": "ファイルが見つかりません（ドライブがつながっていない、移動した、など）",
  // The context menus.
  "controls.library.menu.playFrom": "ここから再生",
  "controls.library.menu.playNext": "次に再生",
  "controls.library.menu.enqueue": "キューの最後に追加",
  "controls.library.menu.playAlbum": "アルバムを再生",
  "controls.library.menu.playAlbumNext": "アルバムを次に再生",
  "controls.library.menu.enqueueAlbum": "アルバムをキューの最後に追加",
  "controls.library.tileTitle": "{album} — {artist}（{n} 曲）",
  "controls.library.empty.search": "見つかりませんでした。かな・全角半角・大文字小文字は区別しません。漢字の読みでは探せません。",
  "controls.library.empty.albums": "アルバムがありません（アルバム名のタグがある曲が、ここに並びます）",
  "controls.library.empty.tracks": "曲がありません。左の「監視フォルダー」の「＋」で、音楽のフォルダーを加えてください。",
  "controls.library.noPlayable": "再生できる曲がありません（ファイルが見つかりません）",
  // The watched folders.
  "controls.library.folder.title": "{path}\n{n} 曲",
  "controls.library.folder.titleMissing": "{path}\n{n} 曲（見つからない {missing} 曲）",
  "controls.library.folder.removeTitle": "このフォルダーをライブラリから外す",
  "controls.library.folder.removeConfirm":
    "「{path}」をライブラリから外します。\n中の曲の再生回数・評価・修正も消えます（音楽ファイルは消しません）。よろしいですか？",

  // --- the ABX (abx.ts, abxView.ts) ---
  "controls.abx.kind.processed": "原音 と 音作り（今の設定）",
  "controls.abx.kind.sixteenBit": "原音 と 16bit（TPDF ディザ）",
  "controls.abx.kind.presets": "設定 A と 設定 B（EQ・トーン・真空管）",
  "controls.abx.side.original": "原音",
  "controls.abx.side.processed": "音作り",
  "controls.abx.side.a": "設定 A",
  "controls.abx.side.b": "設定 B",
  "controls.abx.presetsTitle": "設定 A「{a}」と 設定 B「{b}」",
  // A side's name (設定 A と設定 B), sent with the request and shown in the run.
  "controls.abx.eq.currentName": "今の EQ",
  "controls.abx.eq.none": "EQ なし",
  "controls.abx.side.tone": "トーン（{tone}）",
  "controls.abx.side.warmUpNow": "ウォームアップ：今の状態",
  "controls.abx.side.warmUpCold": "ウォームアップ：起動直後",
  "controls.abx.side.tube": "{tube}（DRIVE {drive}{extras}）",
  "controls.abx.side.noTube": "真空管なし",
  // Between the parts of a side's name.
  "controls.abx.side.separator": "・",
  // The set-up's choices.
  "controls.abx.eq.current": "今の EQ（{bands} バンド）",
  "controls.abx.eq.currentOff": "今の EQ（かかりません）",
  "controls.abx.tone.current": "今のトーン（{tone} dB）",
  "controls.abx.tone.currentNone": "今のトーン（なし）",
  "controls.abx.problem.samePair": "A と B に違う設定を選んでください。",
  "controls.abx.problem.notPlaying": "ABX で聴く曲を、先に再生してください。",
  "controls.abx.problem.dop": "DSD（DoP）の曲は ABX で比べられません（DoP のデータには処理をかけないため）。",
  "controls.abx.note.originalMode":
    "今は原音モードなので、B も原音と同じ音です（確かめ用の対照として使えます）。音作りの設定と比べるには、設定画面で音作りモードにしてから始めてください。",
  "controls.abx.note.already16bit": "この曲はもともと 16bit なので、B との違いはディザの雑音（−101 dBFS 程度）だけです。",
  "controls.abx.startFailed": "始められませんでした",
  "controls.abx.region": "{from}〜{to}（{seconds} 秒）をくり返し",
  "controls.abx.trial": "{trial} / {trials} 回目",
  "controls.abx.match":
    "比べるもの：{title}。B を {gain} dB にして、A と同じ音量に合わせています（この区間で解析）。答えると X が入れ替わります（聴いている音はそのまま）。",
  // The result.
  "controls.abx.result.levelUnmeasured": "音量差：測れませんでした（流した時間が短すぎます。比べ始めの 3 秒は測りません）",
  "controls.abx.result.level": "音量差 {db} dB（基準は 0.1 dB 以内）",
  "controls.abx.result.levelOver":
    "音量差 {db} dB（基準は 0.1 dB 以内）：基準を超えたので、結果は参考です（音量の違いで聞き分けた可能性があります）",
  "controls.abx.result.aborted": "中断しました",
  "controls.abx.result.abortedVerdict": "途中で中断したので、結果は出しません（途中の結果で判断すると、p の意味が崩れるため）。",
  "controls.abx.result.distinguished":
    "p < 0.05：偶然とは考えにくい結果です。A と B の違いを聞き分けられた可能性が高いと言えます。",
  "controls.abx.result.notDistinguished":
    "p ≥ 0.05：偶然でも十分に起こりうる結果です。聞き分けられたとは言えません（違いがないという意味ではありません）。",
  "controls.abx.result.tooFew":
    "正解が少なすぎます（当てずっぽうでこれ以下になる確率は {percent}%）。A と B の違いは聞こえていて、取り違えている可能性があります。この検定は「当たりが多いか」だけを見るので、これを「聞き分けられた」とは数えません。もう一度試すと確かめられます。",
  "controls.abx.result.headline": "{trials} 回中 {correct} 回正解",
  "controls.abx.result.p": "p = {p}（当てずっぽうでこれ以上当たる確率）",

  // --- the EQ editor (eqEditor.ts) ---
  "controls.eqEditor.preamp": "プリアンプ",
  "controls.eqEditor.originalMode": "今は原音モードなので、EQ はかかりません（ここで決めた設定は残ります）。",
  "controls.eqEditor.eqOff": "「EQ を使う」がオフなので、EQ はかかりません。",
  "controls.eqEditor.bandCount": "{n} / {max} バンド",
  "controls.eqEditor.preampAuto":
    "自動：{db}（{khz} kHz で計算。曲のレートで少し変わります）。EQ そのものでは 0 dB を超えません。",
  "controls.eqEditor.preampManualShort":
    "手動：最大のブーストより {db} dB 少なく下げています。0 dBFS に近い曲では、超えた分を保険のリミッターが抑えます。",
  "controls.eqEditor.preampManualOk": "手動：最大のブースト以上に下げているので、EQ そのものでは 0 dB を超えません。",
  "controls.eqEditor.limiterIdle": "保険のリミッター：働いていません（設定を変えてから、または再生を始めてから）。",
  "controls.eqEditor.limiterActive": "保険のリミッター：{events} 回、最大 {db}（設定を変えてから、または再生を始めてから）。",
  "controls.eqEditor.noBands": "バンドがありません。グラフをダブルクリックして足してください。",
  "controls.eqEditor.chooseBand": "バンドを選ぶと、ここで周波数・ゲイン・Q をツマミで変えられます。",
  "controls.eqEditor.bandTitle": "バンド {n}",
  "controls.eqEditor.use": "使う",
  "controls.eqEditor.dontUse": "使わない",
  "controls.eqEditor.type": "種類",
  "controls.eqEditor.soloTitle": "このバンドの周波数のあたりだけを聴く（バンドパス。保存はしません）",
  "controls.eqEditor.removeBand": "このバンドを消す",
  "controls.eqEditor.frequency": "周波数",
  "controls.eqEditor.gain": "ゲイン",
  "controls.eqEditor.soloOn": "Solo 中",
  "controls.eqEditor.soloStop": "Solo をやめる",
  "controls.eqEditor.remove": "消す",
  "controls.eqEditor.nodeTitle": "バンド {n}：{band} Q {q}",

  // --- the EQ presets (eqPresets.ts, eqPresetBar.ts) ---
  "controls.eqPresets.flat": "EQ なし（平ら）",
  "controls.eqPresets.noName": "名前を入れてください",
  "controls.eqPresets.tooMany": "プリセットは {max} 個までです。使わないものを消してください",
  "controls.eqPresets.notFound": "そのプリセットは見つかりません",
  "controls.eqPresets.taken": "「{name}」はもうあります",
  "controls.eqPresets.label": "プリセット",
  "controls.eqPresets.selectLabel": "EQ のプリセット",
  "controls.eqPresets.name": "プリセットの名前",
  "controls.eqPresets.save": "保存…",
  "controls.eqPresets.saveTitle": "今の EQ をプリセットとして保存します（同じ名前は置き換え）",
  "controls.eqPresets.rename": "名前を変える…",
  "controls.eqPresets.renameTitle": "選んでいるプリセットの名前を変えます",
  "controls.eqPresets.delete": "消す",
  "controls.eqPresets.deleteTitle": "選んでいるプリセットを消します",
  "controls.eqPresets.cancel": "キャンセル",
  "controls.eqPresets.none": "（プリセットはまだありません）",
  "controls.eqPresets.unsaved": "（今の EQ：保存していません）",
  "controls.eqPresets.saveOk": "保存",
  "controls.eqPresets.renameOk": "名前を変える",
  "controls.eqPresets.willReplace": "「{name}」を今の EQ で置き換えます",
  "controls.eqPresets.renamed": "名前を「{name}」に変えました",
  "controls.eqPresets.saved": "「{name}」を保存しました",
  "controls.eqPresets.confirmDelete": "もう一度押すと消します",
  "controls.eqPresets.deleted": "「{name}」を消しました（今の EQ はそのままです）",

  // --- the tube type by genre (tubeAuto.ts, tubeAutoView.ts) ---
  "controls.tubeAuto.problem.tooManyRules": "対応表は {max} 行までです",
  "controls.tubeAuto.problem.emptyGenre": "ジャンルが空の行があります",
  "controls.tubeAuto.problem.genreTooLong": "ジャンルは {max} 文字までです",
  "controls.tubeAuto.problem.sameGenre": "「{first}」と「{second}」は同じジャンルとして扱われます",
  "controls.tubeAuto.problemNotSaved": "{problem}。直すまで保存しません。",
  "controls.tubeAuto.saveFailed": "保存できませんでした",
  "controls.tubeAuto.choice.off": "自動で選びません。いつも手動で選んだ種類（{manual}）です。",
  "controls.tubeAuto.choice.notPlaying":
    "再生していません。曲ごとに、アルバムの指定 → ジャンルの対応表 → 手動で選んだ種類（{manual}）の順で決めます。",
  "controls.tubeAuto.choice.album": "今の曲：アルバム「{album}」の指定で {type}。",
  "controls.tubeAuto.choice.genre": "今の曲：ジャンル「{genre}」が対応表にあるので {type}。",
  "controls.tubeAuto.choice.noGenre": "今の曲：ジャンルのタグがないので、手動で選んだ種類（{manual}）。",
  "controls.tubeAuto.choice.unknownGenre": "今の曲：ジャンル「{genres}」は対応表にないので、手動で選んだ種類（{manual}）。",
  // Between the current track's genres.
  "controls.tubeAuto.genreSeparator": "・",
  "controls.tubeAuto.albumAuto": "自動（ジャンルで選ぶ）",
  "controls.tubeAuto.ruleCount": "{n} 行",
  "controls.tubeAuto.rowGenre": "{n} 行目のジャンル",
  "controls.tubeAuto.rowType": "{n} 行目の種類",
  "controls.tubeAuto.remove": "削除",
  "controls.tubeAuto.rowRemove": "{n} 行目を削除",
  "controls.tubeAuto.albumChoice": "{album}：{type}",
  "controls.tubeAuto.noAlbumName": "（名前なし）",
  "controls.tubeAuto.stopChoice": "指定をやめる",
  "controls.tubeAuto.noAlbums": "指定したアルバムはありません。再生中のアルバムの種類を上で選ぶと、ここに並びます。",
  "controls.tubeAuto.albumCount": "{n} 枚",

  // --- the amp view (ampView.ts) ---
  "controls.amp.knob.volume": "VOLUME（デジタル音量。100% なら処理をしません）",
  "controls.amp.knob.mix": "MIX（空間の響きの割合。0% で元の音だけ）",
  "controls.amp.knob.disabled": "{label}：{why}",
  "controls.amp.knob.unusable": "使えません",
  "controls.amp.knob.title": "{label}：{value}（上下にドラッグ、Shift で細かく。ホイール、ダブルクリックで既定値）",
  "controls.amp.vu": "VU メーター {ch}",
  "controls.amp.lamp.pure": "PURE：原音モード（処理をかけません。音量 100% で BIT-PERFECT になりえます）",
  "controls.amp.lamp.tube": "TUBE：音作りモードで真空管をかけます",
  "controls.amp.lamp.off": "SPACE OFF：空間（部屋の響きの畳み込み）をかけません",
  "controls.amp.lamp.club":
    "SPACE CLUB：小さな部屋の響きを畳み込んで混ぜます（IR は合成か自分のファイル。MIX・プリディレイとあわせて設定画面で）",
  "controls.amp.lamp.hall":
    "SPACE HALL：ホールの響きを畳み込んで混ぜます（IR は実測の St Paul's Hall・合成・自分のファイル。MIX・プリディレイとあわせて設定画面で）",
  "controls.amp.lamp.arena":
    "SPACE ARENA：大きな屋内空間の響きを畳み込んで混ぜます（IR は合成か自分のファイル。MIX・プリディレイとあわせて設定画面で）",
  "controls.amp.lamp.tubeType": "TUBE TYPE：{type}",
  "controls.amp.lamp.crossfeed": "CROSSFEED：ヘッドホン向けのクロスフィード（強さは設定画面で）",
  "controls.amp.lamp.auto": "AUTO：曲のジャンルで真空管の種類を選んでいます（設定画面の「ジャンルで種類を選ぶ」）",
  "controls.amp.mode.tube": "MODE：TUBE（音作りモード）→ 押すと PURE（原音モード）",
  "controls.amp.mode.pure": "MODE：PURE（原音モード）→ 押すと TUBE（音作りモードで真空管）",
  "controls.amp.crossfeed.on": "CROSSFEED：オン（ヘッドホン向けのクロスフィード。強さは設定画面で。音作りモードでかかります）",
  "controls.amp.crossfeed.off": "CROSSFEED：オフ（ヘッドホン向けのクロスフィード。強さは設定画面で。音作りモードでかかります）",
  "controls.amp.processingOnly": "音作りモード（TUBE）でかかります",
  "controls.amp.badgeTitle": "出力の確認（経路と BIT-PERFECT の判定）",
} as const;

export const en: Record<keyof typeof ja, string> = { ...ja };
