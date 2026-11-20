import { savedSettingsTab, SETTINGS_TAB_KEY, SETTINGS_TABS, tabByKey, tabOfSection } from "./settingsTabs";
import { onLangChange, t } from "./i18n";
import { currentUiView, onUiViewChange } from "./uiView";

// The settings screen's tabs on the page (settingsTabs.ts has what is in
// each). The tabs are buttons in a list down the left; choosing one shows
// its sections in the panel on the right and hides the others. Every control
// stays in the page whichever tab is shown, so nothing else needs to know.

function storage(): Storage | null {
  try {
    return localStorage;
  } catch {
    return null; // no storage: the first tab each time
  }
}

export class SettingsTabsView {
  private readonly list = document.getElementById("settings-tabs") as HTMLElement;
  private readonly panel = document.getElementById("settings-panes") as HTMLElement;
  private readonly groups = [...document.querySelectorAll<HTMLElement>("#settings .settings-group")];
  private readonly buttons = new Map<string, HTMLButtonElement>();
  private current = "";

  constructor() {
    for (const tab of SETTINGS_TABS) {
      const b = document.createElement("button");
      b.type = "button";
      b.id = `settings-tab-${tab.id}`;
      b.setAttribute("role", "tab");
      b.setAttribute("aria-controls", this.panel.id);
      b.dataset.tab = tab.id;
      b.addEventListener("click", () => this.show(tab.id));
      this.buttons.set(tab.id, b);
      this.list.append(b);
    }
    this.list.addEventListener("keydown", (e) => {
      const to = tabByKey(this.current, e.key);
      if (!to) return;
      e.preventDefault();
      this.show(to);
      this.buttons.get(to)?.focus();
    });
    this.name();
    onLangChange(() => this.name()); // the names, and the headings inside, in the language chosen
    this.show(savedSettingsTab(storage()?.getItem(SETTINGS_TAB_KEY) ?? null), false);
    // Down the left, or along the top in the amp view's short panel (style.css).
    const orient = () => this.list.setAttribute("aria-orientation", currentUiView() === "amp" ? "horizontal" : "vertical");
    onUiViewChange(orient);
    orient();
  }

  private name() {
    for (const tab of SETTINGS_TABS) {
      const b = this.buttons.get(tab.id);
      if (!b) continue;
      b.textContent = t(tab.labelKey);
      // What is inside, for a pointer resting on the tab.
      b.title = tab.sections.map((s) => this.headingOf(s)).filter(Boolean).join(t("list.separator"));
    }
  }

  get tab(): string {
    return this.current;
  }

  show(id: string, remember = true) {
    const tab = SETTINGS_TABS.find((t) => t.id === id) ?? SETTINGS_TABS[0];
    const changed = tab.id !== this.current;
    this.current = tab.id;
    for (const [name, b] of this.buttons) {
      const on = name === tab.id;
      b.setAttribute("aria-selected", String(on));
      b.tabIndex = on ? 0 : -1; // one stop in the Tab order; the arrow keys move between the tabs
    }
    for (const g of this.groups) g.hidden = !tab.sections.includes(g.dataset.group ?? "");
    this.panel.setAttribute("aria-labelledby", `settings-tab-${tab.id}`);
    if (changed) this.panel.scrollTop = 0;
    if (remember) {
      try {
        storage()?.setItem(SETTINGS_TAB_KEY, tab.id);
      } catch {
        // not kept
      }
    }
  }

  // Shows the tab that has this section and brings its heading into view
  // (a link in an article: "app:settings/eq").
  showSection(section: string) {
    const id = tabOfSection(section);
    if (!id) return;
    this.show(id);
    this.groups.find((g) => g.dataset.group === section)?.scrollIntoView();
  }

  private headingOf(section: string): string {
    const h = this.groups.find((g) => g.dataset.group === section)?.querySelector("h2");
    // The heading's own words, without the small note beside it.
    return [...(h?.childNodes ?? [])].filter((n) => n.nodeType === Node.TEXT_NODE).map((n) => n.textContent?.trim() ?? "").join("");
  }
}
