// What the engine and the app report (task 11-3, M11): a code and its values
// (app/logic/NativeMessages.h), turned into a sentence here, in the language
// in use, from the tables (the key "native.<code>", i18n/parts/native.ts).
//
// A plain string is shown as it is: a technical text (an exception's, in
// English), or a message from an app older than the page.

import { t, type MessageKey } from "./i18n";
import { ja } from "./i18n/ja";

export type NativeText = string | { code: string; values?: Record<string, string | number> };

function isMessage(m: unknown): m is { code: string; values?: Record<string, string | number> } {
  return typeof m === "object" && m !== null && typeof (m as { code?: unknown }).code === "string";
}

// The sentence for `m` ("" for nothing). An unknown code shows the code
// itself (never empty), with its values, so that it is still seen.
export function nativeText(m: NativeText | null | undefined): string {
  if (m === null || m === undefined) return "";
  if (typeof m === "string") return m;
  if (!isMessage(m)) return String(m);
  const values = m.values ?? {};
  const key = `native.${m.code}`;
  if (Object.prototype.hasOwnProperty.call(ja, key)) return t(key as MessageKey, values);
  const shown = Object.entries(values).map(([k, v]) => `${k}=${v}`);
  return shown.length > 0 ? `${m.code} (${shown.join(", ")})` : m.code;
}
