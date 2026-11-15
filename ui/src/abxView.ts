// The ABX dialog (task 6-6): set up (what to compare, the region, how many
// trials), run (A, B, X and the answers, by mouse or keys), and the result.
// Opened from the output check and from the settings screen. The test runs in
// the app; its state comes with the playback status.

import type { AbxApi } from "./backend";
import {
  defaultRegion,
  defaultSides,
  keyAction,
  regionText,
  resultText,
  sideLabels,
  sidePairProblem,
  sideRequest,
  type AbxChoice,
  type AbxKind,
  type SideChoice,
  type SideTube,
} from "./abx";
import type { EqPreset } from "./eqPresets";
import { activeBands, defaultListening, type ListeningSettings, type SettingsInfo } from "./settings";
import type { PlaybackStatus } from "./status";
import { clampUnit } from "./tubeCharacter";

interface SideEls {
  eq: HTMLSelectElement;
  tone: HTMLSelectElement;
  tube: HTMLSelectElement;
  drive: HTMLInputElement;
  warmUp: HTMLSelectElement; // task 9-8
  unit: HTMLInputElement;
}

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

function clock(seconds: number): string {
  const s = Math.max(0, Math.floor(seconds));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

export class AbxView {
  private status: PlaybackStatus | null = null;
  private preparing = false;
  private presets: EqPreset[] = []; // 設定 A と設定 B (task 6-9)
  private listening: ListeningSettings = structuredClone(defaultListening);
  private sidesSet = false; // the sides start from the defaults each time the dialog opens

  private readonly els = {
    dialog: $<HTMLDialogElement>("abx-dialog"),
    track: $("abx-track"),
    close: $<HTMLButtonElement>("abx-close"),
    setup: $("abx-setup"),
    kindNote: $("abx-kind-note"),
    presetBox: $("abx-presets"),
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
    private readonly loadSettings: () => Promise<SettingsInfo>,
  ) {
    const e = this.els;
    for (const side of [this.sideA, this.sideB])
      for (const el of [side.eq, side.tone, side.tube, side.drive, side.warmUp, side.unit])
        el.addEventListener("change", () => this.renderSetupNotes());
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

  private readonly sideA = this.sideEls("a");
  private readonly sideB = this.sideEls("b");

  private sideEls(k: "a" | "b"): SideEls {
    return {
      eq: $<HTMLSelectElement>(`abx-${k}-eq`),
      tone: $<HTMLSelectElement>(`abx-${k}-tone`),
      tube: $<HTMLSelectElement>(`abx-${k}-tube`),
      drive: $<HTMLInputElement>(`abx-${k}-drive`),
      warmUp: $<HTMLSelectElement>(`abx-${k}-warmup`),
      unit: $<HTMLInputElement>(`abx-${k}-unit`),
    };
  }

  private choice(side: SideEls): SideChoice {
    const drive = Number(side.drive.value);
    return {
      eq: side.eq.value,
      tone: side.tone.value === "flat" ? "flat" : "current",
      tube: side.tube.value as SideTube,
      drive: Number.isFinite(drive) ? Math.min(6, Math.max(-30, drive)) : this.listening.tubeDriveDb,
      warmUp: (["off", "now", "cold"] as const).find((w) => w === side.warmUp.value) ?? "off",
      unit: Number(side.unit.value) >= 1 ? clampUnit(Number(side.unit.value)) : 0,
    };
  }

  private setChoice(side: SideEls, c: SideChoice) {
    side.eq.value = c.eq;
    side.tone.value = c.tone;
    side.tube.value = c.tube;
    side.drive.value = String(c.drive);
    side.warmUp.value = c.warmUp ?? "off";
    side.unit.value = String(c.unit ?? 0);
  }

  open() {
    this.sidesSet = false;
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
    void this.refreshSides();
  }

  // The choices for 設定 A と設定 B: the EQ (the current one, none, the saved
  // presets), the tone (current or none), the tube and DRIVE. From the
  // defaults when the dialog opens; kept for "もう一度".
  private async refreshSides() {
    try {
      const s = await this.loadSettings();
      this.presets = s.eqPresets ?? [];
      this.listening = s.listening;
    } catch {
      this.presets = [];
    }
    const l = this.listening;
    const bands = activeBands(l);
    const tone = [l.toneBassDb !== 0 ? `BASS ${l.toneBassDb > 0 ? "+" : ""}${l.toneBassDb.toFixed(1)}` : "", l.toneTrebleDb !== 0 ? `TREBLE ${l.toneTrebleDb > 0 ? "+" : ""}${l.toneTrebleDb.toFixed(1)}` : ""].filter((t) => t);
    for (const side of [this.sideA, this.sideB]) {
      const before = side.eq.value;
      side.eq.replaceChildren(
        new Option(`今の EQ（${bands > 0 ? `${bands} バンド` : "かかりません"}）`, "current"),
        new Option("EQ なし", "flat"),
        ...this.presets.map((p) => new Option(p.name, `p:${p.name}`)),
      );
      if ([...side.eq.options].some((o) => o.value === before)) side.eq.value = before;
      side.tone.options[0].textContent = `今のトーン（${tone.length > 0 ? tone.join("、") + " dB" : "なし"}）`;
    }
    if (!this.sidesSet) {
      const d = defaultSides(l);
      this.setChoice(this.sideA, d.a);
      this.setChoice(this.sideB, d.b);
      this.sidesSet = true;
    }
    this.renderSetupNotes();
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
    for (const side of [this.sideA, this.sideB])
      for (const el of [side.drive, side.warmUp, side.unit]) el.disabled = side.tube.value === "none";
    const pairProblem = presets ? sidePairProblem(this.choice(this.sideA), this.choice(this.sideB)) : "";
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
      ...(kind === "presets"
        ? {
            a: sideRequest(this.choice(this.sideA), this.listening, this.presets),
            b: sideRequest(this.choice(this.sideB), this.listening, this.presets),
          }
        : {}),
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
