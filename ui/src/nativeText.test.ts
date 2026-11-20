import { describe, expect, it } from "vitest";
import header from "../../app/logic/NativeMessages.h?raw";
import { MockBackend } from "./backend";
import { placeholders, setLang } from "./i18n";
import { en } from "./i18n/en";
import { ja } from "./i18n/ja";
import { MockSettings } from "./mockSettings";
import { nativeText } from "./nativeText";

// Task 11-3 (M11): the engine and the app send what happened, a code and its
// values (app/logic/NativeMessages.h); the page writes the sentence from its
// tables (i18n/parts/native.ts). The list there and the tables here must agree.

interface Declared {
  name: string;
  code: string;
  values: string[];
}

// The codes as the header declares them, one per line:
//   inline constexpr NativeCode name{"group.code", "value", ...};
function declaredCodes(): Declared[] {
  const out: Declared[] = [];
  for (const line of header.replace(/\r\n/g, "\n").split("\n")) {
    const m = /^\s*inline constexpr NativeCode (\w+)\{(.*)\};\s*$/.exec(line);
    if (!m) continue;
    const strings = [...m[2].matchAll(/"([^"]*)"/g)].map((s) => s[1]);
    out.push({ name: m[1], code: strings[0] ?? "", values: strings.slice(1) });
  }
  return out;
}

const DECLARED = declaredCodes();
const TABLES = { ja, en } as Record<"ja" | "en", Record<string, string>>;

// Every source file of the page (tests aside), as text.
const sources = import.meta.glob(["./**/*.ts", "!./**/*.test.ts", "!./i18n/**/*.ts"], {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>;

describe("the app's codes and the page's tables", () => {
  it("the header's list is read: every NativeCode line, none missed", () => {
    const lines = header.split("\n").filter((l) => /^\s*inline constexpr NativeCode\b/.test(l));
    expect(DECLARED.length).toBe(lines.length);
    expect(DECLARED.length).toBeGreaterThan(30);
    for (const d of DECLARED) expect(d.code, d.name).toMatch(/^[a-z][A-Za-z0-9]*(\.[a-z][A-Za-z0-9]*)+$/);
    expect(new Set(DECLARED.map((d) => d.code)).size).toBe(DECLARED.length);
  });

  it("every code has its key in both languages, with exactly the code's values", () => {
    for (const d of DECLARED)
      for (const lang of ["ja", "en"] as const) {
        const text = TABLES[lang][`native.${d.code}`];
        expect(text, `${lang} native.${d.code}`).toBeDefined();
        expect(placeholders(text), `${lang} native.${d.code}`).toEqual([...d.values].sort());
      }
  });

  it("the part has no key without a code", () => {
    const codes = new Set(DECLARED.map((d) => d.code));
    const keys = Object.keys(ja).filter((k) => k.startsWith("native."));
    expect(keys.filter((k) => !codes.has(k.slice("native.".length)))).toEqual([]);
    expect(keys.length).toBe(DECLARED.length);
  });

  it("the codes the page's scripts send (the mock engine) are declared", () => {
    const codes = new Set(DECLARED.map((d) => d.code));
    const used = new Set<string>();
    // Every "group.code" string after a `code:` on its line (a choice of two included).
    for (const text of Object.values(sources))
      for (const line of text.split("\n")) {
        const at = line.search(/\bcode: /);
        if (at < 0) continue;
        for (const m of line.slice(at).matchAll(/"([a-z][A-Za-z0-9]*(?:\.[a-z][A-Za-z0-9]*)+)"/g)) used.add(m[1]);
      }
    expect(used.size).toBeGreaterThanOrEqual(7);
    expect([...used].filter((c) => !codes.has(c))).toEqual([]);
  });
});

describe("nativeText", () => {
  it("writes the sentence of a code with its values, in the language in use", () => {
    setLang("ja");
    expect(nativeText({ code: "player.unreadable", values: { file: "a.xyz", detail: "unknown format" } })).toBe(
      "このファイルは読めません（WAV、FLAC、AIFF、ALAC・AAC（.m4a）、MP3、DSF、DFF に対応）: a.xyz (unknown format)",
    );
    expect(nativeText({ code: "eq.typeUnsupported", values: { line: "12", type: "BWLP" } })).toBe(
      "12 行目: 対応していない種類（BWLP）なので飛ばしました",
    );
    expect(nativeText({ code: "abx.alreadyRunning", values: {} })).toBe("ABX はもう始まっています");
    expect(nativeText({ code: "abx.alreadyRunning" })).toBe("ABX はもう始まっています");
    setLang("en");
    expect(nativeText({ code: "asio.driverMissing", values: { name: "X" } })).toBe(
      en["native.asio.driverMissing"].replace("{name}", "X"),
    );
    setLang("ja");
  });

  it("shows an unknown code itself (never empty), a plain string as it is, nothing as empty", () => {
    expect(nativeText({ code: "player.somethingNew", values: { detail: "d" } })).toBe("player.somethingNew (detail=d)");
    expect(nativeText({ code: "player.somethingNew" })).toBe("player.somethingNew");
    expect(nativeText("device error 0x88890004")).toBe("device error 0x88890004");
    expect(nativeText("")).toBe("");
    expect(nativeText(null)).toBe("");
    expect(nativeText(undefined)).toBe("");
  });
});

describe("the mock engine sends the app's codes", () => {
  it("ABX before anything plays", async () => {
    const r = await new MockBackend().abx.start({ kind: "processed", startSeconds: 0, seconds: 10, trials: 10 });
    expect(r.ok).toBe(false);
    expect(r.message).toEqual({ code: "abx.notPlaying" });
    expect(nativeText(r.message)).toBe("ABX で聴く曲を、先に再生してください");
  });

  it("the library with no track to play", async () => {
    const r = await new MockBackend().library.play([12345], 0);
    expect(nativeText(r.message)).toBe("再生できる曲がありません（ファイルが見つかりません）");
  });

  it("the settings: an ASIO driver that crashes, an EQ file's skipped line, an IR file that cannot be read", async () => {
    const s = new MockSettings();
    const asio = await s.setOutput({ kind: "asio", id: "{MOCK-CRASH}", name: "x", allowDop: false });
    expect(nativeText(asio.message)).toBe(
      "この ASIO ドライバーは使えません。読み込むと異常終了します（確かめるための別のプロセスで起きたので、このアプリは無事です）",
    );
    const eq = await s.importEq();
    expect((eq.warnings ?? []).map(nativeText)).toEqual(["6 行目: 対応していない種類（BWLP）なので飛ばしました"]);
    const info = await s.get();
    const set = await s.setListening({ ...info.listening, spaceIr: ["C:\\bad.wav", "", ""] });
    expect(nativeText(set.spaceIrs?.[0].error)).toBe("このファイルは IR として読めません（WAV・FLAC・AIFF などの PCM のファイルを選んでください）");
  });
});
