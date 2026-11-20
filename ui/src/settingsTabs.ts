// The settings screen's tabs (2026-11-18, the user): the tabs down the left,
// one tab's sections on the right, so the whole of the settings is in sight
// at once. A section is one heading of the page (index.html: a
// .settings-group, named by data-group); a tab shows one or more of them.
//
// How the sections are grouped is decided here only: move a name to another
// tab, or add a tab, and the page follows (settingsTabs.test.ts checks that
// every section is in exactly one tab, or set aside below).

import type { MessageKey } from "./i18n";

export interface SettingsTab {
  id: string;
  labelKey: MessageKey; // the tab's name, in the tables (i18n/ja.ts)
  sections: readonly string[]; // data-group names, shown in the page's order
}

export const SETTINGS_TABS: readonly SettingsTab[] = [
  { id: "output", labelKey: "settings.tab.output", sections: ["output", "output-rate"] },
  { id: "mode", labelKey: "settings.tab.mode", sections: ["mode"] },
  { id: "level", labelKey: "settings.tab.level", sections: ["isp-safe", "replaygain"] },
  { id: "eq", labelKey: "settings.tab.eq", sections: ["eq", "tone"] },
  { id: "tube", labelKey: "settings.tab.tube", sections: ["tube"] },
  { id: "space", labelKey: "settings.tab.space", sections: ["space", "crossfeed"] },
  { id: "look", labelKey: "settings.tab.look", sections: ["look"] },
  // The app, its trademarks and the components' licenses (task 8-2; the user, 2026-11-20).
  { id: "about", labelKey: "settings.tab.about", sections: ["about", "components"] },
];

// Sections that stay in the page but are in no tab, so they are never shown
// (the tabs' view hides every group that is not in the chosen tab).
// "articles": the article reader is put off (M7; the user, 2026-11-19), so
// its entry (記事を開く…) is out of sight until there are articles to read.
// The reader itself (task 7-1) is still built and tested. To bring the entry
// back, move the name into the look tab's sections above.
export const HIDDEN_SECTIONS: readonly string[] = ["articles"];

export const SETTINGS_TAB_KEY = "settingsTab"; // the tab last used (localStorage)

export function tabOfSection(section: string): string | null {
  return SETTINGS_TABS.find((t) => t.sections.includes(section))?.id ?? null;
}

// The tab an arrow key, Home or End moves to; null for any other key.
export function tabByKey(current: string, key: string): string | null {
  const ids = SETTINGS_TABS.map((t) => t.id);
  const i = ids.indexOf(current);
  switch (key) {
    case "ArrowDown":
    case "ArrowRight":
      return ids[(i + 1) % ids.length];
    case "ArrowUp":
    case "ArrowLeft":
      return i < 0 ? ids[0] : ids[(i - 1 + ids.length) % ids.length];
    case "Home":
      return ids[0];
    case "End":
      return ids[ids.length - 1];
    default:
      return null;
  }
}

export function savedSettingsTab(value: string | null): string {
  return SETTINGS_TABS.some((t) => t.id === value) ? (value as string) : SETTINGS_TABS[0].id;
}
