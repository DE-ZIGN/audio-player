// The page's texts in English (task 11-1). Every key of the Japanese table
// (ja.ts), with the same {values}. Keep chapter 1 of CLAUDE.md here too: say
// what the player does, never that the sound gets better (i18n.test.ts looks
// for such words).

import type { BASE_JA, Messages } from "./ja";
import * as controls from "./parts/controls";
import * as native from "./parts/native";
import * as page from "./parts/page";
import * as reader from "./parts/reader";
import * as status from "./parts/status";

const base: Record<keyof typeof BASE_JA, string> = {
  "settings.tab.output": "Output",
  "settings.tab.mode": "Mode",
  "settings.tab.level": "Level",
  "settings.tab.eq": "EQ / Tone",
  "settings.tab.tube": "Tubes",
  "settings.tab.space": "Space",
  "settings.tab.look": "Look",

  "settings.look.language": "言語 / Language",
  "settings.look.languageNote":
    "The language of the screens. At first it follows the Windows display language (Japanese for Japanese, English for any other).",
  "lang.ja": "日本語",
  "lang.en": "English",

  "list.separator": ", ",
};

export const en: Messages = { ...base, ...page.en, ...status.en, ...controls.en, ...native.en, ...reader.en };
