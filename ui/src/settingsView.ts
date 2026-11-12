// The settings screen (task 6-3): output, mode, ISP-safe gain, ReplayGain, EQ.
// Every change is sent to the app at once (which applies and saves it); EQ
// fields are sent a moment after typing stops.

import type { SettingsApi } from "./backend";
import {
  activeBands,
  curvePath,
  curveRange,
  dbText,
  defaultListening,
  defaultOutput,
  FILTER_LABELS,
  hasGain,
  ISP_LABELS,
  LIMITS,
  MAX_BANDS,
  modeSummary,
  newBand,
  outputOptions,
  parseField,
  REPLAY_GAIN_LABELS,
  selectedOutputValue,
  type EqBand,
  type EqCurve,
  type EqFilterType,
  type IspSafeMode,
  type ListeningSettings,
  type OutputChoice,
  type OutputOption,
  type OutputsInfo,
  type ReplayGainMode,
} from "./settings";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const SVG = "http://www.w3.org/2000/svg";
const CURVE = { width: 560, height: 160, minHz: 20, maxHz: 20000 };

export class SettingsView {
  private listening: ListeningSettings = structuredClone(defaultListening);
  private output: OutputChoice = { ...defaultOutput };
  private outputs: OutputsInfo = { wasapi: [], asio: [] };
  private options: OutputOption[] = [];
  private sendTimer: ReturnType<typeof setTimeout> | undefined;
  private curveSeq = 0;
  private loaded = false;

  private readonly els = {
    section: $("settings"),
    error: $("settings-error"),
    device: $<HTMLSelectElement>("output-device"),
    deviceStatus: $("output-device-status"),
    dop: $<HTMLInputElement>("output-dop"),
    summary: $("mode-summary"),
    ispOriginal: $<HTMLSelectElement>("isp-original"),
    ispProcessed: $<HTMLSelectElement>("isp-processed"),
    replayGain: $("replaygain"),
    eqEnabled: $<HTMLInputElement>("eq-enabled"),
    eqImport: $<HTMLButtonElement>("eq-import"),
    eqAdd: $<HTMLButtonElement>("eq-add"),
    eqClear: $<HTMLButtonElement>("eq-clear"),
    curve: document.getElementById("eq-curve") as unknown as SVGSVGElement,
    preamp: $("eq-preamp"),
    warnings: $("eq-warnings"),
    bands: document.querySelector<HTMLTableSectionElement>("#eq-bands tbody")!,
  };

  constructor(private readonly api: SettingsApi) {
    for (const sel of [this.els.ispOriginal, this.els.ispProcessed])
      for (const [value, label] of Object.entries(ISP_LABELS)) sel.add(new Option(label, value));
    this.els.replayGain.replaceChildren(
      ...Object.entries(REPLAY_GAIN_LABELS).map(([value, label]) => {
        const l = document.createElement("label");
        const r = document.createElement("input");
        r.type = "radio";
        r.name = "replaygain";
        r.value = value;
        r.addEventListener("change", () => r.checked && this.change({ replayGain: value as ReplayGainMode }));
        l.append(r, ` ${label}`);
        return l;
      }),
    );

    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="mode"]'))
      r.addEventListener("change", () => r.checked && this.change({ mode: r.value as ListeningSettings["mode"] }));
    this.els.ispOriginal.addEventListener("change", () =>
      this.change({ ispSafeOriginal: this.els.ispOriginal.value as IspSafeMode }),
    );
    this.els.ispProcessed.addEventListener("change", () =>
      this.change({ ispSafeProcessed: this.els.ispProcessed.value as IspSafeMode }),
    );
    this.els.eqEnabled.addEventListener("change", () => this.change({ eqEnabled: this.els.eqEnabled.checked }));
    this.els.eqAdd.addEventListener("click", () => {
      if (this.listening.eqBands.length >= MAX_BANDS) return;
      this.change({ eqBands: [...this.listening.eqBands, newBand(this.listening.eqBands)] }, true);
    });
    this.els.eqClear.addEventListener("click", () => {
      this.els.warnings.hidden = true;
      this.change({ eqBands: [] }, true);
    });
    this.els.eqImport.addEventListener("click", () => void this.importEq());
    this.els.device.addEventListener("change", () => void this.chooseOutput());
    this.els.dop.addEventListener("change", () => void this.setDop());
    this.api.onOutputsChanged(() => void this.refreshOutputs());
  }

  // Called when the screen is shown: reads what the app has now.
  async refresh() {
    try {
      const s = await this.api.get();
      this.listening = s.listening;
      this.output = s.output;
      this.loaded = true;
      this.renderListening(true);
      await this.refreshOutputs();
    } catch (e) {
      this.showError(`設定を読めませんでした: ${String(e)}`);
    }
  }

  // --- output ---------------------------------------------------------------

  private async refreshOutputs() {
    if (!this.loaded) return;
    this.outputs = await this.api.outputs();
    this.renderOutputs();
  }

  private renderOutputs() {
    this.options = outputOptions(this.outputs, this.output);
    const groups = { wasapi: this.group("WASAPI 排他"), asio: this.group("ASIO") };
    for (const o of this.options) {
      const opt = new Option(o.label, o.value);
      groups[o.group].append(opt);
    }
    this.els.device.replaceChildren(...[groups.wasapi, groups.asio].filter((g) => g.children.length > 0));
    this.els.device.value = selectedOutputValue(this.outputs, this.output);
    this.els.dop.checked = this.output.allowDop;
    const missing = this.options.find((o) => o.missing);
    this.showDeviceStatus(
      this.outputs.message ??
        (missing ? "選んである出力が今は見つかりません。つなぐと使えます（WASAPI は見つからない間、Windows の既定のデバイスで出します）。" : ""),
    );
  }

  private group(label: string) {
    const g = document.createElement("optgroup");
    g.label = label;
    return g;
  }

  private async chooseOutput() {
    const option = this.options.find((o) => o.value === this.els.device.value);
    if (!option) return;
    const choice: OutputChoice = { ...option.choice, allowDop: false }; // DoP only after the user says so
    this.els.device.disabled = true;
    this.els.error.hidden = true;
    this.showDeviceStatus(choice.kind === "asio" ? "ASIO ドライバーを別のプロセスで読み込んで確かめています…" : "", false);
    const r = await this.api.setOutput(choice);
    this.els.device.disabled = false;
    if (r.ok) {
      this.output = choice;
      this.showDeviceStatus("");
    } else {
      this.showDeviceStatus("");
      this.showError(r.message ?? "この出力は選べませんでした");
    }
    this.renderOutputs();
  }

  private async setDop() {
    this.els.error.hidden = true;
    const choice = { ...this.output, allowDop: this.els.dop.checked };
    const r = await this.api.setOutput(choice);
    if (r.ok) this.output = choice;
    else this.showError(r.message ?? "設定できませんでした");
    this.els.dop.checked = this.output.allowDop;
  }

  // A warning carries the triangle mark (CLAUDE.md chapter 8); progress does not.
  private showDeviceStatus(text: string, warning = true) {
    this.els.deviceStatus.hidden = text === "";
    this.els.deviceStatus.className = warning ? "notice" : "note";
    this.els.deviceStatus.textContent = text && warning ? `⚠ ${text}` : text;
  }

  // --- listening --------------------------------------------------------------

  private change(part: Partial<ListeningSettings>, redrawBands = false) {
    this.listening = { ...this.listening, ...part };
    this.els.error.hidden = true;
    this.renderListening(redrawBands);
    this.send(0);
  }

  // Sends the settings (after `delayMs`, restarted by every change).
  private send(delayMs: number) {
    clearTimeout(this.sendTimer);
    this.sendTimer = setTimeout(async () => {
      const r = await this.api.setListening(this.listening);
      if (!r.ok) this.showError(r.message ?? "設定を反映できませんでした");
    }, delayMs);
  }

  private renderListening(redrawBands: boolean) {
    const l = this.listening;
    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="mode"]')) r.checked = r.value === l.mode;
    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="replaygain"]'))
      r.checked = r.value === l.replayGain;
    this.els.ispOriginal.value = l.ispSafeOriginal;
    this.els.ispProcessed.value = l.ispSafeProcessed;
    this.els.eqEnabled.checked = l.eqEnabled;
    this.els.summary.textContent = modeSummary(l);
    this.els.section.classList.toggle("original-mode", l.mode === "original");
    this.els.eqAdd.disabled = l.eqBands.length >= MAX_BANDS;
    this.els.eqClear.disabled = l.eqBands.length === 0;
    if (redrawBands) this.renderBands();
    void this.drawCurve();
  }

  private renderBands() {
    const rows = this.listening.eqBands.map((b, i) => {
      const tr = document.createElement("tr");
      tr.classList.toggle("off", !b.enabled);

      const on = document.createElement("input");
      on.type = "checkbox";
      on.checked = b.enabled;
      on.setAttribute("aria-label", `バンド ${i + 1} を使う`);
      on.addEventListener("change", () => this.updateBand(i, { enabled: on.checked }, true));

      const type = document.createElement("select");
      type.setAttribute("aria-label", `バンド ${i + 1} の種類`);
      for (const [value, label] of Object.entries(FILTER_LABELS)) type.add(new Option(label, value));
      type.value = b.type;
      type.addEventListener("change", () => this.updateBand(i, { type: type.value as EqFilterType }, true));

      const field = (name: "frequency" | "gainDb" | "q", label: string) => {
        const input = document.createElement("input");
        input.type = "text";
        input.inputMode = "decimal";
        input.className = "num";
        input.value = String(b[name]);
        input.setAttribute("aria-label", `バンド ${i + 1} の${label}`);
        input.title = `${LIMITS[name].min} 〜 ${LIMITS[name].max}`;
        input.disabled = name === "gainDb" && !hasGain(b.type);
        input.addEventListener("input", () => {
          const x = parseField(name, input.value);
          input.classList.toggle("invalid", x === null);
          if (x !== null) this.updateBand(i, { [name]: x }, false, 250);
        });
        input.addEventListener("change", () => {
          // Show the value as it is used (limited).
          const x = parseField(name, input.value);
          input.value = String(x ?? this.listening.eqBands[i]?.[name] ?? "");
          input.classList.remove("invalid");
        });
        return input;
      };

      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "icon small plain";
      remove.textContent = "×";
      remove.title = "このバンドを消す";
      remove.setAttribute("aria-label", `バンド ${i + 1} を消す`);
      remove.addEventListener("click", () =>
        this.change({ eqBands: this.listening.eqBands.filter((_, j) => j !== i) }, true),
      );

      for (const el of [on, type, field("frequency", "周波数"), field("gainDb", "ゲイン"), field("q", "Q"), remove]) {
        const td = document.createElement("td");
        td.append(el);
        tr.append(td);
      }
      return tr;
    });
    if (rows.length === 0) {
      const tr = document.createElement("tr");
      const td = document.createElement("td");
      td.colSpan = 6;
      td.className = "empty-row";
      td.textContent = "バンドがありません（EQ はかかりません）";
      tr.append(td);
      rows.push(tr);
    }
    this.els.bands.replaceChildren(...rows);
  }

  private updateBand(i: number, part: Partial<EqBand>, redraw: boolean, delayMs = 0) {
    const bands = this.listening.eqBands.map((b, j) => (j === i ? { ...b, ...part } : b));
    this.listening = { ...this.listening, eqBands: bands };
    this.els.error.hidden = true;
    if (redraw) this.renderBands();
    this.els.summary.textContent = modeSummary(this.listening);
    void this.drawCurve();
    this.send(delayMs);
  }

  private async importEq() {
    const r = await this.api.importEq();
    if (r.cancelled) return;
    if (!r.ok || !r.bands) {
      this.showError(r.message ?? "読み込めませんでした");
      return;
    }
    const notes = [...(r.warnings ?? [])];
    if (r.bands.length > MAX_BANDS) notes.push(`バンドは ${MAX_BANDS} までです。残りは読み込みませんでした。`);
    if (r.filePreampDb !== undefined && r.filePreampDb !== null)
      notes.push(`ファイルのプリアンプ（${dbText(r.filePreampDb)}）は使わず、こちらで自動で決めます。`);
    this.els.warnings.hidden = notes.length === 0;
    this.els.warnings.textContent = notes.map((n) => `⚠ ${n}`).join("\n");
    this.change({ eqBands: r.bands.slice(0, MAX_BANDS), eqEnabled: true }, true);
  }

  // --- the curve ----------------------------------------------------------------

  private async drawCurve() {
    const seq = ++this.curveSeq;
    const l = this.listening;
    const used = l.eqEnabled ? l.eqBands : [];
    let c: EqCurve;
    try {
      c = await this.api.eqCurve(used);
    } catch {
      return;
    }
    if (seq !== this.curveSeq) return; // a newer one is on its way

    const maxHz = Math.min(CURVE.maxHz, c.points.at(-1)?.[0] ?? CURVE.maxHz);
    const frame = { ...CURVE, maxHz, rangeDb: curveRange(c.points, c.preampDb) };
    const svg = this.els.curve;
    const el = (name: string, attrs: Record<string, string | number>) => {
      const e = document.createElementNS(SVG, name);
      for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, String(v));
      return e;
    };
    const children: SVGElement[] = [];
    // Grid: octave-ish frequencies and every 6 dB.
    for (const hz of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
      if (hz >= maxHz) continue;
      const x = (Math.log(hz / CURVE.minHz) / Math.log(maxHz / CURVE.minHz)) * CURVE.width;
      children.push(el("line", { x1: x, x2: x, y1: 0, y2: CURVE.height, class: "grid" }));
      const t = el("text", { x: x + 3, y: CURVE.height - 4, class: "axis" });
      t.textContent = hz >= 1000 ? `${hz / 1000}k` : String(hz);
      children.push(t);
    }
    for (let db = -frame.rangeDb; db <= frame.rangeDb; db += frame.rangeDb / 2) {
      const y = ((frame.rangeDb - db) / (2 * frame.rangeDb)) * CURVE.height;
      children.push(el("line", { x1: 0, x2: CURVE.width, y1: y, y2: y, class: db === 0 ? "zero" : "grid" }));
      const t = el("text", { x: 4, y: Math.min(CURVE.height - 16, Math.max(11, y - 3)), class: "axis" });
      t.textContent = db === 0 ? "0 dB" : `${db > 0 ? "+" : "−"}${Math.abs(db)}`;
      children.push(t);
    }
    if (c.points.length > 0) {
      // The bands, and what comes out after the automatic preamp.
      children.push(el("path", { d: curvePath(c.points, frame), class: "curve" }));
      if (c.preampDb < 0) children.push(el("path", { d: curvePath(c.points, frame, c.preampDb), class: "curve-out" }));
    }
    svg.replaceChildren(...children);

    const original = l.mode === "original";
    const bands = activeBands(l);
    this.els.preamp.textContent =
      bands === 0
        ? "EQ はかかりません。"
        : `自動プリアンプ ${dbText(c.preampDb)}（${c.sampleRate / 1000} kHz で計算。実線が EQ、点線がプリアンプを含めた出力）。` +
          (c.skipped > 0 ? ` このレートでは作れないバンドが ${c.skipped} つあり、かけません。` : "") +
          (original ? " 今は原音モードなのでかけません。" : "");
  }

  private showError(text: string) {
    this.els.error.hidden = false;
    this.els.error.textContent = text;
  }
}
