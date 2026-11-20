// The page's texts in Japanese (task 11-1; CLAUDE.md: 多言語化). The
// English table (en.ts) has every key of this one: TypeScript checks it.
//
// Keys: where the text is shown, then what it is ("settings.tab.output").
// Values: {name} is put in by t() (i18n.ts). Describe what the player does;
// never claim that something sounds better (CLAUDE.md chapter 1).

import * as about from "./parts/about";
import * as controls from "./parts/controls";
import * as native from "./parts/native";
import * as page from "./parts/page";
import * as reader from "./parts/reader";
import * as status from "./parts/status";

// The parts each part of the page moves its texts into (task 11-2), the
// engine's and the app's messages (task 11-3), and the article reader's
// words (task 11-5), and the settings' About tab (task 8-2).
export const PARTS = { page, status, controls, native, reader, about };

const base = {
  // The settings screen's tabs (settingsTabs.ts).
  "settings.tab.output": "出力",
  "settings.tab.mode": "モード",
  "settings.tab.level": "レベル",
  "settings.tab.eq": "EQ・トーン",
  "settings.tab.tube": "真空管",
  "settings.tab.space": "空間",
  "settings.tab.look": "見た目",
  "settings.tab.about": "情報",

  // The language (settings, 見た目). Its heading reads in both languages, so
  // that someone who cannot read the one shown still finds it.
  "settings.look.language": "言語 / Language",
  "settings.look.languageNote": "画面の言語です。初めは Windows の表示言語に合わせます（日本語のとき日本語、それ以外は英語）。",
  // Each language in its own words, whichever is shown.
  "lang.ja": "日本語",
  "lang.en": "English",

  // Between the items of a list in one line ("出力、出力レート").
  "list.separator": "、",
} as const;

export const BASE_JA = base;

export const ja = { ...base, ...page.ja, ...status.ja, ...controls.ja, ...native.ja, ...reader.ja, ...about.ja } as const;

export type MessageKey = keyof typeof ja;
export type Messages = Record<MessageKey, string>;
