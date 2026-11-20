// The EQ presets' bar (task 6-9), on the settings screen and in the EQ
// dialog: choose a preset (puts its curve into the EQ), save the current EQ
// under a name (a name in use is replaced), rename, delete (pressed twice).
// The list itself is the app's: every change is sent whole (eqPresets.ts).

import {
  matchingPreset,
  MAX_PRESET_NAME,
  presetFrom,
  presetName,
  withDeleted,
  withRenamed,
  withSaved,
  type EqPreset,
  type ListResult,
} from "./eqPresets";
import { onLangChange, t, type MessageKey } from "./i18n";
import type { ListeningSettings } from "./settings";

export interface PresetHost {
  listening(): ListeningSettings;
  presets(): EqPreset[];
  apply(p: EqPreset): void;
  save(list: EqPreset[]): Promise<string | null>; // an error to show, or null
  // The preset last chosen or saved, shared by the bars (shown when several are the current EQ).
  chosen: string;
}

const NONE = "";
const CONFIRM_MS = 4000;

export class PresetBar {
  private readonly select = document.createElement("select");
  private readonly saveBtn = this.button();
  private readonly renameBtn = this.button();
  private readonly deleteBtn = this.button();
  private readonly form = document.createElement("div");
  private readonly input = document.createElement("input");
  private readonly okBtn = this.button();
  private readonly cancelBtn = this.button();
  private readonly labelText = document.createTextNode("");
  private readonly message = document.createElement("span");
  private said: () => string = () => ""; // the message shown, drawn again in another language
  private mode: "save" | "rename" | null = null;
  private renaming = "";
  private confirmTimer: ReturnType<typeof setTimeout> | undefined;

  constructor(
    root: HTMLElement,
    private readonly host: PresetHost,
  ) {
    root.classList.add("eq-presets");
    const label = document.createElement("label");
    label.className = "eq-preset-choice";
    label.append(this.labelText, this.select);
    this.input.type = "text";
    this.input.maxLength = MAX_PRESET_NAME;
    this.form.className = "eq-preset-form";
    this.form.hidden = true;
    this.form.append(this.input, this.okBtn, this.cancelBtn);
    this.message.className = "note eq-preset-message";
    root.replaceChildren(label, this.saveBtn, this.renameBtn, this.deleteBtn, this.form, this.message);

    this.select.addEventListener("change", () => {
      const p = this.host.presets().find((q) => q.name === this.select.value);
      if (!p) return;
      this.host.chosen = p.name;
      this.closeForm();
      this.host.apply(p);
    });
    this.saveBtn.addEventListener("click", () => this.openForm("save"));
    this.renameBtn.addEventListener("click", () => this.openForm("rename"));
    this.deleteBtn.addEventListener("click", () => void this.remove());
    this.okBtn.addEventListener("click", () => void this.submit());
    this.cancelBtn.addEventListener("click", () => this.closeForm());
    this.input.addEventListener("input", () => this.showNameNote());
    this.input.addEventListener("keydown", (e) => {
      e.stopPropagation(); // the EQ dialog's own keys (Delete removes a band) stay out of the name
      if (e.key === "Enter") void this.submit();
      if (e.key === "Escape") {
        e.preventDefault(); // close the form, not the dialog
        this.closeForm();
      }
    });
    this.name();
    onLangChange(() => {
      this.name();
      this.render();
      this.say(this.said);
    });
  }

  // The bar's own words, in the language in use.
  private name() {
    this.labelText.data = `${t("controls.eqPresets.label")} `;
    this.select.setAttribute("aria-label", t("controls.eqPresets.selectLabel"));
    this.input.placeholder = t("controls.eqPresets.name");
    this.input.setAttribute("aria-label", t("controls.eqPresets.name"));
    const words: [HTMLButtonElement, MessageKey, MessageKey | null][] = [
      [this.saveBtn, "controls.eqPresets.save", "controls.eqPresets.saveTitle"],
      [this.renameBtn, "controls.eqPresets.rename", "controls.eqPresets.renameTitle"],
      [this.deleteBtn, "controls.eqPresets.delete", "controls.eqPresets.deleteTitle"],
      [this.cancelBtn, "controls.eqPresets.cancel", null],
    ];
    for (const [b, text, title] of words) {
      b.textContent = t(text);
      if (title) b.title = t(title);
    }
    if (this.deleteBtn.dataset.confirm !== undefined) this.deleteBtn.textContent = t("controls.eqPresets.confirmDelete");
    this.okBtn.textContent = this.mode === "rename" ? t("controls.eqPresets.renameOk") : this.mode === "save" ? t("controls.eqPresets.saveOk") : "OK";
  }

  render() {
    const presets = this.host.presets();
    const current = matchingPreset(this.host.listening(), presets, this.host.chosen);
    const options: HTMLOptionElement[] = [];
    if (current === null)
      options.push(new Option(presets.length === 0 ? t("controls.eqPresets.none") : t("controls.eqPresets.unsaved"), NONE));
    for (const p of presets) options.push(new Option(p.name, p.name));
    this.select.replaceChildren(...options);
    this.select.value = current ?? NONE;
    this.select.disabled = presets.length === 0;
    this.renameBtn.disabled = current === null;
    this.deleteBtn.disabled = current === null;
    if (current === null) this.resetDelete();
  }

  private button() {
    const b = document.createElement("button");
    b.type = "button";
    return b;
  }

  private current(): string | null {
    return matchingPreset(this.host.listening(), this.host.presets(), this.host.chosen);
  }

  private openForm(mode: "save" | "rename") {
    this.resetDelete();
    const current = this.current();
    if (mode === "rename" && current === null) return;
    this.mode = mode;
    this.renaming = current ?? "";
    this.input.value = mode === "rename" ? this.renaming : (current ?? this.host.chosen);
    this.okBtn.textContent = mode === "save" ? t("controls.eqPresets.saveOk") : t("controls.eqPresets.renameOk");
    this.form.hidden = false;
    this.input.focus();
    this.input.select();
    this.showNameNote();
  }

  private closeForm() {
    this.mode = null;
    this.form.hidden = true;
    this.say(() => "");
  }

  private showNameNote() {
    const name = presetName(this.input.value);
    const taken = name !== null && this.host.presets().some((p) => p.name === name);
    if (this.mode === "save" && taken) this.say(() => t("controls.eqPresets.willReplace", { name }));
    else if (this.mode === "rename" && taken && name !== this.renaming) this.say(() => `⚠ ${t("controls.eqPresets.taken", { name })}`);
    else this.say(() => "");
  }

  private async submit() {
    if (!this.mode) return;
    const list = this.host.presets();
    const r: ListResult =
      this.mode === "save"
        ? withSaved(list, presetFrom(this.host.listening(), this.input.value))
        : withRenamed(list, this.renaming, this.input.value);
    if (!r.ok) {
      const message = r.message;
      this.say(() => `⚠ ${message}`);
      return;
    }
    const name = presetName(this.input.value)!;
    const before = this.host.chosen;
    this.host.chosen = name; // shown once saved
    const error = await this.host.save(r.list);
    if (error) {
      this.host.chosen = before;
      this.say(() => `⚠ ${error}`);
      return;
    }
    const renamed = this.mode === "rename";
    this.mode = null;
    this.form.hidden = true;
    this.say(() => (renamed ? t("controls.eqPresets.renamed", { name }) : t("controls.eqPresets.saved", { name })));
  }

  // Pressed once: asks; pressed again within a few seconds: deletes.
  private async remove() {
    const current = this.current();
    if (current === null) return;
    if (this.deleteBtn.dataset.confirm !== current) {
      this.closeForm();
      this.deleteBtn.dataset.confirm = current;
      this.deleteBtn.textContent = t("controls.eqPresets.confirmDelete");
      this.deleteBtn.classList.add("danger");
      clearTimeout(this.confirmTimer);
      this.confirmTimer = setTimeout(() => this.resetDelete(), CONFIRM_MS);
      return;
    }
    this.resetDelete();
    const error = await this.host.save(withDeleted(this.host.presets(), current));
    this.say(() => (error ? `⚠ ${error}` : t("controls.eqPresets.deleted", { name: current })));
  }

  private resetDelete() {
    clearTimeout(this.confirmTimer);
    delete this.deleteBtn.dataset.confirm;
    this.deleteBtn.textContent = t("controls.eqPresets.delete");
    this.deleteBtn.classList.remove("danger");
  }

  // `text` is kept, so the message is drawn again when the language changes.
  private say(text: () => string) {
    this.said = text;
    this.message.textContent = text();
  }
}
