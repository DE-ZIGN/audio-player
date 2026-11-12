// The output check dialog (task 6-4): draws outputCheck.ts's models. Opened
// from the output field at the right of the top bar. Updated with every
// status event (at most 10 a second) while it is open.

import { ispWarning, meterModel, pathModel, verdictModel, type PathBox, type ProcessingBox } from "./outputCheck";
import type { PlaybackStatus } from "./status";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const MARKS = { ok: "✓", ng: "✕", warn: "⚠", info: "–" } as const;

function el<K extends keyof HTMLElementTagNameMap>(tag: K, cls = "", text = ""): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text) e.textContent = text;
  return e;
}

export class OutputView {
  private readonly els = {
    dialog: $<HTMLDialogElement>("output-dialog"),
    file: $("oc-file"),
    diagram: $("oc-diagram"),
    isp: $("oc-isp"),
    ispText: $("oc-isp-text"),
    ispOn: $<HTMLButtonElement>("oc-isp-on"),
    verdict: $("oc-verdict"),
    peaks: $("oc-peaks"),
    peakValues: $("oc-peak-values"),
    loudFill: $("oc-loud-fill"),
    momentary: $("oc-momentary"),
    short: $("oc-short"),
    integrated: $("oc-integrated"),
    meterNote: $("meter-note"),
  };
  private last: PlaybackStatus | null = null;
  private drawnKey = "";

  constructor(enableIspSafe: (mode: PlaybackStatus["mode"]) => Promise<void>) {
    this.els.ispOn.addEventListener("click", async () => {
      if (!this.last) return;
      this.els.ispOn.disabled = true;
      await enableIspSafe(this.last.mode);
      this.els.ispOn.disabled = false;
    });
  }

  open() {
    this.els.dialog.showModal();
    if (this.last) this.render(this.last);
  }

  render(s: PlaybackStatus) {
    this.last = s;
    if (!this.els.dialog.open) return;
    this.els.file.textContent = s.state === "empty" ? "" : s.fileName;
    this.renderDiagram(s);

    const isp = ispWarning(s);
    this.els.isp.hidden = isp === null;
    if (isp) this.els.ispText.textContent = `⚠ ${isp}`;

    this.renderVerdict(s);
    this.renderMeters(s);
  }

  // --- the diagram and the verdict (rebuilt only when what they show changes) ---

  private renderDiagram(s: PlaybackStatus) {
    const m = pathModel(s);
    const key = JSON.stringify(m);
    if (key === this.drawnKey) return;
    this.drawnKey = key;

    const box = (b: PathBox) => {
      const d = el("div", "oc-box");
      d.append(el("div", "oc-box-title", b.title));
      for (const line of b.lines) d.append(el("div", line.startsWith("⚠") ? "oc-line warn" : "oc-line", line));
      return d;
    };
    const processing = (b: ProcessingBox) => {
      const d = el("div", `oc-box oc-processing${b.active ? " active" : ""}`);
      d.append(el("div", "oc-box-title", b.title));
      d.append(el("div", "oc-badge", b.badge));
      const list = el("ul", "oc-stages");
      for (const r of b.rows) {
        const li = el("li", r.on ? "on" : "");
        li.append(el("span", "", r.name), el("span", r.warn ? "oc-state warn" : "oc-state", r.warn ? `⚠ ${r.value}` : r.value));
        list.append(li);
      }
      d.append(list);
      if (b.note) d.append(el("div", "oc-line", b.note));
      return d;
    };
    const arrow = () => el("div", "oc-arrow", "→");
    this.els.diagram.replaceChildren(
      box(m.file),
      arrow(),
      box(m.decoder),
      arrow(),
      processing(m.processing),
      arrow(),
      box(m.output),
      arrow(),
      box(m.dac),
    );
  }

  private verdictKey = "";

  private renderVerdict(s: PlaybackStatus) {
    const v = verdictModel(s);
    const key = JSON.stringify(v);
    if (key === this.verdictKey) return;
    this.verdictKey = key;
    if (!v) {
      this.els.verdict.replaceChildren(el("p", "note", "再生を始めると、出力を開いたときの経路から判定します。"));
      return;
    }
    const head = el("div", `oc-verdict-head ${v.bitPerfect ? "ok" : "ng"}`);
    head.append(el("span", "oc-verdict-mark", v.bitPerfect ? "✓" : "✕"), el("span", "oc-verdict-title", v.title));
    const list = el("ul", "oc-checks");
    for (const c of v.checks) {
      const li = el("li", c.state);
      li.append(el("span", "oc-mark", MARKS[c.state]), el("span", "oc-check-label", c.label), el("span", "oc-check-detail", c.detail));
      list.append(li);
    }
    this.els.verdict.replaceChildren(head, list, el("p", "note oc-scope", v.scope));
  }

  // --- meters (every update) -------------------------------------------------------

  private renderMeters(s: PlaybackStatus) {
    const m = meterModel(s);
    if (this.els.peaks.children.length !== m.peaks.length) {
      this.els.peaks.replaceChildren(
        ...m.peaks.map((p) => {
          const col = el("div", "oc-bar-col");
          const bar = el("div", "oc-bar");
          bar.append(el("div", "fill"));
          col.append(bar, el("div", "oc-bar-label", p.label));
          return col;
        }),
      );
    }
    m.peaks.forEach((p, i) => {
      const fill = this.els.peaks.children[i].querySelector<HTMLElement>(".fill")!;
      fill.style.height = `${p.percent}%`;
      fill.classList.toggle("over", p.over);
    });
    this.els.peakValues.replaceChildren(
      ...m.peaks.map((p) => {
        const d = el("div", `oc-value${p.over ? " over" : ""}`);
        d.append(el("span", "oc-value-label", p.label), el("span", "oc-value-now", p.now));
        d.append(el("span", "oc-value-max", `最大 ${p.over ? "⚠ " : ""}${p.max}`));
        return d;
      }),
    );
    this.els.loudFill.style.height = `${m.loudness.percent}%`;
    this.els.momentary.textContent = m.loudness.momentary;
    this.els.short.textContent = m.loudness.shortTerm;
    this.els.integrated.textContent = m.loudness.integrated;
    this.els.meterNote.textContent = m.note;
  }
}
