// The EQ editor (task 6-3): a large dialog with the response curve, where
// each band is a node to drag, and knobs for the selected band and the preamp.
// Changes go to the settings screen (SettingsView), which sends and saves them.

import {
  bandAt,
  defaultValue,
  dragNode,
  graphX,
  graphY,
  nodeAt,
  nodePoint,
  parseKnobText,
  wheelKnob,
  type GraphFrame,
  type Param,
} from "./eqEdit";
import { autoPreampDb, responsePoints, usesGain } from "./eqResponse";
import { onLangChange, t } from "./i18n";
import { Knob } from "./knob";
import {
  dbText,
  FILTER_LABELS,
  frequencyText,
  MAX_BANDS,
  type EqBand,
  type EqFilterType,
  type ListeningSettings,
} from "./settings";
import type { LimiterStats } from "./status";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const SVG = "http://www.w3.org/2000/svg";
const RATE = 48000; // the curve as at 48 kHz (the engine works at the track's rate)
const DRAG_SEND_MS = 60; // while dragging, the settings go to the app at most this often

export interface EqEditorHost {
  listening(): ListeningSettings;
  // Changes the settings; sent to the app after `delayMs` (restarted by every change).
  update(part: Partial<ListeningSettings>, delayMs: number): void;
  importEq(): Promise<void>;
  setSolo(band: number | null): void;
}

function svgEl<K extends keyof SVGElementTagNameMap>(name: K, attrs: Record<string, string | number> = {}) {
  const e = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, String(v));
  return e;
}

export class EqEditor {
  private selected = -1;
  private solo: number | null = null;
  private rangeDb = 12;
  private drag: { index: number; start: EqBand; x: number; y: number; moved: boolean } | null = null;

  private readonly els = {
    dialog: $<HTMLDialogElement>("eq-dialog"),
    state: $("eqd-state"),
    modeNote: $("eqd-mode-note"),
    modeText: $("eqd-mode-text"),
    toProcessed: $<HTMLButtonElement>("eqd-to-processed"),
    close: $<HTMLButtonElement>("eqd-close"),
    enabled: $<HTMLInputElement>("eqd-enabled"),
    range: $<HTMLSelectElement>("eqd-range"),
    importBtn: $<HTMLButtonElement>("eqd-import"),
    clear: $<HTMLButtonElement>("eqd-clear"),
    graph: document.getElementById("eqd-graph") as unknown as SVGSVGElement,
    warnings: $("eqd-warnings"),
    chips: $("eqd-chips"),
    controls: $("eqd-controls"),
    autoPreamp: $<HTMLInputElement>("eqd-auto-preamp"),
    preampKnobBox: $("eqd-preamp-knob"),
    preampNote: $("eqd-preamp-note"),
    limiter: $("eqd-limiter"),
    menu: $("eqd-menu"),
  };

  private preampKnob: Knob;
  private bandKnobs: Record<"frequency" | "gainDb" | "q", Knob> | null = null;
  private controlsFor = -2; // the band the controls were built for (-1: none)
  private lastLimiter: LimiterStats | null = null;

  constructor(
    private readonly host: EqEditorHost,
    private readonly setMode: (processed: boolean) => void,
  ) {
    this.preampKnob = this.makePreampKnob();
    this.els.preampKnobBox.append(this.preampKnob.el);
    // The words drawn here, again in the language chosen: the knobs are made
    // again (their names are set when made), the rest is drawn again.
    onLangChange(() => {
      const before = this.preampKnob;
      this.preampKnob = this.makePreampKnob();
      before.el.replaceWith(this.preampKnob.el);
      const l = this.host.listening();
      this.preampKnob.set(l.eqAutoPreamp ? this.autoPreamp() : l.eqManualPreampDb);
      this.preampKnob.setDisabled(l.eqAutoPreamp);
      this.controlsFor = -2;
      if (this.lastLimiter) this.showStatus(this.lastLimiter, this.solo);
      this.render();
      this.hideMenu();
    });

    this.els.close.addEventListener("click", () => {
      if (this.solo !== null) this.setSolo(null);
      this.els.dialog.close();
    });
    this.els.dialog.addEventListener("close", () => {
      this.hideMenu();
      if (this.solo !== null) this.setSolo(null); // solo is only for while the editor is open
    });
    this.els.toProcessed.addEventListener("click", () => this.setMode(true));
    this.els.enabled.addEventListener("change", () => this.host.update({ eqEnabled: this.els.enabled.checked }, 0));
    this.els.range.addEventListener("change", () => {
      this.rangeDb = Number(this.els.range.value);
      this.render();
    });
    this.els.importBtn.addEventListener("click", async () => {
      await this.host.importEq();
      this.selected = -1;
      this.render();
    });
    this.els.clear.addEventListener("click", () => {
      this.setSolo(null);
      this.selected = -1;
      this.host.update({ eqBands: [] }, 0);
    });
    this.els.autoPreamp.addEventListener("change", () =>
      this.host.update({ eqAutoPreamp: this.els.autoPreamp.checked }, 0),
    );
    this.bindGraph();
    this.els.dialog.addEventListener("keydown", (e) => {
      const t = e.target as HTMLElement;
      if (["INPUT", "SELECT", "TEXTAREA"].includes(t.tagName)) return;
      if ((e.key === "Delete" || e.key === "Backspace") && this.selected >= 0) {
        e.preventDefault();
        this.removeBand(this.selected);
      }
    });
    new ResizeObserver(() => this.els.dialog.open && this.drawGraph()).observe(this.els.graph);
  }

  private makePreampKnob(): Knob {
    return new Knob({
      param: "preamp",
      label: t("controls.eqEditor.preamp"),
      value: 0,
      defaultValue: () => 0,
      parse: (text) => this.parse("preamp", text),
      onChange: (v) => this.host.update({ eqManualPreampDb: v }, DRAG_SEND_MS),
    });
  }

  open() {
    const bands = this.bands();
    if (this.selected >= bands.length) this.selected = -1;
    if (this.selected < 0 && bands.length > 0) this.selected = 0;
    this.els.dialog.showModal();
    this.render();
  }

  isOpen() {
    return this.els.dialog.open;
  }

  // The settings changed (from here or elsewhere): draw them.
  render() {
    if (!this.els.dialog.open) return;
    const l = this.host.listening();
    const bands = l.eqBands;
    if (this.selected >= bands.length) this.selected = bands.length - 1;
    if (this.solo !== null && this.solo >= bands.length) this.setSolo(null);

    const original = l.mode === "original";
    this.els.modeNote.hidden = !original && l.eqEnabled;
    this.els.modeText.textContent = `⚠ ${original ? t("controls.eqEditor.originalMode") : t("controls.eqEditor.eqOff")}`;
    this.els.toProcessed.hidden = !original;
    this.els.enabled.checked = l.eqEnabled;
    this.els.clear.disabled = bands.length === 0;
    this.els.state.textContent = t("controls.eqEditor.bandCount", { n: bands.length, max: MAX_BANDS });

    this.els.autoPreamp.checked = l.eqAutoPreamp;
    this.preampKnob.set(l.eqAutoPreamp ? this.autoPreamp() : l.eqManualPreampDb);
    this.preampKnob.setDisabled(l.eqAutoPreamp);
    this.els.preampNote.textContent = l.eqAutoPreamp
      ? t("controls.eqEditor.preampAuto", { db: dbText(this.autoPreamp()), khz: RATE / 1000 })
      : this.manualNote(l);

    this.renderChips();
    this.renderControls();
    this.drawGraph();
  }

  // From the playback status: the limiter's count and the solo state.
  showStatus(limiter: LimiterStats, eqSolo: number | null) {
    this.solo = eqSolo;
    this.lastLimiter = limiter;
    this.els.limiter.textContent =
      limiter.events === 0
        ? t("controls.eqEditor.limiterIdle")
        : `⚠ ${t("controls.eqEditor.limiterActive", { events: limiter.events, db: dbText(limiter.maxReductionDb) })}`;
    this.els.limiter.className = limiter.events === 0 ? "note" : "notice";
    if (this.els.dialog.open) {
      this.renderChips();
      this.updateSoloButton();
    }
  }

  showWarnings(lines: string[]) {
    this.els.warnings.hidden = lines.length === 0;
    this.els.warnings.textContent = lines.map((n) => `⚠ ${n}`).join("\n");
  }

  // --- state helpers -----------------------------------------------------------

  private bands() {
    return this.host.listening().eqBands;
  }

  private autoPreamp() {
    const l = this.host.listening();
    return autoPreampDb(l.eqEnabled ? l.eqBands : [], RATE);
  }

  private manualNote(l: ListeningSettings) {
    const over = -autoPreamp(l) - -l.eqManualPreampDb; // the largest boost minus what the preamp takes off
    return over > 0.05
      ? `⚠ ${t("controls.eqEditor.preampManualShort", { db: over.toFixed(1) })}`
      : t("controls.eqEditor.preampManualOk");
  }

  private parse(p: Param, text: string): number | null {
    return parseKnobText(p, text);
  }

  private setBand(i: number, part: Partial<EqBand>, delayMs = 0) {
    const bands = this.bands().map((b, j) => (j === i ? { ...b, ...part } : b));
    this.host.update({ eqBands: bands }, delayMs);
  }

  private addBand(b: EqBand) {
    const bands = this.bands();
    if (bands.length >= MAX_BANDS) return;
    this.selected = bands.length;
    this.host.update({ eqBands: [...bands, b] }, 0);
  }

  private removeBand(i: number) {
    if (this.solo !== null) this.setSolo(null);
    const bands = this.bands().filter((_, j) => j !== i);
    this.selected = Math.min(i, bands.length - 1);
    this.host.update({ eqBands: bands }, 0);
  }

  private setSolo(band: number | null) {
    this.solo = band;
    this.host.setSolo(band);
    this.renderChips();
    this.updateSoloButton();
  }

  private select(i: number) {
    if (i === this.selected) return;
    this.selected = i;
    if (this.solo !== null && i >= 0) this.setSolo(i); // solo follows the selected band
    this.renderChips();
    this.renderControls();
    this.drawGraph();
  }

  // --- band chips and controls --------------------------------------------------

  private renderChips() {
    const bands = this.bands();
    this.els.chips.replaceChildren(
      ...bands.map((b, i) => {
        const chip = document.createElement("button");
        chip.type = "button";
        chip.setAttribute("role", "tab");
        chip.setAttribute("aria-selected", String(i === this.selected));
        chip.className = "eq-chip";
        chip.classList.toggle("selected", i === this.selected);
        chip.classList.toggle("off", !b.enabled);
        chip.classList.toggle("solo", this.solo === i);
        chip.innerHTML = "";
        const no = document.createElement("b");
        no.textContent = String(i + 1);
        const text = document.createElement("span");
        text.textContent = bandText(b);
        chip.append(no, text);
        chip.addEventListener("click", () => this.select(i));
        return chip;
      }),
    );
    if (bands.length === 0) {
      const p = document.createElement("p");
      p.className = "note";
      p.textContent = t("controls.eqEditor.noBands");
      this.els.chips.append(p);
    }
  }

  private renderControls() {
    const band = this.selected >= 0 ? this.bands()[this.selected] : undefined;
    if (!band) {
      this.controlsFor = -1;
      this.bandKnobs = null;
      const p = document.createElement("p");
      p.className = "note";
      p.textContent = t("controls.eqEditor.chooseBand");
      this.els.controls.replaceChildren(p);
      return;
    }
    if (this.controlsFor === this.selected && this.bandKnobs) {
      // Same band: only the values (keeps a knob being turned or typed in).
      this.bandKnobs.frequency.set(band.frequency);
      this.bandKnobs.gainDb.set(band.gainDb);
      this.bandKnobs.q.set(band.q);
      this.bandKnobs.gainDb.setDisabled(!usesGain(band.type));
      const on = this.els.controls.querySelector<HTMLInputElement>("input.eq-on");
      if (on) on.checked = band.enabled;
      const type = this.els.controls.querySelector<HTMLSelectElement>("select.eq-type");
      if (type) type.value = band.type;
      this.updateSoloButton();
      return;
    }
    this.controlsFor = this.selected;
    const i = this.selected;

    const head = document.createElement("div");
    head.className = "eq-control-head";
    const title = document.createElement("b");
    title.textContent = t("controls.eqEditor.bandTitle", { n: i + 1 });
    const onLabel = document.createElement("label");
    onLabel.className = "check";
    const on = document.createElement("input");
    on.type = "checkbox";
    on.className = "eq-on";
    on.checked = band.enabled;
    on.addEventListener("change", () => this.setBand(i, { enabled: on.checked }));
    onLabel.append(on, ` ${t("controls.eqEditor.use")}`);
    const type = document.createElement("select");
    type.className = "eq-type";
    type.setAttribute("aria-label", t("controls.eqEditor.type"));
    for (const [value, label] of Object.entries(FILTER_LABELS)) type.add(new Option(label, value));
    type.value = band.type;
    type.addEventListener("change", () => this.setBand(i, { type: type.value as EqFilterType }));
    const solo = document.createElement("button");
    solo.type = "button";
    solo.className = "eq-solo";
    solo.title = t("controls.eqEditor.soloTitle");
    solo.addEventListener("click", () => this.setSolo(this.solo === i ? null : i));
    const remove = document.createElement("button");
    remove.type = "button";
    remove.className = "link";
    remove.textContent = t("controls.eqEditor.removeBand");
    remove.addEventListener("click", () => this.removeBand(i));
    head.append(title, onLabel, type, solo, remove);

    const knobs = document.createElement("div");
    knobs.className = "eq-knobs";
    const make = (param: "frequency" | "gainDb" | "q", label: string) =>
      new Knob({
        param,
        label,
        value: band[param],
        centred: param === "gainDb",
        defaultValue: () => defaultValue(param, this.bands()[i] ?? null),
        parse: (text) => this.parse(param, text),
        onChange: (v) => this.setBand(i, { [param]: v }, DRAG_SEND_MS),
      });
    this.bandKnobs = {
      frequency: make("frequency", t("controls.eqEditor.frequency")),
      gainDb: make("gainDb", t("controls.eqEditor.gain")),
      q: make("q", "Q"),
    };
    this.bandKnobs.gainDb.setDisabled(!usesGain(band.type));
    knobs.append(this.bandKnobs.frequency.el, this.bandKnobs.gainDb.el, this.bandKnobs.q.el);
    this.els.controls.replaceChildren(head, knobs);
    this.updateSoloButton();
  }

  private updateSoloButton() {
    const b = this.els.controls.querySelector<HTMLButtonElement>("button.eq-solo");
    if (!b) return;
    const on = this.solo !== null && this.solo === this.selected;
    b.classList.toggle("active", on);
    b.textContent = on ? t("controls.eqEditor.soloOn") : "Solo";
    b.setAttribute("aria-pressed", String(on));
    const l = this.host.listening();
    b.disabled = l.mode === "original" || !l.eqEnabled;
  }

  // --- the graph ------------------------------------------------------------------

  private frame(): GraphFrame {
    const r = this.els.graph.getBoundingClientRect();
    return { width: Math.max(1, r.width), height: Math.max(1, r.height), minHz: 20, maxHz: 20000, rangeDb: this.rangeDb };
  }

  private local(e: MouseEvent) {
    const r = this.els.graph.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }

  private bindGraph() {
    const g = this.els.graph;
    g.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      this.hideMenu();
      const { x, y } = this.local(e);
      const i = nodeAt(this.bands(), x, y, this.frame());
      if (i < 0) return;
      e.preventDefault();
      this.select(i);
      g.setPointerCapture(e.pointerId);
      this.drag = { index: i, start: { ...this.bands()[i] }, x, y, moved: false };
    });
    g.addEventListener("pointermove", (e) => {
      const { x, y } = this.local(e);
      if (!this.drag) {
        g.classList.toggle("over-node", nodeAt(this.bands(), x, y, this.frame()) >= 0);
        return;
      }
      if (e.shiftKey !== (g.dataset.fine === "1")) {
        // Shift pressed or released mid-drag: carry on from here.
        this.drag = { ...this.drag, start: { ...this.bands()[this.drag.index] }, x, y };
        g.dataset.fine = e.shiftKey ? "1" : "";
      }
      const next = dragNode(this.drag.start, x - this.drag.x, y - this.drag.y, this.frame(), e.shiftKey);
      this.drag.moved = true;
      this.setBand(this.drag.index, { frequency: next.frequency, gainDb: next.gainDb, q: next.q }, DRAG_SEND_MS);
    });
    const end = (e: PointerEvent) => {
      if (g.hasPointerCapture(e.pointerId)) g.releasePointerCapture(e.pointerId);
      if (this.drag?.moved) this.host.update({}, 0); // send the last position now
      this.drag = null;
      g.dataset.fine = "";
    };
    g.addEventListener("pointerup", end);
    g.addEventListener("pointercancel", end);
    g.addEventListener("dblclick", (e) => {
      const { x, y } = this.local(e);
      const f = this.frame();
      const i = nodeAt(this.bands(), x, y, f);
      if (i >= 0) this.removeBand(i);
      else this.addBand(bandAt(x, y, f));
    });
    g.addEventListener(
      "wheel",
      (e) => {
        const { x, y } = this.local(e);
        const under = nodeAt(this.bands(), x, y, this.frame(), 24);
        const i = under >= 0 ? under : this.selected;
        if (i < 0) return;
        e.preventDefault();
        if (under >= 0) this.select(under);
        const b = this.bands()[i];
        this.setBand(i, { q: wheelKnob("q", b.q, e.deltaY, e.shiftKey) }, DRAG_SEND_MS);
      },
      { passive: false },
    );
    g.addEventListener("contextmenu", (e) => {
      const { x, y } = this.local(e);
      const i = nodeAt(this.bands(), x, y, this.frame());
      if (i < 0) return;
      e.preventDefault();
      this.select(i);
      this.showMenu(i, e.clientX, e.clientY);
    });
    document.addEventListener("pointerdown", (e) => {
      if (!this.els.menu.hidden && !this.els.menu.contains(e.target as Node)) this.hideMenu();
    });
  }

  private showMenu(i: number, clientX: number, clientY: number) {
    const b = this.bands()[i];
    const item = (text: string, action: () => void, checked = false, disabled = false) => {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.setAttribute("role", "menuitem");
      btn.textContent = `${checked ? "✓ " : ""}${text}`;
      btn.disabled = disabled;
      btn.addEventListener("click", () => {
        this.hideMenu();
        action();
      });
      return btn;
    };
    const sep = () => {
      const hr = document.createElement("hr");
      return hr;
    };
    const l = this.host.listening();
    this.els.menu.replaceChildren(
      item(b.enabled ? t("controls.eqEditor.dontUse") : t("controls.eqEditor.use"), () => this.setBand(i, { enabled: !b.enabled })),
      item(this.solo === i ? t("controls.eqEditor.soloStop") : "Solo", () => this.setSolo(this.solo === i ? null : i), false,
        l.mode === "original" || !l.eqEnabled),
      sep(),
      ...(Object.entries(FILTER_LABELS) as [EqFilterType, string][]).map(([type, label]) =>
        item(label, () => this.setBand(i, { type }), b.type === type),
      ),
      sep(),
      item(t("controls.eqEditor.remove"), () => this.removeBand(i)),
    );
    const menu = this.els.menu;
    menu.hidden = false;
    const r = menu.getBoundingClientRect();
    menu.style.left = `${Math.min(clientX, window.innerWidth - r.width - 8)}px`;
    menu.style.top = `${Math.min(clientY, window.innerHeight - r.height - 8)}px`;
  }

  private hideMenu() {
    this.els.menu.hidden = true;
  }

  private drawGraph() {
    if (!this.els.dialog.open) return;
    const f = this.frame();
    const svg = this.els.graph;
    svg.setAttribute("viewBox", `0 0 ${f.width} ${f.height}`);
    const l = this.host.listening();
    const bands = l.eqBands;
    const active = l.eqEnabled && l.mode === "processed";
    const children: SVGElement[] = [];

    // Grid and labels.
    const freqs = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000];
    for (const hz of freqs) {
      const x = graphX(hz, f);
      children.push(svgEl("line", { x1: x, x2: x, y1: 0, y2: f.height, class: "grid" }));
      if (hz > 20 && hz < 20000) {
        const label = svgEl("text", { x: x + 3, y: f.height - 5, class: "axis" });
        label.textContent = hz >= 1000 ? `${hz / 1000}k` : String(hz);
        children.push(label);
      }
    }
    const step = f.rangeDb <= 6 ? 2 : f.rangeDb <= 12 ? 3 : 6;
    for (let db = -f.rangeDb; db <= f.rangeDb; db += step) {
      const y = graphY(db, f);
      children.push(svgEl("line", { x1: 0, x2: f.width, y1: y, y2: y, class: db === 0 ? "zero" : "grid" }));
      if (db !== -f.rangeDb && db !== f.rangeDb) {
        const label = svgEl("text", { x: 5, y: y - 3, class: "axis" });
        label.textContent = db === 0 ? "0 dB" : `${db > 0 ? "+" : "−"}${Math.abs(db)}`;
        children.push(label);
      }
    }

    const count = Math.max(100, Math.min(600, Math.round(f.width / 2)));
    const path = (points: [number, number][], offset = 0) =>
      points
        .map(([hz, db], k) => `${k === 0 ? "M" : "L"}${graphX(hz, f).toFixed(1)} ${graphY(db + offset, f).toFixed(1)}`)
        .join(" ");

    // The selected band alone, as a shaded area from the 0 dB line.
    const sel = this.selected >= 0 ? bands[this.selected] : undefined;
    if (sel && sel.enabled) {
      const pts = responsePoints([sel], RATE, count);
      const zero = graphY(0, f).toFixed(1);
      children.push(
        svgEl("path", {
          d: `M0 ${zero} ${path(pts).replace(/^M/, "L")} L${f.width} ${zero} Z`,
          class: "band-area",
        }),
      );
    }

    const used = l.eqEnabled ? bands : [];
    const total = responsePoints(used, RATE, count);
    const preamp = l.eqAutoPreamp ? autoPreampDb(used, RATE) : l.eqManualPreampDb;
    children.push(svgEl("path", { d: path(total), class: `curve${active ? "" : " inactive"}` }));
    if (preamp !== 0 && used.some((b) => b.enabled))
      children.push(svgEl("path", { d: path(total, preamp), class: `curve-out${active ? "" : " inactive"}` }));

    // Nodes, the selected one last (on top).
    const order = bands.map((_, i) => i).sort((a, b) => (a === this.selected ? 1 : b === this.selected ? -1 : a - b));
    for (const i of order) {
      const b = bands[i];
      const p = nodePoint(b, f);
      const cls = ["node", i === this.selected ? "selected" : "", b.enabled ? "" : "off", this.solo === i ? "solo" : ""]
        .filter(Boolean)
        .join(" ");
      const gNode = svgEl("g", { class: cls, transform: `translate(${p.x.toFixed(1)} ${p.y.toFixed(1)})` });
      gNode.append(svgEl("circle", { r: 9 }));
      const no = svgEl("text", { y: 3.5 });
      no.textContent = String(i + 1);
      gNode.append(no);
      const title = svgEl("title");
      title.textContent = t("controls.eqEditor.nodeTitle", { n: i + 1, band: bandText(b), q: b.q.toFixed(2) });
      gNode.append(title);
      children.push(gNode);
    }
    svg.replaceChildren(...children);
  }
}

// A band in a few words: its type, frequency and (when it has one) gain.
function bandText(b: EqBand): string {
  return `${FILTER_LABELS[b.type]} ${frequencyText(b.frequency)}${usesGain(b.type) ? ` ${dbText(b.gainDb)}` : ""}`;
}

function autoPreamp(l: ListeningSettings) {
  return autoPreampDb(l.eqEnabled ? l.eqBands : [], RATE);
}
