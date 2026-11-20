// The tube type by genre on the settings screen (task 9-7): on/off, which type
// plays for the current track and why, the current album's choice, the genre
// table (edited in place) and the list of album choices. Every change sends
// the whole of it to the app (settingsSetTubeAuto), which applies and saves it.

import type { SettingsApi } from "./backend";
import { onLangChange, t } from "./i18n";
import { nativeText, type NativeText } from "./nativeText";
import { TUBE_LABELS, type TubeType } from "./settings";
import type { PlaybackStatus } from "./status";
import {
  MAX_GENRE_LENGTH,
  choiceText,
  rulesProblem,
  withAlbumChoice,
  type TubeAutoSettings,
  type TubeAutoStatus,
  type TubeGenreRule,
} from "./tubeAuto";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const TEXT_SEND_MS = 500;

export class TubeAutoView {
  private settings: TubeAutoSettings = { enabled: false, rules: [], albums: [] };
  private defaults: TubeGenreRule[] = [];
  private status: TubeAutoStatus | null = null;
  private sendTimer: ReturnType<typeof setTimeout> | undefined;
  private shownNow = "";
  private saveFailed: { message?: NativeText } | null = null; // the app's answer to the last send, when it failed

  private readonly els = {
    enabled: $<HTMLInputElement>("tube-auto-enabled"),
    now: $("tube-auto-now"),
    albumRow: $("tube-auto-album-row"),
    albumName: $("tube-auto-album-name"),
    album: $<HTMLSelectElement>("tube-auto-album"),
    rules: $("tube-auto-rules"),
    ruleCount: $("tube-auto-rule-count"),
    add: $<HTMLButtonElement>("tube-auto-add"),
    reset: $<HTMLButtonElement>("tube-auto-reset"),
    problem: $("tube-auto-problem"),
    albums: $("tube-auto-albums"),
    albumCount: $("tube-auto-album-count"),
  };

  constructor(
    private readonly api: SettingsApi,
    private readonly manualType: () => TubeType,
  ) {
    const e = this.els;
    e.album.add(new Option("", ""));
    for (const value of Object.keys(TUBE_LABELS)) e.album.add(new Option("", value));
    this.nameAlbumChoices();
    // The words drawn here, again in the language chosen.
    onLangChange(() => {
      this.nameAlbumChoices();
      this.render(true);
      this.showProblem();
    });
    e.enabled.addEventListener("change", () => this.change({ enabled: e.enabled.checked }));
    e.album.addEventListener("change", () => {
      const album = this.status?.album;
      if (album) this.change(withAlbumChoice(this.settings, album, (e.album.value as TubeType) || null));
    });
    e.add.addEventListener("click", () => {
      this.change({ rules: [...this.settings.rules, { genre: "", type: this.manualType() }] }, TEXT_SEND_MS, true);
      (e.rules.querySelector("tr:last-child input") as HTMLInputElement | null)?.focus();
    });
    e.reset.addEventListener("click", () => this.change({ rules: structuredClone(this.defaults) }, 0, true));
  }

  // The album's choices: automatic, then each type.
  private nameAlbumChoices() {
    const labels = TUBE_LABELS as Record<string, string>;
    for (const o of this.els.album.options) o.textContent = o.value === "" ? t("controls.tubeAuto.albumAuto") : labels[o.value];
  }

  load(s: TubeAutoSettings, defaults: TubeGenreRule[]) {
    this.settings = structuredClone(s);
    this.defaults = structuredClone(defaults);
    this.render(true);
  }

  onStatus(s: PlaybackStatus) {
    this.status = s.state === "empty" ? null : (s.tubeAuto ?? null);
    this.renderNow();
  }

  // The manual type changed (the line about the current track names it).
  renderNow() {
    const e = this.els;
    const text = choiceText(this.settings, this.status, this.manualType());
    if (text !== this.shownNow) {
      this.shownNow = text;
      e.now.textContent = text;
    }
    const album = this.status?.album ?? null;
    e.albumRow.hidden = !album;
    if (album) {
      if (e.albumName.textContent !== album.title) e.albumName.textContent = album.title;
      const chosen = this.settings.albums.find((a) => a.albumKey === album.key)?.type ?? "";
      if (document.activeElement !== e.album) e.album.value = chosen;
      e.album.disabled = !this.settings.enabled;
    }
  }

  // Changes the settings and sends them after `delayMs`; `rows` redraws the table.
  private change(part: Partial<TubeAutoSettings>, delayMs = 0, rows = false) {
    this.settings = { ...this.settings, ...part };
    this.render(rows);
    clearTimeout(this.sendTimer);
    this.saveFailed = null;
    if (this.showProblem()) return;
    this.sendTimer = setTimeout(async () => {
      const r = await this.api.setTubeAuto(this.settings);
      if (!r.ok) {
        this.saveFailed = { message: r.message };
        this.showProblem();
      }
    }, delayMs);
  }

  // What stops the table from being saved, else the app's refusal of the
  // last send, else nothing. True when the table has a problem.
  private showProblem(): boolean {
    const problem = rulesProblem(this.settings.rules);
    const text = problem
      ? `⚠ ${t("controls.tubeAuto.problemNotSaved", { problem })}`
      : this.saveFailed
        ? `⚠ ${nativeText(this.saveFailed.message) || t("controls.tubeAuto.saveFailed")}`
        : "";
    this.els.problem.hidden = text === "";
    this.els.problem.textContent = text;
    return problem !== null;
  }

  private render(rows: boolean) {
    const e = this.els;
    e.enabled.checked = this.settings.enabled;
    if (rows) this.renderRules();
    e.ruleCount.textContent = t("controls.tubeAuto.ruleCount", { n: this.settings.rules.length });
    this.renderAlbums();
    this.shownNow = "";
    this.renderNow();
  }

  private renderRules() {
    const rows = this.settings.rules.map((r, i) => {
      const tr = document.createElement("tr");
      const genre = document.createElement("input");
      genre.type = "text";
      genre.value = r.genre;
      genre.maxLength = MAX_GENRE_LENGTH;
      genre.setAttribute("aria-label", t("controls.tubeAuto.rowGenre", { n: i + 1 }));
      genre.addEventListener("input", () => {
        const rules = this.settings.rules.map((x, j) => (j === i ? { ...x, genre: genre.value } : x));
        this.change({ rules }, TEXT_SEND_MS);
      });
      const type = document.createElement("select");
      type.setAttribute("aria-label", t("controls.tubeAuto.rowType", { n: i + 1 }));
      for (const [value, label] of Object.entries(TUBE_LABELS)) type.add(new Option(label, value));
      type.value = r.type;
      type.addEventListener("change", () => {
        const rules = this.settings.rules.map((x, j) => (j === i ? { ...x, type: type.value as TubeType } : x));
        this.change({ rules });
      });
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "link";
      remove.textContent = t("controls.tubeAuto.remove");
      remove.setAttribute("aria-label", t("controls.tubeAuto.rowRemove", { n: i + 1 }));
      remove.addEventListener("click", () => this.change({ rules: this.settings.rules.filter((_, j) => j !== i) }, 0, true));
      const cells = [genre, type, remove].map((c) => {
        const td = document.createElement("td");
        td.append(c);
        return td;
      });
      tr.append(...cells);
      return tr;
    });
    this.els.rules.replaceChildren(...rows);
  }

  private renderAlbums() {
    const list = this.settings.albums.map((a) => {
      const li = document.createElement("li");
      const name = document.createElement("span");
      name.textContent = t("controls.tubeAuto.albumChoice", {
        album: a.album || t("controls.tubeAuto.noAlbumName"),
        type: TUBE_LABELS[a.type],
      });
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "link";
      remove.textContent = t("controls.tubeAuto.stopChoice");
      remove.addEventListener("click", () =>
        this.change({ albums: this.settings.albums.filter((x) => x.albumKey !== a.albumKey) }),
      );
      li.append(name, " ", remove);
      return li;
    });
    if (list.length === 0) {
      const li = document.createElement("li");
      li.className = "note";
      li.textContent = t("controls.tubeAuto.noAlbums");
      list.push(li);
    }
    this.els.albums.replaceChildren(...list);
    this.els.albumCount.textContent = t("controls.tubeAuto.albumCount", { n: this.settings.albums.length });
  }
}
