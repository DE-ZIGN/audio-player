# 標準ビューの部品の画像（タスク 6-8）

画像生成（作業フォルダーの `imagegen`、Codex の画像生成）で、このプロジェクトのために作った画像です。
注文書は作業フォルダーの `imagegen/std_parts_orders.py`（STD-WOOD、STD-METAL、STD-PANEL、STD-VU-FACE、STD-BUTTON、STD-PLAY）。
`tools/ui_textures.py` で、つなぎ目の処理（木目・地・金属）、ボタンの切り抜き、縮小、JPEG への変換をしています。

どの画像にも文字、数字、目盛り、ロゴは入っていません（CLAUDE.md 8章：文字や目盛りはプログラムで描く）。

| ファイル | 使う場所 |
|---|---|
| wood.jpg | 標準ビューの木枠（外周） |
| metal.jpg | 上部の再生バー |
| panel.jpg | 左右の欄と本文の地 |
| vu-face.jpg | VU メーターの盤面（目盛り・数字・針は SVG で重ねる） |
| button.jpg | 前へ・停止・次へのボタン |
| play.jpg | 再生ボタン |
