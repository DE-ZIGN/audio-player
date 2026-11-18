// The settings screen's tabs (2026-11-18, the user): the tabs down the left,
// one tab's sections on the right, so the whole of the settings is in sight
// at once. A section is one heading of the page (index.html: a
// .settings-group, named by data-group); a tab shows one or more of them.
//
// How the sections are grouped is decided here only: move a name to another
// tab, or add a tab, and the page follows (settingsTabs.test.ts checks that
// every section is in exactly one tab).

export interface SettingsTab {
  id: string;
  label: string;
  sections: readonly string[]; // data-group names, shown in the page's order
}

export const SETTINGS_TABS: readonly SettingsTab[] = [
  { id: "output", label: "出力", sections: ["output", "output-rate"] },
  { id: "mode", label: "モード", sections: ["mode"] },
  { id: "level", label: "レベル", sections: ["isp-safe", "replaygain"] },
  { id: "eq", label: "EQ・トーン", sections: ["eq", "tone"] },
  { id: "tube", label: "真空管", sections: ["tube"] },
  { id: "space", label: "空間", sections: ["space", "crossfeed"] },
  { id: "look", label: "見た目", sections: ["look", "articles"] },
];

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
