// The licenses screen's data (task 8-2; settings, 情報 / About). The build
// makes it from the list of distributed components (third_party/components.json,
// cmake/AboutData.cmake): ui/about/about.json, with each license text copied
// beside it as <id>-<n>.txt (the app's own as app-<n>.txt). The development
// server has none of it: the tab says so and shows what it can.

import type { MessageKey } from "./i18n";

export const ABOUT_DIR = "about/";
export const ABOUT_DATA = `${ABOUT_DIR}about.json`;

// The app's own license and source (CLAUDE.md chapter 1: AGPLv3, the source public).
export const APP_LICENSE = "AGPL-3.0-only";
export const APP_LICENSE_URL = "https://www.gnu.org/licenses/agpl-3.0.html";
export const APP_SOURCE_URL = "https://github.com/DE-ZIGN/audio-player";

// What a component's entry may note (its "note"), where its license asks for
// it: a change made to it, a part the user may replace, the license chosen.
export const NOTE_KEYS = [
  "about.note.juce",
  "about.note.asio",
  "about.note.alac",
  "about.note.mpg123",
  "about.note.taglib",
  "about.note.mair",
] as const satisfies readonly MessageKey[];
export type NoteKey = (typeof NOTE_KEYS)[number];

export interface LicenseText {
  file: string; // in ABOUT_DIR
  name: string; // the file's name in its component ("LICENSE.md", "COPYING.LGPL")
}

export interface AboutComponent {
  id: string;
  name: string;
  version: string;
  license: string; // SPDX
  url: string;
  copyright: string[];
  parent?: string; // the id of the component it is bundled in
  note?: NoteKey;
  texts: LicenseText[];
}

export interface AboutData {
  app: { name: string; version: string; texts: LicenseText[] };
  components: AboutComponent[];
}

export type AboutState = { state: "ok"; data: AboutData } | { state: "none" } | { state: "error"; detail: string };

export type Fetcher = (url: string, init?: RequestInit) => Promise<Response>;

const fail = (what: string): never => {
  throw new Error(`about.json: ${what}`);
};
const isText = (v: unknown): v is string => typeof v === "string" && v.trim().length > 0;
const field = (o: Record<string, unknown>, name: string, where: string): string =>
  isText(o[name]) ? (o[name] as string) : fail(`${where}: no ${name}`);
const object = (v: unknown, where: string): Record<string, unknown> =>
  typeof v === "object" && v !== null && !Array.isArray(v) ? (v as Record<string, unknown>) : fail(`${where} is not an object`);

// A copied text's name: a plain file name in the folder, nothing that leaves it.
const TEXT_FILE = /^[a-z0-9]+(?:-[a-z0-9]+)*\.txt$/;

function texts(v: unknown, where: string): LicenseText[] {
  if (!Array.isArray(v) || v.length === 0) return fail(`${where}: no license texts`);
  return v.map((t, i) => {
    const o = object(t, `${where} text ${i + 1}`);
    const file = field(o, "file", where);
    if (!TEXT_FILE.test(file)) fail(`${where}: bad file name ${file}`);
    return { file, name: field(o, "name", where) };
  });
}

// Checks the data's shape (the page shows nothing it cannot trust the form of).
export function parseAboutData(value: unknown): AboutData {
  const root = object(value, "the data");
  const app = object(root.app, "app");
  if (!Array.isArray(root.components) || root.components.length === 0) fail("no components");
  const components = (root.components as unknown[]).map((v, i): AboutComponent => {
    const o = object(v, `component ${i + 1}`);
    const id = field(o, "id", `component ${i + 1}`);
    const copyright = o.copyright;
    if (!Array.isArray(copyright) || copyright.length === 0 || !copyright.every(isText)) fail(`${id}: no copyright lines`);
    const c: AboutComponent = {
      id,
      name: field(o, "name", id),
      version: field(o, "version", id),
      license: field(o, "license", id),
      url: field(o, "url", id),
      copyright: copyright as string[],
      texts: texts(o.texts, id),
    };
    if (o.parent !== undefined) c.parent = field(o, "parent", id);
    if (o.note !== undefined) {
      const note = field(o, "note", id);
      if (!(NOTE_KEYS as readonly string[]).includes(note)) fail(`${id}: unknown note ${note}`);
      c.note = note as NoteKey;
    }
    return c;
  });
  const ids = new Set(components.map((c) => c.id));
  if (ids.size !== components.length) fail("an id twice");
  for (const c of components) if (c.parent !== undefined && !ids.has(c.parent)) fail(`${c.id}: no component ${c.parent}`);
  return {
    app: { name: field(app, "name", "app"), version: field(app, "version", "app"), texts: texts(app.texts, "app") },
    components,
  };
}

// The data as the tab reads it: none in the browser's mock (nothing is asked
// for: Vite would answer with its page), else the data or what went wrong.
export async function loadAboutData(kind: "app" | "mock", get: Fetcher = fetch): Promise<AboutState> {
  if (kind !== "app") return { state: "none" };
  try {
    const r = await get(ABOUT_DATA, { cache: "no-store" });
    if (!r.ok) return { state: "error", detail: `HTTP ${r.status}` };
    return { state: "ok", data: parseAboutData(await r.json()) };
  } catch (e) {
    return { state: "error", detail: String(e) };
  }
}

// One license text; an empty or missing one is an error (a text must be shown in full).
export async function loadLicenseText(file: string, get: Fetcher = fetch): Promise<string> {
  if (!TEXT_FILE.test(file)) throw new Error(`bad file name ${file}`);
  const r = await get(`${ABOUT_DIR}${file}`, { cache: "no-store" });
  if (!r.ok) throw new Error(`${file}: HTTP ${r.status}`);
  const text = await r.text();
  if (text.trim().length === 0) throw new Error(`${file}: empty`);
  return text;
}

// The components as shown: each one with the parts bundled in it under it.
export function bundledTree(components: readonly AboutComponent[]): { component: AboutComponent; bundled: AboutComponent[] }[] {
  return components
    .filter((c) => c.parent === undefined)
    .map((component) => ({ component, bundled: components.filter((c) => c.parent === component.id) }));
}
