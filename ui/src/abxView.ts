// The ABX dialog (task 6-6): set up (what to compare, the region, how many
// trials), run (A, B, X and the answers, by mouse or keys), and the result.
// Opened from the output check and from the settings screen. The test runs in
// the app; its state comes with the playback status.

import type { AbxApi } from "./backend";
import {
  defaultRegion,
  keyAction,
  presetPairProblem,
  regionText,
  resultText,
  sideLabels,
  type AbxChoice,
  type AbxKind,
} from "./abx";
import { FLAT_PRESET, type EqPreset } from "./eqPresets";
import type { PlaybackStatus } from "./status";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

function clock(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export class AbxView {
  private status: PlaybackStatus | null = null;
  private preparing = false;
  private presets: EqPreset[] = []; // 設定 A と設定 B (task 6-9)

  private readonly els = {
    dialog: $<HTMLDialogElement>("abx-dialog"),
    track: $("abx-track"),
    close: $<HTMLButtonElement>("abx-close"),
    setup: $("abx-setup"),
    kindNote: $("abx-kind-note"),
    presetBox: $("abx-presets"),
    presetA: $<HTMLSelectElement>("abx-preset-a"),
    presetB: $<HTMLSelectElement>("abx-preset-b"),
    start: $<HTMLInputElement>("abx-start"),
    startText: $("abx-start-text"),
    here: $<HTMLButtonElement>("abx-here"),
    length: $<HTMLSelectElement>("abx-length"),
    trials: $<HTMLSelectElement>("abx-trials"),
    error: $("abx-error"),
    startBtn: $<HTMLButtonElement>("abx-start-btn"),
    preparing: $("abx-preparing"),
    run: $("abx-run"),
    trial: $("abx-trial"),
    region: $("abx-region"),
    aLabel: $("abx-a-label"),
    bLabel: $("abx-b-label"),
    answerA: $<HTMLButtonElement>("abx-answer-a"),
    answerB: $<HTMLButtonElement>("abx-answer-b"),
    match: $("abx-match"),
    abort: $<HTMLButtonElement>("abx-abort"),
    result: $("abx-result"),
    headline: $("abx-headline"),
    p: $("abx-p"),
    verdict: $("abx-verdict"),
    low: $("abx-low"),
    level: $("abx-level"),
    again: $<HTMLButtonElement>("abx-again"),
    done: $<HTMLButtonElement>("abx-done"),
  };

  constructor(
    private readonly api: AbxApi,
    private readonly loadPresets: () => Promise<EqPreset[]>,
  ) {
    const e = this.els;
    for (const sel of [e.presetA, e.presetB]) sel.addEventListener("change", () => this.renderSetupNotes());
    e.close.addEventListener("click", () => void this.close());
    e.done.addEventListener("click", () => void this.close());
    e.again.addEventListener("click", async () => {
      await this.api.close();
      this.showSetup();
    });
    e.start.addEventListener("input", () => (e.startText.textContent = clock(Number(e.start.value))));
    e.here.addEventListener("click", () => this.fillRegion(true));
    e.length.addEventListener("change", () => this.fillRegion(false));
    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="abx-kind"]'))
      r.addEventListener("change", () => this.renderSetupNotes());
    e.startBtn.addEventListener("click", () => void this.start());
    for (const b of document.querySelectorAll<HTMLButtonElement>(".abx-choice"))
      b.addEventListener("click", () => void this.api.choose(b.dataset.choice as AbxChoice));
    e.answerA.addEventListener("click", () => void this.api.answer("A"));
    e.answerB.addEventListener("click", () => void this.api.answer("B"));
    e.abort.addEventListener("click", () => void this.api.abort());

    // Keys while it runs: a b x to listen, 1 2 to answer, Esc to stop (not to
    // close). On the document: the focus may have left the dialog (the start
    // button it was on is hidden once the test runs).
    document.addEventListener("keydown", (ev) => {
      if (!e.dialog.open || !this.status?.abx?.running) return;
      const t = ev.target as HTMLElement;
      if (["INPUT", "SELECT", "TEXTAREA"].includes(t.tagName)) return;
      const action = keyAction(ev.key);
      if (!action) return;
      ev.preventDefault();
      if (action === "abort") void this.api.abort();
      else if (action === "answerA") void this.api.answer("A");
      else if (action === "answerB") void this.api.answer("B");
      else void this.api.choose(action);
    });
    e.dialog.addEventListener("cancel", (ev) => {
      if (this.status?.abx?.running) ev.preventDefault(); // Esc stops the test (above), the dialog stays
    });
  }

  open() {
    const d = this.els.dialog;
    if (!d.open) d.showModal();
    if (this.status?.abx) this.render(this.status);
    else this.showSetup();
  }

  render(s: PlaybackStatus) {
    this.status = s;
    if (!this.els.dialog.open) return;
    this.els.track.textContent = s.state === "empty" ? "" : s.fileName;
    const a = s.abx;
    if (!a) {
      if (!this.els.run.hidden || !this.els.result.hidden) this.showSetup();
      return;
    }
    if (a.running) this.renderRun(s);
    else if (a.result) this.renderResult(s);
  }

  private async close() {
    if (this.status?.abx?.running) await this.api.abort();
    if (this.status?.abx) await this.api.close();
    this.els.dialog.close();
  }

  // --- setup ------------------------------------------------------------------------

  private kind(): AbxKind {
    return (document.querySelector<HTMLInputElement>('input[name="abx-kind"]:checked')?.value as AbxKind) ?? "processed";
  }

  private showSetup() {
    const e = this.els;
    e.setup.hidden = false;
    e.run.hidden = true;
    e.result.hidden = true;
    e.error.hidden = true;
    this.fillRegion(true);
    this.renderSetupNotes();
    void this.refreshPresets();
  }

  // The presets to choose from: "EQ なし" first, then the saved ones. The
  // choices made before stay when they still exist.
  private async refreshPresets() {
    try {
      this.presets = await this.loadPresets();
    } catch {
      this.presets = [];
    }
    const fill = (sel: HTMLSelectElement, fallback: string) => {
      const before = sel.value;
      sel.replaceChildren(new Option(FLAT_PRESET.name, "flat"), ...this.presets.map((p) => new Option(p.name, `p:${p.name}`)));
      const values = [...sel.options].map((o) => o.value);
      sel.value = values.includes(before) ? before : values.includes(fallback) ? fallback : values[0];
    };
    fill(this.els.presetA, "flat");
    fill(this.els.presetB, this.presets[0] ? `p:${this.presets[0].name}` : "flat");
    this.renderSetupNotes();
  }

  private presetFor(value: string): EqPreset | undefined {
    return value === "flat" ? FLAT_PRESET : this.presets.find((p) => `p:${p.name}` === value);
  }

  private fillRegion(fromPosition: boolean) {
    const s = this.status;
    const e = this.els;
    const rate = s?.sourceFormat?.sampleRate ?? 0;
    const total = rate > 0 && s ? s.totalFrames / rate : 0;
    const length = Number(e.length.value);
    const position = fromPosition ? (rate > 0 && s ? s.positionFrames / rate : 0) : Number(e.start.value);
    const r = defaultRegion(position, total, length);
    e.start.max = String(Math.max(0, Math.floor(total - Math.min(length, total))));
    e.start.value = String(r.start);
    e.startText.textContent = clock(r.start);
  }

  private renderSetupNotes() {
    const s = this.status;
    const e = this.els;
    let problem = "";
    if (!s || s.state === "empty" || !s.sourceFormat) problem = "ABX で聴く曲を、先に再生してください。";
    else if (s.dop) problem = "DSD（DoP）の曲は ABX で比べられません（DoP のデータには処理をかけないため）。";
    const presets = this.kind() === "presets";
    e.presetBox.hidden = !presets;
    const pairProblem = presets ? presetPairProblem(e.presetA.value, e.presetB.value, 1 + this.presets.length) : "";
    e.startBtn.disabled = problem !== "" || pairProblem !== "" || this.preparing;
    if (problem) {
      e.error.hidden = false;
      e.error.textContent = problem;
    }
    let note = pairProblem;
    if (note) {
      // (the pair's problem, above)
    } else if (this.kind() === "processed" && s?.mode === "original")
      note =
        "今は原音モードなので、B も原音と同じ音です（確かめ用の対照として使えます）。音作りの設定と比べるには、設定画面で音作りモードにしてから始めてください。";
    else if (this.kind() === "16bit" && s?.sourceFormat?.sampleType === "int" && s.sourceFormat.validBits <= 16)
      note = "この曲はもともと 16bit なので、B との違いはディザの雑音（−101 dBFS 程度）だけです。";
    e.kindNote.hidden = note === "";
    e.kindNote.textContent = note ? `⚠ ${note}` : "";
  }

  private async start() {
    const e = this.els;
    this.preparing = true;
    e.startBtn.disabled = true;
    e.preparing.hidden = false;
    e.error.hidden = true;
    const kind = this.kind();
    const r = await this.api.start({
      kind,
      startSeconds: Number(e.start.value),
      seconds: Number(e.length.value),
      trials: Number(e.trials.value),
      ...(kind === "presets" ? { a: this.presetFor(e.presetA.value), b: this.presetFor(e.presetB.value) } : {}),
    });
    this.preparing = false;
    e.preparing.hidden = true;
    e.startBtn.disabled = false;
    if (!r.ok) {
      e.error.hidden = false;
      e.error.textContent = r.message ?? "始められませんでした";
    }
  }

  // --- running ------------------------------------------------------------------------

  private renderRun(s: PlaybackStatus) {
    const a = s.abx!;
    const e = this.els;
    e.setup.hidden = true;
    e.result.hidden = true;
    e.run.hidden = false;
    e.trial.textContent = `${a.trial} / ${a.trials} 回目`;
    e.region.textContent = regionText(a.startFrames, a.lengthFrames, a.rate);
    const labels = sideLabels(a);
    e.aLabel.textContent = labels.a;
    e.bLabel.textContent = labels.b;
    for (const b of document.querySelectorAll<HTMLButtonElement>(".abx-choice")) {
      const on = b.dataset.choice === a.listening;
      b.classList.toggle("active", on);
      b.setAttribute("aria-pressed", String(on));
    }
    const sign = a.matchDb > 0 ? "+" : a.matchDb < 0 ? "−" : "±";
    e.match.textContent =
      `比べるもの：${labels.title}。B を ${sign}${Math.abs(a.matchDb).toFixed(2)} dB にして、A と同じ音量に合わせています（この区間で解析）。` +
      "答えると X が入れ替わります（聴いている音はそのまま）。";
  }

  // --- result ---------------------------------------------------------------------------

  private renderResult(s: PlaybackStatus) {
    const r = s.abx!.result!;
    const t = resultText(r);
    const e = this.els;
    e.setup.hidden = true;
    e.run.hidden = true;
    e.result.hidden = false;
    e.headline.textContent = t.headline;
    e.headline.classList.toggle("distinguished", r.distinguished);
    e.p.textContent = t.p;
    e.p.hidden = t.p === "";
    e.verdict.textContent = t.verdict;
    e.low.hidden = t.low === "";
    e.low.textContent = t.low ? `⚠ ${t.low}` : "";
    e.level.textContent = t.level;
    e.level.hidden = t.level === "";
  }
}
