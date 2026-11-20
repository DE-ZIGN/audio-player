// The page's texts, part "page" (task 11-2): merged into the tables by
// ../ja.ts and ../en.ts. Keys start with "page." so the parts never clash.
//
// English: copied from the Japanese until task 11-4 writes it (with the
// glossary the user decides). The type still asks for every key.

export const ja = {
  // --- the top bar (index.html, main.ts) ---
  "page.amp.label": "アンプ",
  "page.now.choose": "曲を選んでください",
  "page.transport.shuffle": "シャッフル",
  "page.transport.prev": "前の曲",
  "page.transport.prevTitle": "前の曲（3 秒より先なら曲の頭へ）",
  "page.transport.play": "再生",
  "page.transport.pause": "一時停止",
  "page.transport.playPauseTitle": "再生 / 一時停止（スペース）",
  "page.transport.playTitle": "再生（スペース）",
  "page.transport.pauseTitle": "一時停止（スペース）",
  "page.transport.stop": "停止",
  "page.transport.stopTitle": "停止（曲の頭へ戻る）",
  "page.transport.next": "次の曲",
  "page.transport.repeat": "リピート",
  "page.transport.seek": "再生位置",
  // Shuffle and repeat (playModes.ts, main.ts): the state, then what the button does.
  "page.shuffle.off": "シャッフル：オフ",
  "page.shuffle.on": "シャッフル：オン",
  "page.shuffle.title": "{state}（この先の曲の順番を混ぜる）",
  "page.repeat.off": "リピート：オフ",
  "page.repeat.all": "リピート：全曲",
  "page.repeat.one": "リピート：1 曲",
  "page.repeat.title": "{state}（押すごとに オフ → 全曲 → 1 曲）",
  "page.volume.boxTitle": "デジタル音量。100% なら処理をしない（原音経路、BIT-PERFECT になりうる）。下げると 64bit の処理経路を通ります",
  "page.volume.mute": "ミュート", // the button
  "page.volume.slider": "音量",
  "page.volume.muted": "ミュート", // the volume shown (queueOps.ts)
  "page.levels.title": "出力に渡す値の True Peak（直近 0.1 秒）。正確な値は「出力の確認」で",
  "page.vu.title": "VU メーター（出力に渡す値。0 VU = −18 dBFS のサイン波、約 300 ms の平均。正確な値は「出力の確認」の True Peak と LUFS で）",
  "page.vu.label": "VU メーター {channel}",
  "page.topbar.outputTitle": "出力の確認（経路とメーター）",
  "page.topbar.output": "出力",

  // --- the side bar ---
  "page.nav.now": "再生中",
  "page.nav.tracks": "曲",
  "page.nav.albums": "アルバム",
  "page.nav.settings": "設定",
  "page.nav.openFile": "ファイルを開く",
  "page.folders.title": "監視フォルダー",
  "page.folders.add": "フォルダーを加える",
  "page.folders.addButton": "＋",
  // The views (uiView.ts, main.ts).
  "page.view.standard": "標準ビュー",
  "page.view.library": "ライブラリビュー",
  "page.view.amp": "アンプビュー",
  // The side bar's buttons to the other two views.
  "page.view.toStandard": "標準ビューにする",
  "page.view.toLibrary": "ライブラリビューにする",
  "page.view.toAmp": "アンプビューにする",
  "page.view.toggleTitle": "ビューを切り替える（見た目と配置だけが変わります）",

  // --- the library's head ---
  "page.library.title": "ライブラリ", // until libraryView.ts names the list shown
  "page.library.search": "ライブラリを検索（かな・全角半角を問わず）",
  "page.library.searchLabel": "検索",
  "page.library.sort": "並べ替え",
  "page.library.display": "表示",
  "page.library.list": "一覧",
  "page.library.tiles": "タイル",
  "page.album.back": "← アルバム一覧へ",

  // --- the play queue and the status bar (main.ts) ---
  "page.queue.heading": "再生キュー",
  "page.queue.clear": "この先を消す",
  "page.queue.clearTitle": "今の曲より後を消す",
  "page.queue.count": "{total} 曲・あと {left} 曲",
  "page.queue.countFromFiles": "{total} 曲（ファイルを開いて再生）・あと {left} 曲",
  "page.queue.remove": "キューから外す",
  "page.queue.cannotAdd": "ファイルを開いて再生している列には、ライブラリの曲を足せません（ライブラリから再生し直してください）",
  "page.state.paused": "一時停止中",
  "page.backend.mock": "模擬エンジン（ブラウザー単体で表示中）",

  // --- the settings screen (index.html, settingsView.ts) ---
  "page.settings.tabsLabel": "設定の項目",
  "page.details": "詳細", // every <summary> of an explanation
  "page.settings.forMode": "音作りモードでかけます", // beside the headings
  "page.settings.readFailed": "設定を読めませんでした: {error}",
  "page.settings.applyFailed": "設定を反映できませんでした",
  // 出力
  "page.settings.output.heading": "出力",
  "page.settings.output.device": "出力デバイス",
  "page.settings.output.deviceNote":
    "WASAPI は排他モードで出します（ほかのアプリの音は混ざりません）。ASIO ドライバーは、選んだときに別のプロセスで読み込んで確かめます。",
  "page.settings.output.dop": "DSD を DoP で送る",
  "page.settings.output.dopNote":
    "DoP に対応した DAC だけでオンにしてください。対応していない DAC では大きな雑音になります（DAC が対応しているかは自動では分かりません）。出力を選び直すとオフに戻ります。",
  "page.settings.output.wasapiGroup": "WASAPI 排他",
  "page.settings.output.missing":
    "選んである出力が今は見つかりません。つなぐと使えます（WASAPI は見つからない間、Windows の既定のデバイスで出します）。",
  "page.settings.output.asioChecking": "ASIO ドライバーを別のプロセスで読み込んで確かめています…",
  "page.settings.output.chooseFailed": "この出力は選べませんでした",
  "page.settings.output.dopFailed": "設定できませんでした",
  // モード
  "page.settings.mode.heading": "モード",
  "page.settings.mode.group": "モード",
  "page.settings.mode.original": "原音モード",
  "page.settings.mode.originalNote": "ReplayGain・EQ をかけません。音量 100% で処理がなければ、ビットパーフェクトの経路で出します。",
  "page.settings.mode.processed": "音作りモード",
  "page.settings.mode.processedNote": "下の ReplayGain・EQ・ISP セーフを 64bit 浮動小数点でかけます。",
  "page.settings.mode.abx": "ABX で聴き比べる…",
  "page.settings.mode.abxNote": "今の曲で、原音と音作りの違いを聞き分けられるかを、ブラインドテストで確かめます。",
  // ISP セーフ
  "page.settings.isp.heading": "ISP セーフ",
  "page.settings.isp.original": "原音モードでは",
  "page.settings.isp.processed": "音作りモードでは",
  "page.settings.isp.note":
    "曲の True Peak（サンプルの間のピーク）が −0.5 dBTP 以下になるように、音量を線形に下げます。「計測した値」は取り込み時に測った曲（アルバムの ReplayGain ではアルバム）の True Peak から、測っていない曲は −3 dB。どちらも保険のリミッター（−0.3 dBTP）を通します。",
  // ReplayGain
  "page.settings.replayGain.note":
    "曲（またはアルバム）の統合ラウドネスを −18 LUFS に合わせます。値は取り込み時の解析から。上げるときは True Peak が 0 dBTP を超えない分までにします。解析していない曲にはかけません。",
  // EQ
  "page.settings.eq.enabled": "EQ を使う",
  "page.settings.eq.import": "ファイルから読み込む…",
  "page.settings.eq.open": "EQ を開く",
  "page.settings.eq.curve": "EQ の周波数特性（クリックで EQ を開く）",
  "page.settings.eq.note":
    "最小位相の IIR（バイクアッド）で 64bit 浮動小数点。プリアンプは、自動なら最大のブーストの分だけ下げ、EQ そのものではどの周波数も 0 dB を超えないようにします。手で決めることもでき、そのとき超えた分は保険のリミッターが受け持ちます（働いた回数と量を表示します）。読み込めるのは AutoEQ の ParametricEQ.txt と Equalizer APO の書き方です（ファイルのプリアンプの値は使いません）。",
  "page.settings.eq.presetSaveFailed": "プリセットを保存できませんでした",
  "page.settings.eq.importFailed": "読み込めませんでした",
  "page.settings.eq.tooManyBands": "バンドは {max} までです。残りは読み込みませんでした。",
  "page.settings.eq.filePreamp": "ファイルのプリアンプ（{db}）は使いません（プリアンプは自動か、手で決めます）。",
  "page.settings.eq.none": "EQ はかかりません。",
  "page.settings.eq.preampAuto": "{bands} バンド、プリアンプ {db}（自動）。実線が EQ、点線がプリアンプを含めた出力。",
  "page.settings.eq.preampManual": "{bands} バンド、プリアンプ {db}（手動）。実線が EQ、点線がプリアンプを含めた出力。",
  "page.settings.eq.originalNotApplied": "今は原音モードなのでかけません。", // after the line above, a space between
  // トーン
  "page.settings.tone.heading": "トーン",
  "page.settings.tone.reset": "0 に戻す",
  "page.settings.tone.note":
    "BASS は 150 Hz、TREBLE は 4.5 kHz を中心に、なだらかに上げ下げします（±10 dB）。上げた分はプリアンプで下げ、0 dB を超えて持ち上げません。パラメトリック EQ の後にかかります。",
  // 真空管
  "page.settings.tube.heading": "真空管",
  "page.settings.tube.enabled": "真空管を使う",
  "page.settings.tube.types": "真空管の種類",
  "page.settings.tube.driveReset": "既定（-12 dB）に戻す",
  "page.settings.tube.switchNote": "オン・オフを切り替えると、処理の遅れが変わるため、再生中は一瞬途切れます。",
  "page.settings.tube.note":
    "真空管のモデル（Koren の式）と負荷線から求めた伝達特性で、倍音を加えます（波形整形方式）。信号の大きさに応じたバイアスの変化と電源の沈み込みを、回路の時定数にもとづいて簡易に再現しています。 小さな音の音量は変えず、DRIVE で曲がり方（倍音の量）が変わります。DRIVE 0 dB で、0 dBFS の山がグリッドを 0 V まで振ります。",
  "page.settings.tube.warmUpWaiting": "起動からの時間に合わせて移ります。",
  "page.settings.tube.unitNow": "{unit}（{type}）：{channels}",
  // 演出 (the tube's show)
  "page.settings.tubeShow.heading": "演出",
  "page.settings.tubeShow.romance": "ロマン枠：効果は確かめていません",
  "page.settings.tubeShow.warmUp": "ウォームアップ",
  "page.settings.tubeShow.units": "個体差",
  "page.settings.tubeShow.unit": "球の番号",
  "page.settings.tubeShow.unitNext": "次の番号の球にする",
  "page.settings.tubeShow.note":
    "実機の真空管のウォームアップや個体差を、真空管のモデルで計算して再現する演出です。球を変えて聴き比べるといった、マニアならではの楽しみを体験してください。データ上、音の違いは出ていますが、実際に聴いて違いが感じ取れるか？ ぜひ ABX（「設定 A と 設定 B」）で試してみてください。",
  "page.settings.tubeShow.warmUpNote":
    "ウォームアップ：アプリを起動してからの時間で、特性を「冷えた球」から「暖まった球」へ移します。冷えた球は、同じ回路でカソードのエミッション（電子を出す量）を 50% にして求めた特性です。冷えた特性の割合は時定数 4 分で減り、起動から 20 分で暖まった特性だけになります。",
  "page.settings.tubeShow.unitsNote":
    "個体差：同じ型番の球の、1 本ごとのばらつきです。球ごとにエミッションを ±10%、増幅率 μ を ±5% の範囲でずらし、その球で特性を求め直します。値は番号で決まり、同じ番号ならいつも同じ球です。左右は別の球で、PP では 2 本の差のぶん、打ち消し合っていた偶数次の倍音が残ります。",
  "page.settings.tubeShow.bothNote": "どちらも小さな音の音量は変えず、曲がり方（倍音の量）が変わります。",
  // ジャンルで種類を選ぶ (the tube type by genre). The album's name and the
  // counts are filled in by tubeAutoView.ts between the pieces.
  "page.settings.tubeAuto.heading": "ジャンルで種類を選ぶ",
  "page.settings.tubeAuto.enabled": "曲のジャンルで種類を自動で選ぶ",
  "page.settings.tubeAuto.albumBefore": "このアルバム（",
  "page.settings.tubeAuto.albumSelect": "このアルバムの真空管の種類",
  "page.settings.tubeAuto.rulesBefore": "ジャンルの対応表（",
  "page.settings.tubeAuto.albumsBefore": "アルバムごとの指定（",
  "page.settings.tubeAuto.after": "）",
  "page.settings.tubeAuto.genre": "ジャンル",
  "page.settings.tubeAuto.type": "種類",
  "page.settings.tubeAuto.addRow": "行を足す",
  "page.settings.tubeAuto.reset": "初期値に戻す",
  "page.settings.tubeAuto.startNote": "対応表の初期値は出発点で、ジャンルに合う種類を示すものではありません。",
  "page.settings.tubeAuto.note":
    "曲ごとに、アルバムの指定 → ジャンルの対応表 → 上で手動で選んだ種類、の順で決めます。ジャンルのタグは「;」「/」「,」で区切り、大文字・小文字と全角・半角を区別せずに比べます。完全に一致する行を先に、なければジャンルの一部に含まれる行（「Vocal Jazz」の「Jazz」）を、表の上から探します。 種類は曲の境目で切り替わります（20 ms でなめらかに）。どの種類も小さな音の音量はそろえてあり、音楽に近い信号（DRIVE −12 dB）で差は 0.03 dB 以内です。ABX で比べるときは使いません。",
  // 空間
  "page.settings.space.heading": "空間",
  "page.settings.space.rooms": "空間（部屋）",
  "page.settings.space.preDelay": "プリディレイ",
  "page.settings.space.lowCut": "ローカット",
  "page.settings.space.reset": "既定に戻す（MIX 20%・10 ms・120 Hz）",
  "page.settings.space.irs": "部屋ごとの IR（実測・合成・自分のファイル）",
  "page.settings.space.irsNote":
    "自分のファイルは、WAV・FLAC・AIFF の、モノラルかステレオの IR を読めます。最初の音より前の無音は除き、5 秒より長い分は切ります。出力のレートと違えば、変換して使います。読めないファイルのときは、その部屋の内蔵の IR を使います。",
  "page.settings.space.note":
    "部屋の響き（インパルス応答、IR）を、曲に畳み込んで混ぜます（区切った FFT による畳み込み、64bit 浮動小数点）。MIX は響きの割合で、0% は元の音だけ、100% は響きだけです。IR はエネルギーを 1 にそろえてあり、広い帯域の音では、響きが元の音とほぼ同じ大きさになります。 プリディレイは、元の音から響きが始まるまでの時間です（計算の区切りのため、最小 約 3 ms）。元の音は遅らせません。ローカットは響きの側だけにかける 2 次のハイパスで、元の音の低域は変えません。 HALL の既定は、実在のホール（St Paul's Hall、英国）で測った IR です。CLUB と ARENA の既定、および「合成」は、鏡像法の初期反射と、帯域ごとに減衰する雑音から合成した IR で、実在の場所を測ったものではありません（数字は部屋の大きさと、中域の残響時間）。 部屋の切り替えは約 40 ms のクロスフェードで、再生は途切れません。OFF か MIX 0% なら処理を外し、ほかに処理がなければ原音経路と同じ出力です。真空管の後、クロスフィードの前にかかります。",
  "page.settings.space.preDelayBelowMin": "{value}（最小 約 3 ms）",
  "page.settings.space.off": "OFF：空間をかけません。",
  "page.settings.space.roomNote": "{room}：{ir}",
  "page.settings.space.irGroup": "{room} の IR",
  "page.settings.space.irMeasured": "実測（St Paul's Hall）",
  "page.settings.space.irSynth": "合成",
  "page.settings.space.irFile": "自分のファイル",
  "page.settings.space.irFileNamed": "自分のファイル（{file}）",
  "page.settings.space.chooseFile": "ファイルを選ぶ…",
  // クロスフィード
  "page.settings.crossfeed.heading": "クロスフィード",
  "page.settings.crossfeed.enabled": "クロスフィードを使う（ヘッドホン向け）",
  "page.settings.crossfeed.levels": "クロスフィードの強さ",
  "page.settings.crossfeed.note":
    "左右それぞれの低い音を、反対側にも少し遅れた位相で混ぜます（bs2b の方式）。数字は、混ぜる音のローパスの周波数と、低い音で反対側がまっすぐの音より何 dB 小さいか。中央の音は、低域で 0 dB、高域でやや下がります（標準で 15 kHz に −1.75 dB）。ステレオの曲だけにかけます。真空管の後にかかります。",
  // 出力レート
  "page.settings.rate.heading": "出力レート",
  "page.settings.rate.modes": "出力レート",
  "page.settings.rate.rate": "レート",
  "page.settings.rate.multiple": "倍数",
  "page.settings.rate.multipleOption": "{k} 倍",
  "page.settings.rate.keepNote": "レートを上げても、元の録音にない音（情報）は増えません。変換すると BIT-PERFECT ではなくなります。",
  "page.settings.rate.note":
    "リサンプラー（r8brain-free-src、直線位相）で、処理の最後（保険のリミッターの後、ディザの前）にレートを変換します。EQ・真空管・空間などの計算は、曲のレートのままです。設計値は阻止域の減衰 180 dB。自動テストでの測定では、折り返し（エイリアス）とイメージは 200 dB 以上小さく、20 kHz までの音量の差は 0.01 dB 未満です。",
  "page.settings.rate.modesNote":
    "固定：どの曲もそのレートで出します。曲ごとに DAC のレートが切り替わらなくなります（レートの違う曲の境目では出力を開き直すので、一瞬途切れます）。下限：そのレート未満の曲だけを上げます。整数倍：曲の 2・4・8 倍で出します（44.1 kHz の曲は 88.2・176.4・352.8 kHz）。DAC が受け付けないときは、受け付けるいちばん大きい倍数、それもなければ曲のレートのままです。",
  "page.settings.rate.unsupportedNote":
    "音作りモードで、DAC が曲のレートを受け付けないときは、受け付けるレート（同じ系列の 2 倍・半分などを優先）へ変換して鳴らします。原音モードでは変換せず、再生できない理由を表示します。",
  // 見た目
  "page.settings.look.heading": "見た目",
  "page.settings.look.views": "ビュー（見た目と配置だけが変わり、機能と表示する情報は同じです）",
  "page.settings.look.standard": "標準ビュー",
  "page.settings.look.standardSub": "写実的メディアプレイヤー",
  "page.settings.look.amp": "アンプビュー",
  "page.settings.look.ampSub": "真空管アンプ風。情報欄は小さめ",
  "page.settings.look.library": "ライブラリビュー",
  "page.settings.look.librarySub": "ライブラリ型メディアプレイヤー風",
  "page.settings.look.themes": "ライブラリビューのテーマ（色だけが変わります）",
  "page.settings.look.amber": "アンバー",
  "page.settings.look.bluegray": "ブルーグレー",
  // 記事
  "page.settings.articles.heading": "記事",
  "page.settings.articles.open": "記事を開く…",
  "page.settings.articles.note": "このプレイヤーの考え方や、ビットパーフェクト・ABX などの解説を、別の窓で開きます（記事は準備中です）。",

  // --- the dialogs ---
  "page.dialog.close": "閉じる", // the ✕ button's name
  // The EQ editor
  "page.eqDialog.toProcessed": "音作りモードにする",
  "page.eqDialog.enabled": "EQ を使う",
  "page.eqDialog.range": "縦の範囲",
  "page.eqDialog.import": "ファイルから読み込む…",
  "page.eqDialog.clear": "すべて消す",
  "page.eqDialog.graph": "EQ のグラフ。ダブルクリックでバンドを足す",
  "page.eqDialog.hint":
    "何もないところをダブルクリック：バンドを足す ・ ノードをドラッグ：左右で周波数、上下でゲイン（ゲインのない種類は Q） ・ ノードの上でホイール：Q ・ Shift：細かく ・ ノードをダブルクリックか Delete：消す ・ 右クリック：メニュー",
  "page.eqDialog.bands": "バンド",
  "page.eqDialog.preamp": "プリアンプ",
  "page.eqDialog.autoPreamp": "自動（最大のブーストの分だけ下げる）",
  // ABX
  "page.abx.kinds": "比べるもの",
  "page.abx.kindProcessed": "原音 と 音作り（今の設定）",
  "page.abx.kind16bit": "原音 と 16bit（TPDF ディザ）",
  "page.abx.kindPresets": "設定 A と 設定 B（EQ・トーン・真空管）",
  "page.abx.tone": "トーン",
  "page.abx.tube": "真空管",
  "page.abx.warmUp": "ウォームアップ",
  "page.abx.unit": "球の番号",
  "page.abx.aEq": "A の EQ",
  "page.abx.aTone": "A のトーン",
  "page.abx.aTube": "A の真空管",
  "page.abx.aDrive": "A の DRIVE",
  "page.abx.aWarmUp": "A のウォームアップ",
  "page.abx.aUnit": "A の球の番号（0 は個体差なし）",
  "page.abx.bEq": "B の EQ",
  "page.abx.bTone": "B のトーン",
  "page.abx.bTube": "B の真空管",
  "page.abx.bDrive": "B の DRIVE",
  "page.abx.bWarmUp": "B のウォームアップ",
  "page.abx.bUnit": "B の球の番号（0 は個体差なし）",
  "page.abx.toneCurrent": "今のトーン",
  "page.abx.none": "なし",
  "page.abx.warmUpNow": "今の状態",
  "page.abx.warmUpCold": "起動直後に固定",
  "page.abx.presetsNote":
    "どちらも音作りモードで、今の ReplayGain・ISP セーフ・音量をかけ、EQ・トーン・真空管を A と B で選んだものにします。B の音量は A に合わせます。真空管の DRIVE と演出（ウォームアップ・球の番号。0 は個体差なし）は、真空管を選んだ側だけに効きます。「起動直後に固定」は、起動したばかりの冷えた球の特性のまま比べます。",
  "page.abx.regionStart": "区間の始まり",
  "page.abx.here": "今の再生位置から",
  "page.abx.length": "区間の長さ",
  "page.abx.length10": "10 秒",
  "page.abx.length20": "20 秒",
  "page.abx.length30": "30 秒",
  "page.abx.length60": "60 秒",
  "page.abx.trials": "回数",
  "page.abx.trials8": "8 回",
  "page.abx.trials12": "12 回",
  "page.abx.trials16": "16 回",
  "page.abx.trials20": "20 回",
  "page.abx.trials24": "24 回",
  // Written in by main.ts: its "p < 0.05" cannot be a data-i18n text (the
  // page would have it as "&lt;"). The lines of the page's paragraph, joined
  // by a space as the page showed them.
  "page.abx.howItWorks":
    "区間をくり返し流します（つなぎ目は 5 ms のクロスフェード）。B の音量は、この区間で解析して A に合わせます。 A と B は何度でも切り替えて聴けます。X は回ごとに A か B のどちらかで、OS の乱数で決めます。 正解数は最後まで出しません（途中で見ると、当たっているところでやめる偏りが入り、p の意味が崩れるため）。 p < 0.05 なら「聞き分けられた可能性が高い」とします。どちらの音がよいかは判定しません。",
  // The run's texts in the page, until abxView.ts fills them in.
  "page.abx.trialPlaceholder": "1 / 16 回目",
  "page.abx.aPlaceholder": "原音",
  "page.abx.bPlaceholder": "音作り",
  "page.abx.start": "始める",
  "page.abx.preparing": "区間を解析しています…",
  "page.abx.choices": "聴く音",
  "page.abx.xIs": "A か B",
  "page.abx.answers": "答え",
  "page.abx.answerA": "X は A",
  "page.abx.answerB": "X は B",
  "page.abx.abort": "中断する（Esc）",
  "page.abx.again": "もう一度",
  "page.abx.close": "閉じる",
  // The output check
  "page.output.heading": "出力の確認",
  "page.output.pathLabel": "経路",
  "page.output.path": "経路",
  "page.output.ispOn": "ISP セーフをオンにする（計測した値で下げる）",
  "page.output.verdict": "BIT-PERFECT の判定",
  "page.output.abx": "ABX で聴き比べる…",
  "page.output.abxNote": "今の曲で、原音と音作り（または 16bit）を聞き分けられるかを確かめます。",
  "page.output.input": "入力",
  "page.output.integrity": "ファイルの検証",
  "page.output.device": "出力デバイス",
  "page.output.mode": "出力の方式",
  "page.output.windowsVolume": "Windows の音量",
  "page.output.format": "送る形式",
  "page.output.formatRateChange": "{format}（{change}）",
  "page.output.repack": "詰め替え",
  "page.output.processing": "処理",
  "page.output.processingPlaceholder": "なし（原音経路：音量・変換・ディザをかけません）", // until main.ts draws the status
  "page.output.bitPerfect": "ビットパーフェクト",
  "page.output.selfCheck": "自己確認",
  "page.output.transitions": "曲の切り替え",
  "page.output.dropouts": "音切れ",
  "page.output.priority": "再生スレッド",
  "page.output.meters": "メーター",
  "page.output.loudness": "ラウドネス",
  "page.output.momentary": "瞬時（0.4 秒）",
  "page.output.shortTerm": "短時間（3 秒）",
  "page.output.integrated": "この曲の統合値",

  // --- a knob (knob.ts) ---
  "page.knob.title": "{label}：上下にドラッグ（Shift で細かく）、ホイール、ダブルクリックで既定値",
  "page.knob.typeValue": "クリックして数値を入力",
  "page.knob.valueLabel": "{label}の値",

  // --- the file dialogs' titles (task 11-3): the page gives them to the app ---
  "page.chooser.openFiles": "音声ファイルを開く（複数選べます）",
  "page.chooser.addFolder": "音楽のフォルダーを選ぶ（中のフォルダーも含めて監視します）",
  "page.chooser.ir": "空間の IR のファイルを選ぶ（WAV・FLAC・AIFF、モノラルかステレオ）",
  "page.chooser.eq": "EQ の設定を読み込む（AutoEQ の ParametricEQ.txt、Equalizer APO）",
} as const;

export const en: Record<keyof typeof ja, string> = { ...ja };
