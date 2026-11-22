import { describe, expect, it } from "vitest";
import listJson from "../../third_party/components.json?raw";
import notices from "../../THIRD_PARTY_NOTICES.md?raw";
import dependencies from "../../cmake/Dependencies.cmake?raw";
import testsCmake from "../../tests/CMakeLists.txt?raw";
import aboutCmake from "../../cmake/AboutData.cmake?raw";
import appCmake from "../../app/CMakeLists.txt?raw";
import license from "../../LICENSE?raw";
import aboutView from "./aboutView.ts?raw";
import html from "../index.html?raw";
import {
  ABOUT_DATA,
  APP_LICENSE,
  APP_LICENSE_URL,
  APP_SOURCE_URL,
  bundledTree,
  loadAboutData,
  loadLicenseText,
  NOTE_KEYS,
  parseAboutData,
  type Fetcher,
} from "./about";
import { ja } from "./i18n/ja";
import { en } from "./i18n/en";

// Task 8-2: the licenses screen (settings, About). Its list is one file,
// third_party/components.json; the build copies each license text next to the
// page with one about.json (cmake/AboutData.cmake). These tests keep the list
// in step with THIRD_PARTY_NOTICES.md (the human-readable list) and with the
// components the build fetches.

interface ListText {
  fetch?: string;
  path: string;
}
interface ListEntry {
  id: string;
  name: string;
  version: string;
  license: string;
  url: string;
  copyright: string[];
  parent?: string;
  fetch?: string;
  note?: string;
  texts: ListText[];
}
const list = JSON.parse(listJson) as { app: { texts: ListText[] }; components: ListEntry[] };

// The components the build and the tests use but do not distribute.
const BUILD_ONLY = ["Catch2", "lame"];

// THIRD_PARTY_NOTICES.md: one section per component ("## name" or, bundled in
// the one above, "### name"), its items as "- 項目：値".
interface Section {
  level: number;
  name: string; // the heading before its parenthesis
  parent: string | null; // the "##" section a "###" one is under
  items: Record<string, string>;
}
function sections(text: string): Section[] {
  const out: Section[] = [];
  let top: string | null = null;
  for (const line of text.split(/\r?\n/)) {
    const h = /^(##|###) (.+)$/.exec(line);
    if (h) {
      const name = h[2].split("（")[0].trim();
      const level = h[1].length;
      if (level === 2) top = name;
      out.push({ level, name, parent: level === 3 ? top : null, items: {} });
      continue;
    }
    const item = /^- ([^：]+)：(.*)$/.exec(line);
    if (item && out.length > 0) out[out.length - 1].items[item[1]] ??= item[2].replace(/`/g, "").trim();
  }
  return out;
}
const SECTIONS = sections(notices);
const distributed = SECTIONS.filter((s) => s.items["配布物"]?.startsWith("含む"));
const notDistributed = SECTIONS.filter((s) => s.items["配布物"]?.startsWith("含まない"));

// The notices' line starts with the list's value, as a whole word.
const startsWithWord = (line: string | undefined, value: string) =>
  line !== undefined && line.startsWith(value) && !/^[A-Za-z0-9.+-]/.test(line.slice(value.length));

const byName = new Map(list.components.map((c) => [c.name, c]));
const byId = new Map(list.components.map((c) => [c.id, c]));

const declared = [...`${dependencies}\n${testsCmake}`.matchAll(/FetchContent_Declare\(\s*([A-Za-z0-9_]+)/g)].map((m) => m[1]);

// The repository's license texts the list names, as text (the build copies them).
const repoTexts = import.meta.glob(["../../LICENSE", "../../third_party/**/LICENSE*", "../../third_party/licenses/*"], {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>;

// What the build makes of the list (cmake/AboutData.cmake): each text copied as <id>-<n>.txt.
const baseName = (p: string) => p.split("/").at(-1)!;
const built = {
  app: { name: "DE:ZIGN AUDIO PLAYER", version: "1.0.1", texts: list.app.texts.map((t, i) => ({ file: `app-${i + 1}.txt`, name: baseName(t.path) })) },
  components: list.components.map((c) => ({
    ...c,
    texts: c.texts.map((t, i) => ({ file: `${c.id}-${i + 1}.txt`, name: baseName(t.path) })),
  })),
};

describe("the list of components (third_party/components.json)", () => {
  it("each entry is complete: an id fit for a file name, its name, version, SPDX license, address, copyright and texts", () => {
    expect(list.components.length).toBeGreaterThanOrEqual(15);
    expect(new Set(list.components.map((c) => c.id)).size).toBe(list.components.length);
    for (const c of list.components) {
      expect(c.id, c.name).toMatch(/^[a-z0-9]+(?:-[a-z0-9]+)*$/);
      expect(c.id).not.toBe("app"); // the app's own texts are app-<n>.txt
      for (const v of [c.name, c.version, c.license]) expect(v.trim().length, c.id).toBeGreaterThan(0);
      expect(c.license, c.id).toMatch(/^(?:LicenseRef-)?[A-Za-z0-9.+-]+(?: (?:AND|OR) [A-Za-z0-9.+-]+)*$/);
      expect(c.url, c.id).toMatch(/^https?:\/\/[^\s]+$/);
      expect(c.copyright.length, c.id).toBeGreaterThan(0);
      expect(c.texts.length, c.id).toBeGreaterThan(0);
      for (const t of c.texts) expect(t.path, c.id).toMatch(/^[A-Za-z0-9._-]+(?:\/[A-Za-z0-9._-]+)*$/);
    }
  });
  it("a bundled component comes after the one it is in, which is not itself bundled", () => {
    list.components.forEach((c, i) => {
      if (!c.parent) return;
      const parent = list.components.findIndex((p) => p.id === c.parent);
      expect(parent, c.id).toBeGreaterThanOrEqual(0);
      expect(parent, c.id).toBeLessThan(i);
      expect(list.components[parent].parent, c.id).toBeUndefined();
    });
  });
  it("its notes are keys of the tables, and every note key is used", () => {
    const notes = list.components.flatMap((c) => (c.note ? [c.note] : []));
    for (const n of notes) expect(NOTE_KEYS as readonly string[], n).toContain(n);
    expect([...NOTE_KEYS].sort()).toEqual([...new Set(notes)].sort());
  });
  it("says what the licenses ask: ALAC and the IR changed, mpg123 replaceable", () => {
    expect(byId.get("alac")?.note).toBe("about.note.alac");
    expect(byId.get("mair")?.note).toBe("about.note.mair");
    expect(byId.get("mpg123")?.note).toBe("about.note.mpg123");
    expect(ja["about.note.alac"]).toMatch(/変更/);
    expect(ja["about.note.mair"]).toMatch(/変更/);
    expect(ja["about.note.mpg123"]).toMatch(/mpg123\.dll/);
    expect(ja["about.note.mpg123"]).toMatch(/差し替え/);
    expect(en["about.note.alac"]).toMatch(/^Modified/);
    expect(en["about.note.mair"]).toMatch(/^Modified/);
    expect(en["about.note.mpg123"]).toMatch(/replace/);
  });
  it("the texts in this repository are there, and not empty", () => {
    const inRepo = [...list.app.texts, ...list.components.flatMap((c) => c.texts)].filter((t) => !t.fetch);
    expect(inRepo.length).toBeGreaterThan(0);
    for (const t of inRepo) expect(repoTexts[`../../${t.path}`]?.trim().length ?? 0, t.path).toBeGreaterThan(100);
  });
  it("the app's own text is the GNU AGPL v3 (LICENSE), and so is the license it names", () => {
    expect(list.app.texts).toEqual([{ path: "LICENSE" }]);
    expect(license.trimStart()).toMatch(/^GNU AFFERO GENERAL PUBLIC LICENSE\s+Version 3, 19 November 2007/);
    expect(APP_LICENSE).toBe("AGPL-3.0-only");
    expect(APP_LICENSE_URL).toBe("https://www.gnu.org/licenses/agpl-3.0.html");
    expect(APP_SOURCE_URL).toBe("https://github.com/DE-ZIGN/audio-player");
  });
  it("the licenses with a separate full text carry it (GPLv3 for ASIO, AGPLv3 for JUCE, CC BY 4.0 for the IR)", () => {
    expect(repoTexts["../../third_party/licenses/GPL-3.0.txt"]).toMatch(/^\s*GNU GENERAL PUBLIC LICENSE\s+Version 3, 29 June 2007/);
    expect(byId.get("asiosdk")?.texts.map((t) => t.path)).toContain("third_party/licenses/GPL-3.0.txt");
    expect(byId.get("juce")?.texts.map((t) => t.path)).toContain("LICENSE");
    expect(repoTexts["../../third_party/licenses/CC-BY-4.0.txt"]).toMatch(/Creative Commons Attribution 4\.0 International Public License/);
    expect(repoTexts["../../third_party/licenses/CC-BY-4.0.txt"]).toMatch(/Section 8 -- Interpretation\./);
  });
});

describe("the list against THIRD_PARTY_NOTICES.md", () => {
  it("every component distributed there (含む) is in the list, with the same version and license", () => {
    expect(distributed.length).toBeGreaterThanOrEqual(15);
    for (const s of distributed) {
      const c = byName.get(s.name);
      expect(c, `${s.name}: not in third_party/components.json`).toBeDefined();
      expect(startsWithWord(s.items["バージョン"], c!.version), `${s.name}: バージョン「${s.items["バージョン"]}」 vs ${c!.version}`).toBe(true);
      expect(startsWithWord(s.items["ライセンス"], c!.license), `${s.name}: ライセンス「${s.items["ライセンス"]}」 vs ${c!.license}`).toBe(true);
    }
  });
  it("and the other way round: every entry of the list has its section there, marked 含む", () => {
    const names = new Set(distributed.map((s) => s.name));
    expect(list.components.map((c) => c.name).filter((n) => !names.has(n))).toEqual([]);
  });
  it("a part bundled in another (zlib in JUCE, utfcpp in TagLib, ...) is a ### section under that one's", () => {
    for (const s of distributed) {
      const c = byName.get(s.name)!;
      if (s.level === 2) expect(c.parent, s.name).toBeUndefined();
      else expect(c.parent, s.name).toBe(byName.get(s.parent ?? "")?.id);
    }
    expect(byId.get("zlib")?.parent).toBe("juce");
    expect(byId.get("utfcpp")?.parent).toBe("taglib");
    expect(byId.get("ooura-fft")?.parent).toBe("r8brain");
  });
  it("the build- and test-only ones are not in the list", () => {
    expect(notDistributed.map((s) => s.name).sort()).toEqual(["Catch2", "LAME"]);
    for (const s of notDistributed) expect(byName.has(s.name), s.name).toBe(false);
    for (const tool of ["Vite", "TypeScript", "Vitest", "Catch2", "LAME"]) expect(byName.has(tool), tool).toBe(false);
  });
  it("its rules say what the list is, and that the two change together", () => {
    const rules = notices.slice(notices.indexOf("## 書き方のルール"), notices.indexOf("---"));
    expect(rules).toMatch(/third_party\/components\.json/);
    expect(rules).toMatch(/同じコミット/);
    expect(rules).toMatch(/about\.test\.ts/);
  });
});

describe("the list against what the build fetches (FetchContent_Declare)", () => {
  it("every component fetched is in the list, or named as build- or test-only", () => {
    expect(declared.length).toBeGreaterThanOrEqual(10);
    const fetched = new Set(list.components.flatMap((c) => (c.fetch ? [c.fetch] : [])));
    expect(declared.filter((d) => !fetched.has(d) && !BUILD_ONLY.includes(d))).toEqual([]);
    for (const b of BUILD_ONLY) expect(declared, b).toContain(b);
  });
  it("every fetch the list names is declared", () => {
    const named = list.components.flatMap((c) => [...(c.fetch ? [c.fetch] : []), ...c.texts.flatMap((t) => (t.fetch ? [t.fetch] : []))]);
    expect(named.filter((n) => !declared.includes(n))).toEqual([]);
    expect(named.filter((n) => BUILD_ONLY.includes(n))).toEqual([]);
  });
});

describe("the build makes the screen's data (cmake/AboutData.cmake)", () => {
  it("reads the list, copies the texts as <id>-<n>.txt with about.json, and stops when a text is missing", () => {
    expect(aboutCmake).toMatch(/third_party\/components\.json/);
    expect(aboutCmake).toMatch(/string\(JSON/);
    expect(aboutCmake).toMatch(/FetchContent_GetProperties/);
    expect(aboutCmake).toMatch(/message\(FATAL_ERROR/);
    expect(aboutCmake).toMatch(/about\.json/);
    expect(aboutCmake).toMatch(/\$\{prefix\}-\$\{number\}\.txt/);
    expect(aboutCmake).toMatch(/CMAKE_CONFIGURE_DEPENDS/);
  });
  it("the app puts it next to the built page, in ui/about (after the page's own files)", () => {
    expect(appCmake).toMatch(/ap_make_about_data\(/);
    const copyUi = appCmake.indexOf('copy_directory "${AP_UI_DIST_DIR}"');
    const copyAbout = appCmake.indexOf('copy_directory "${AP_ABOUT_DIR}" "$<TARGET_FILE_DIR:ap_app>/ui/about"');
    expect(copyUi).toBeGreaterThan(0);
    expect(copyAbout).toBeGreaterThan(copyUi);
    expect(ABOUT_DATA).toBe("about/about.json");
  });
});

describe("reading the data (about.ts)", () => {
  const ok = (body: unknown): Fetcher => async () =>
    ({ ok: true, status: 200, json: async () => body, text: async () => String(body) }) as Response;
  it("reads what the build makes", () => {
    const data = parseAboutData(JSON.parse(JSON.stringify(built)));
    expect(data.app.name).toBe("DE:ZIGN AUDIO PLAYER");
    expect(data.components.length).toBe(list.components.length);
    expect(data.components.find((c) => c.id === "juce")?.texts).toEqual([
      { file: "juce-1.txt", name: "LICENSE.md" },
      { file: "juce-2.txt", name: "LICENSE" },
    ]);
  });
  it("refuses data of another shape (no list, a file name that leaves the folder, an unknown note)", () => {
    expect(() => parseAboutData(null)).toThrow();
    expect(() => parseAboutData({ app: built.app })).toThrow();
    const bad = (change: (c: (typeof built.components)[number]) => void) => {
      const copy = JSON.parse(JSON.stringify(built));
      change(copy.components[0]);
      return () => parseAboutData(copy);
    };
    expect(bad((c) => (c.texts[0].file = "../index.html"))).toThrow();
    expect(bad((c) => (c.texts = []))).toThrow();
    expect(bad((c) => (c.note = "about.note.nothing"))).toThrow();
    expect(bad((c) => (c.copyright = "one line" as unknown as string[]))).toThrow();
    expect(bad((c) => (c.parent = "no-such-id"))).toThrow();
  });
  it("the development server has no data: says so, without asking for it", async () => {
    let asked = 0;
    const get: Fetcher = async () => {
      asked++;
      throw new Error("not asked");
    };
    expect(await loadAboutData("mock", get)).toEqual({ state: "none" });
    expect(asked).toBe(0);
  });
  it("in the app: the data, or what went wrong (never a throw)", async () => {
    const loaded = await loadAboutData("app", ok(JSON.parse(JSON.stringify(built))));
    expect(loaded.state).toBe("ok");
    const missing = await loadAboutData("app", async () => ({ ok: false, status: 404 }) as Response);
    expect(missing).toEqual({ state: "error", detail: "HTTP 404" });
    const broken = await loadAboutData("app", ok({ nothing: true }));
    expect(broken.state).toBe("error");
    const failed = await loadAboutData("app", async () => {
      throw new Error("offline");
    });
    expect(failed).toEqual({ state: "error", detail: "Error: offline" });
  });
  it("reads a license text from the folder, and refuses an empty one", async () => {
    const asked: string[] = [];
    const get = (body: string): Fetcher => async (url) => {
      asked.push(String(url));
      return { ok: true, status: 200, text: async () => body } as Response;
    };
    expect(await loadLicenseText("zlib-1.txt", get("Permission is granted"))).toBe("Permission is granted");
    expect(asked).toEqual(["about/zlib-1.txt"]);
    await expect(loadLicenseText("zlib-1.txt", get("  \n"))).rejects.toThrow(/empty/);
    await expect(loadLicenseText("../secret.txt", get("x"))).rejects.toThrow();
    await expect(loadLicenseText("zlib-1.txt", async () => ({ ok: false, status: 404 }) as Response)).rejects.toThrow(/404/);
  });
  it("puts each bundled part under the one it is in, in the list's order", () => {
    const tree = bundledTree(parseAboutData(JSON.parse(JSON.stringify(built))).components);
    expect(tree.map((n) => n.component.id)).toEqual(list.components.filter((c) => !c.parent).map((c) => c.id));
    expect(tree.find((n) => n.component.id === "juce")?.bundled.map((c) => c.id)).toEqual(["zlib", "libpng", "ijg", "harfbuzz", "sheenbidi"]);
    expect(tree.find((n) => n.component.id === "taglib")?.bundled.map((c) => c.id)).toEqual(["utfcpp"]);
  });
});

describe("the tab (aboutView.ts, index.html)", () => {
  it("sets the texts as text, never as HTML (license texts have < and &)", () => {
    const code = aboutView.replace(/\/\/.*$/gm, "");
    expect(code).not.toMatch(/innerHTML|outerHTML|insertAdjacentHTML/);
    expect(code).toMatch(/el\("pre"/);
    expect(code).toMatch(/el\("details"/);
    expect(code).toMatch(/pre\.textContent = body/);
  });
  it("is a group of the settings page, with its heading", () => {
    expect(html).toMatch(/<div class="settings-group" data-group="about">[\s\S]*?<h2[^>]*data-i18n="about\.heading"/);
    expect(html).toMatch(/<div class="settings-group" data-group="components">[\s\S]*?<h2[^>]*data-i18n="about\.components\.heading"/);
  });
  it("the trademark: ASIO, as THIRD_PARTY_NOTICES.md writes it; no word of VST", () => {
    const asio = SECTIONS.find((s) => s.name === "Steinberg ASIO SDK")!.items["商標"];
    expect(ja["about.trademark.asio"]).toBe(asio);
    expect(en["about.trademark.asio"]).toMatch(/^ASIO is a trademark of Steinberg Media Technologies GmbH\./);
    const words = Object.entries({ ...ja, ...en })
      .filter(([k]) => k.startsWith("about."))
      .map(([, v]) => v)
      .join("\n");
    expect(words).not.toMatch(/VST/);
  });
});
