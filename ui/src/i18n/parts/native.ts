// The page's texts, part "native" (task 11-3): what the engine and the app
// report, as a code and its values (app/logic/NativeMessages.h); nativeText.ts
// turns { code, values } into the sentence with the key "native.<code>".
// nativeText.test.ts checks that every code has its key here, with exactly the
// code's value names, and that there is no key without a code.
//
// English: copied from the Japanese until task 11-4 writes it.

export const ja = {
  // ABX (PlayerBridge::abxStartAsync).
  "native.abx.alreadyRunning": "ABX はもう始まっています",
  "native.abx.notPlaying": "ABX で聴く曲を、先に再生してください",
  "native.abx.dop": "DSD（DoP）の曲は ABX で比べられません（DoP のデータには処理をかけないため）",
  "native.abx.choosePair": "比べる 2 つの設定を選んでください",
  "native.abx.analysisFailed": "区間を解析できませんでした: {detail}",

  // The article reader's window.
  "native.article.webViewMissing": "記事の表示に必要な Microsoft Edge WebView2 ランタイムが見つかりません。",
  "native.article.pageMissing": "記事の画面のファイルが見つかりません: {path}",

  // ASIO drivers: the check in a child process, the settings, the output notice.
  "native.asio.loadFailed": "この ASIO ドライバーは使えません。初期化に失敗しました（{detail}）",
  "native.asio.crashed":
    "この ASIO ドライバーは使えません。読み込むと異常終了します（確かめるための別のプロセスで起きたので、このアプリは無事です）",
  "native.asio.timedOut": "この ASIO ドライバーは使えません。応答がありませんでした",
  "native.asio.unusable": "この ASIO ドライバーは使えません",
  "native.asio.notRegistered": "この ASIO ドライバーは登録されていません",
  "native.asio.driverMissing": "選んだ ASIO ドライバー（{name}）が見つかりません。設定で出力を選び直してください",

  // Reading an EQ file: the lines skipped, the file itself.
  "native.eq.preampUnreadable": "{line} 行目: Preamp の値が読めません",
  "native.eq.lineSkipped": "{line} 行目: 読めない行なので飛ばしました",
  "native.eq.filterUnreadable": "{line} 行目: フィルターの書き方が読めません",
  "native.eq.typeUnsupported": "{line} 行目: 対応していない種類（{type}）なので飛ばしました",
  "native.eq.valuesUnreadable": "{line} 行目: 周波数やゲインの値が読めません",
  "native.eq.fileUnreadable": "このファイルは読めません: {file} ({detail})",

  // The library.
  "native.library.newerDatabase":
    "ライブラリのデータベースは、このアプリより新しい版で作られています。新しい版のアプリを使ってください: {path}",
  "native.library.openFailed": "ライブラリのデータベースを開けませんでした: {path} ({detail})",
  "native.library.noPlayableTracks": "再生できる曲がありません（ファイルが見つかりません）",
  "native.library.addFolderFailed": "フォルダーを加えられませんでした: {detail}",

  // The output: the notice when playback is not on the chosen one, the device list.
  "native.output.fallbackDefault": "選んだ出力（{name}）が見つからないため、Windows の既定のデバイスで再生しています",
  "native.output.listFailed": "出力デバイスの一覧を読めませんでした",

  // Opening files to play.
  "native.player.unreadable":
    "このファイルは読めません（WAV、FLAC、AIFF、ALAC・AAC（.m4a）、MP3、DSF、DFF に対応）: {file} ({detail})",
  "native.player.openFailed": "ファイルを開けませんでした: {detail}",
  "native.player.nothingToPlay": "再生する曲がありません",

  // A user IR file for the space.
  "native.space.irMissing": "ファイルが見つかりません（移動したか、消したのかもしれません）",
  "native.space.irNotAudio": "このファイルは IR として読めません（WAV・FLAC・AIFF などの PCM のファイルを選んでください）",
  "native.space.irDsd": "DSD のファイルは IR に使えません（PCM のファイルを選んでください）",
  "native.space.irUnreadable": "このファイルは IR として読めません",
  "native.space.irTooLong": "IR のファイルが長すぎます（60 秒まで）",
  "native.space.irSilent": "IR のファイルに音が入っていません",
} as const;

export const en: Record<keyof typeof ja, string> = { ...ja };
