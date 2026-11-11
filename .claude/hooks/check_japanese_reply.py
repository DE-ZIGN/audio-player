"""Stop hook: block a turn whose reply to the user is mostly English.

CLAUDE.md asks for every reply to be in Japanese (the closing message and
the wrap_up box included). This hook reads the session transcript, takes
the text Claude wrote for the user in the current turn (the last text
block and the wrap_up fields), and, if it reads as English, blocks the
stop and asks Claude to write it again in Japanese.

Code, paths, URLs and identifiers are removed before judging, since they
are allowed to be in English.

Run with:  python -X utf8 check_japanese_reply.py   (hook JSON on stdin)
Test with: python -X utf8 check_japanese_reply.py --judge FILE
"""

import json
import re
import sys

WRAP_UP = "mcp__ccd_turn__wrap_up"

KANA = re.compile(r"[぀-ヿｦ-ﾟ]")
KANJI = re.compile(r"[一-鿿]")
LATIN = re.compile(r"[A-Za-z]")

STRIP = [
    re.compile(r"```.*?```", re.S),  # fenced code
    re.compile(r"`[^`\n]*`"),  # inline code
    re.compile(r"\]\([^)]*\)"),  # markdown link targets
    re.compile(r"https?://\S+"),  # URLs
    re.compile(r"\S*[\\/]\S*"),  # paths
    re.compile(r"\b\w+\.(?:cpp|h|hpp|ts|js|json|md|py|css|html|txt|db|exe|dll|wav|flac|mp3)\b", re.I),
    re.compile(r"\b[\w-]*[_\d][\w-]*\b"),  # identifiers, task numbers
    re.compile(r"\b[a-z]+[A-Z]\w*\b"),  # camelCase
]


def prose(text: str) -> str:
    for p in STRIP:
        text = p.sub(" ", text)
    return text


def is_english(text: str) -> bool:
    """True when the prose reads as English rather than Japanese.

    Japanese prose always carries kana; English carries many Latin letters
    and almost no kana. Short replies (a few words) are not judged.
    """
    t = prose(text)
    kana = len(KANA.findall(t))
    kanji = len(KANJI.findall(t))
    latin = len(LATIN.findall(t))
    if latin < 40:
        return False
    # One Japanese character carries about as much as 3-4 Latin letters.
    japanese = kana + kanji
    return japanese * 4 < latin * 0.5


def turn_reply(lines):
    """The user-facing text of the current turn: the last text block and the
    wrap_up fields written after the latest user prompt."""
    last_text = None
    wrap = []
    for raw in reversed(lines):
        try:
            o = json.loads(raw)
        except ValueError:
            continue
        if o.get("isSidechain"):
            continue
        kind = o.get("type")
        content = (o.get("message") or {}).get("content")
        if kind == "user":
            if isinstance(content, str):
                break  # the user's prompt: the turn starts here
            if isinstance(content, list) and not o.get("isMeta"):
                if not any(b.get("type") == "tool_result" for b in content if isinstance(b, dict)):
                    break
            continue
        if kind != "assistant" or not isinstance(content, list):
            continue
        for b in reversed(content):
            if not isinstance(b, dict):
                continue
            if b.get("type") == "text" and last_text is None and b.get("text", "").strip():
                last_text = b["text"]
            elif b.get("type") == "tool_use" and b.get("name") == WRAP_UP and not wrap:
                inp = b.get("input") or {}
                wrap = [inp.get("recap", ""), inp.get("ask", "")]
                wrap += [a.get("label", "") for a in inp.get("actions", []) or [] if isinstance(a, dict)]
    return last_text, " ".join(w for w in wrap if w)


def main() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--judge":
        text = open(sys.argv[2], encoding="utf-8").read()
        print("english" if is_english(text) else "japanese")
        return 0

    data = json.load(sys.stdin)
    if data.get("stop_hook_active"):
        return 0  # already asked once this turn; do not loop
    path = data.get("transcript_path")
    if not path:
        return 0
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()
    text, wrap = turn_reply(lines)

    found = []
    if text and is_english(text):
        found.append("最後のまとめ（返答の本文）")
    if wrap and is_english(wrap):
        found.append("wrap_up の recap・ask・ボタン")
    if not found:
        return 0

    reason = (
        "返答が英語になっています（" + "、".join(found) + "）。"
        "CLAUDE.md の最優先の決まりにより、ユーザーへの返答はすべて日本語で書きます。"
        "同じ内容を日本語で書き直してください（コード、識別子、パスは英語のままでよい）。"
        "wrap_up が英語だった場合は、日本語で wrap_up を呼び直してから、日本語のまとめを書いてください。"
    )
    print(json.dumps({"decision": "block", "reason": reason}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
