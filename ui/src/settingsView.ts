// The settings screen (task 6-3): output, mode, ISP-safe gain, ReplayGain, EQ.
// Every change is sent to the app (which applies and saves it); while a knob
// or a node is being moved, a moment after it stops. The EQ is edited in its
// own dialog (eqEditor.ts).

import type { SettingsApi } from "./backend";
import { EqEditor } from "./eqEditor";
import { PresetBar, type PresetHost } from "./eqPresetBar";
import { applyPreset, type EqPreset } from "./eqPresets";
import { autoPreampDb, responsePoints } from "./eqResponse";
import {
  activeBands,
  curvePath,
  curveRange,
  dbText,
  defaultListening,
  defaultOutput,
  CROSSFEED_LEVELS,
  lowCutText,
  SPACE_LIMITS,
  SPACE_MEASURED,
  SPACE_ROOMS,
  spaceIrText,
  DRIVE_DEFAULT_DB,
  ISP_LABELS,
  MAX_BANDS,
  TUBE_LABELS,
  TUBE_NOTES,
  modeSummary,
  outputOptions,
  REPLAY_GAIN_LABELS,
  selectedOutputValue,
  type IspSafeMode,
  type ListeningSettings,
  type OutputChoice,
  type OutputOption,
  type OutputsInfo,
  type ReplayGainMode,
  type CrossfeedLevel,
  type SpaceIrSource,
  type SpaceIrStatus,
  type SpaceRoom,
  type TubeType,
} from "./settings";
import type { PlaybackStatus } from "./status";
import {
  OUTPUT_RATE_CHOICES,
  OUTPUT_RATE_MODES,
  OUTPUT_RATE_MULTIPLES,
  outputRateNote,
  rateText,
  type OutputRateMode,
} from "./outputRate";
import { playingTubeType } from "./amp";
import { TUBE_UNIT_MAX, clampUnit, unitChannelsText, unitLabel, warmUpText } from "./tubeCharacter";
import { TubeAutoView } from "./tubeAutoView";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const SVG = "http://www.w3.org/2000/svg";
const CURVE = { width: 560, height: 160, minHz: 20, maxHz: 20000 };
const RATE = 48000;
const SLIDER_SEND_MS = 150;

export class SettingsView {
  private listening: ListeningSettings = structuredClone(defaultListening);
  private output: OutputChoice = { ...defaultOutput };
  private outputs: OutputsInfo = { wasapi: [], asio: [] };
  private options: OutputOption[] = [];
  private sendTimer: ReturnType<typeof setTimeout> | undefined;
  private loaded = false;
  private readonly editor: EqEditor;
  private presets: EqPreset[] = []; // task 6-9, the app's list
  private readonly presetBars: PresetBar[];
  private readonly tubeAuto: TubeAutoView; // task 9-7
  private tubeAutoOn = false;

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
    eqOpen: $<HTMLButtonElement>("eq-open"),
    curve: document.getElementById("eq-curve") as unknown as SVGSVGElement,
    preamp: $("eq-preamp"),
    warnings: $("eq-warnings"),
    toneBass: $<HTMLInputElement>("tone-bass"),
    toneBassValue: $("tone-bass-value"),
    toneTreble: $<HTMLInputElement>("tone-treble"),
    toneTrebleValue: $("tone-treble-value"),
    toneReset: $<HTMLButtonElement>("tone-reset"),
    tubeEnabled: $<HTMLInputElement>("tube-enabled"),
    tubeType: $("tube-type"),
    tubeTypeNote: $("tube-type-note"),
    tubeDrive: $<HTMLInputElement>("tube-drive"),
    tubeDriveValue: $("tube-drive-value"),
    tubeDriveReset: $<HTMLButtonElement>("tube-drive-reset"),
    tubeWarmUp: $<HTMLInputElement>("tube-warmup"),
    tubeWarmUpNow: $("tube-warmup-now"),
    tubeUnits: $<HTMLInputElement>("tube-units"),
    tubeUnit: $<HTMLInputElement>("tube-unit"),
    tubeUnitNext: $<HTMLButtonElement>("tube-unit-next"),
    tubeUnitNow: $("tube-unit-now"),
    crossfeedEnabled: $<HTMLInputElement>("crossfeed-enabled"),
    crossfeedLevel: $("crossfeed-level"),
    rateMode: $("rate-mode"),
    rateHz: $<HTMLSelectElement>("rate-hz"),
    rateMultiple: $<HTMLSelectElement>("rate-multiple"),
    rateNote: $("rate-note"),
    spaceRoom: $("space-room"),
    spaceRoomNote: $("space-room-note"),
    spaceMix: $<HTMLInputElement>("space-mix"),
    spaceMixValue: $("space-mix-value"),
    spacePreDelay: $<HTMLInputElement>("space-predelay"),
    spacePreDelayValue: $("space-predelay-value"),
    spaceLowCut: $<HTMLInputElement>("space-lowcut"),
    spaceLowCutValue: $("space-lowcut-value"),
    spaceReset: $<HTMLButtonElement>("space-reset"),
    spaceIrs: $("space-irs"),
    spaceCredit: $("space-credit"),
  };
  private spaceIrs: SpaceIrStatus[] = []; // task 9-6: what became of the user IR files
  private warmUpSeconds: number | null = null; // task 9-8: since the app started (the amp's switch-on)
  private playingTube: TubeType | null = null;

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

    this.editor = new EqEditor(
      {
        listening: () => this.listening,
        update: (part, delayMs) => this.change(part, delayMs),
        importEq: () => this.importEq(),
        setSolo: (band) => void this.api.eqSolo(band),
      },
      (processed) => this.change({ mode: processed ? "processed" : "original" }),
    );

    // EQ presets (task 6-9): the same bar on this screen and in the EQ dialog.
    const presetHost: PresetHost = {
      chosen: "",
      listening: () => this.listening,
      presets: () => this.presets,
      // Choosing a preset puts its curve into the EQ and switches the EQ on (as reading a file does).
      apply: (p) => {
        const l = applyPreset(this.listening, p);
        this.change({ eqBands: l.eqBands, eqAutoPreamp: l.eqAutoPreamp, eqManualPreampDb: l.eqManualPreampDb, eqEnabled: true });
      },
      save: async (list) => {
        const r = await this.api.setEqPresets(list);
        if (!r.ok) return r.message ?? "プリセットを保存できませんでした";
        this.presets = r.eqPresets ?? list;
        this.renderPresets();
        return null;
      },
    };
    this.presetBars = [new PresetBar($("eq-presets"), presetHost), new PresetBar($("eqd-presets"), presetHost)];

    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="mode"]'))
      r.addEventListener("change", () => r.checked && this.change({ mode: r.value as ListeningSettings["mode"] }));
    this.els.ispOriginal.addEventListener("change", () =>
      this.change({ ispSafeOriginal: this.els.ispOriginal.value as IspSafeMode }),
    );
    this.els.ispProcessed.addEventListener("change", () =>
      this.change({ ispSafeProcessed: this.els.ispProcessed.value as IspSafeMode }),
    );
    this.els.eqEnabled.addEventListener("change", () => this.change({ eqEnabled: this.els.eqEnabled.checked }));
    this.els.eqImport.addEventListener("click", () => void this.importEq());
    this.els.eqOpen.addEventListener("click", () => this.editor.open());
    this.els.curve.addEventListener("click", () => this.editor.open());
    this.els.curve.addEventListener("keydown", (e) => {
      if (e.key === "Enter" || e.key === " ") {
        e.preventDefault();
        this.editor.open();
      }
    });
    // The tone and the tube (task 9-3): sliders send a moment after they stop.
    this.els.toneBass.addEventListener("input", () => this.change({ toneBassDb: Number(this.els.toneBass.value) }, SLIDER_SEND_MS));
    this.els.toneTreble.addEventListener("input", () =>
      this.change({ toneTrebleDb: Number(this.els.toneTreble.value) }, SLIDER_SEND_MS),
    );
    this.els.toneReset.addEventListener("click", () => this.change({ toneBassDb: 0, toneTrebleDb: 0 }));
    this.els.tubeEnabled.addEventListener("change", () => this.change({ tubeEnabled: this.els.tubeEnabled.checked }));
    this.els.tubeType.replaceChildren(
      ...(Object.entries(TUBE_LABELS) as [TubeType, string][]).map(([value, label]) => {
        const l = document.createElement("label");
        const r = document.createElement("input");
        r.type = "radio";
        r.name = "tube-type";
        r.value = value;
        r.addEventListener("change", () => r.checked && this.change({ tubeType: value }));
        l.append(r, ` ${label}`);
        return l;
      }),
    );
    this.els.tubeDrive.addEventListener("input", () => this.change({ tubeDriveDb: Number(this.els.tubeDrive.value) }, SLIDER_SEND_MS));
    this.els.tubeDriveReset.addEventListener("click", () => this.change({ tubeDriveDb: DRIVE_DEFAULT_DB }));
    // The show (task 9-8).
    this.els.tubeWarmUp.addEventListener("change", () => this.change({ tubeWarmUp: this.els.tubeWarmUp.checked }));
    this.els.tubeUnits.addEventListener("change", () => this.change({ tubeUnits: this.els.tubeUnits.checked }));
    this.els.tubeUnit.addEventListener("change", () => this.change({ tubeUnit: clampUnit(Number(this.els.tubeUnit.value)) }));
    this.els.tubeUnitNext.addEventListener("click", () =>
      this.change({ tubeUnit: (this.listening.tubeUnit % TUBE_UNIT_MAX) + 1, tubeUnits: true }),
    );
    // The crossfeed (task 9-5).
    this.els.crossfeedEnabled.addEventListener("change", () => this.change({ crossfeedEnabled: this.els.crossfeedEnabled.checked }));
    this.els.crossfeedLevel.replaceChildren(
      ...(Object.entries(CROSSFEED_LEVELS) as [CrossfeedLevel, { label: string }][]).map(([value, { label }]) => {
        const l = document.createElement("label");
        const r = document.createElement("input");
        r.type = "radio";
        r.name = "crossfeed-level";
        r.value = value;
        r.addEventListener("change", () => r.checked && this.change({ crossfeedLevel: value }));
        l.append(r, ` ${label}`);
        return l;
      }),
    );

    // The output rate (task 10-4).
    this.els.rateMode.replaceChildren(
      ...(Object.entries(OUTPUT_RATE_MODES) as [OutputRateMode, string][]).map(([value, label]) => {
        const l = document.createElement("label");
        const r = document.createElement("input");
        r.type = "radio";
        r.name = "rate-mode";
        r.value = value;
        r.addEventListener("change", () => r.checked && this.change({ outputRateMode: value }));
        l.append(r, ` ${label}`);
        return l;
      }),
    );
    this.els.rateHz.replaceChildren(
      ...OUTPUT_RATE_CHOICES.map((hz) => {
        const o = document.createElement("option");
        o.value = String(hz);
        o.textContent = rateText(hz);
        return o;
      }),
    );
    this.els.rateMultiple.replaceChildren(
      ...OUTPUT_RATE_MULTIPLES.map((k) => {
        const o = document.createElement("option");
        o.value = String(k);
        o.textContent = `${k} 倍`;
        return o;
      }),
    );
    this.els.rateHz.addEventListener("change", () => this.change({ outputRateHz: Number(this.els.rateHz.value) }));
    this.els.rateMultiple.addEventListener("change", () =>
      this.change({ outputRateMultiple: Number(this.els.rateMultiple.value) }),
    );

    // The space (task 9-6). The bundled measured IR's credit (CC BY 4.0).
    this.els.spaceCredit.textContent = Object.values(SPACE_MEASURED)
      .map((m) => m.credit)
      .join(" ");
    const rooms: [SpaceRoom, string][] = [["off", "OFF"], ...Object.entries(SPACE_ROOMS).map(([k, v]) => [k as SpaceRoom, v.label] as [SpaceRoom, string])];
    this.els.spaceRoom.replaceChildren(
      ...rooms.map(([value, label]) => {
        const l = document.createElement("label");
        const r = document.createElement("input");
        r.type = "radio";
        r.name = "space-room";
        r.value = value;
        r.addEventListener("change", () => r.checked && this.change({ spaceRoom: value }));
        l.append(r, ` ${label}`);
        return l;
      }),
    );
    this.els.spaceMix.addEventListener("input", () => this.change({ spaceMixPercent: Number(this.els.spaceMix.value) }, SLIDER_SEND_MS));
    this.els.spacePreDelay.addEventListener("input", () =>
      this.change({ spacePreDelayMs: Number(this.els.spacePreDelay.value) }, SLIDER_SEND_MS),
    );
    // The low cut's slider: its left end (10) is "none".
    this.els.spaceLowCut.addEventListener("input", () => {
      const v = Number(this.els.spaceLowCut.value);
      this.change({ spaceLowCutHz: v < SPACE_LIMITS.lowCut.min ? 0 : v }, SLIDER_SEND_MS);
    });
    this.els.spaceReset.addEventListener("click", () =>
      this.change({
        spaceMixPercent: SPACE_LIMITS.mix.defaultValue,
        spacePreDelayMs: SPACE_LIMITS.preDelay.defaultValue,
        spaceLowCutHz: SPACE_LIMITS.lowCut.defaultValue,
      }),
    );

    this.tubeAuto = new TubeAutoView(
      {
        ...this.api,
        setTubeAuto: async (t) => {
          const r = await this.api.setTubeAuto(t);
          if (r.ok) {
            this.tubeAutoOn = t.enabled;
            this.els.summary.textContent = modeSummary(this.listening, this.tubeAutoOn);
            this.notify();
          }
          return r;
        },
      },
      () => this.listening.tubeType,
    );

    this.els.device.addEventListener("change", () => void this.chooseOutput());
    this.els.dop.addEventListener("change", () => void this.setDop());
    this.api.onOutputsChanged(() => void this.refreshOutputs());
  }

  // --- for the amp view (task 9-9): the same settings, changed from its knobs ---

  private readonly listeners: (() => void)[] = [];

  // The listening settings as the app has them; null until they are read.
  get current(): ListeningSettings | null {
    return this.loaded ? this.listening : null;
  }

  get tubeAutoEnabled(): boolean {
    return this.tubeAutoOn;
  }

  changeListening(part: Partial<ListeningSettings>, delayMs = 0) {
    if (this.loaded) this.change(part, delayMs);
  }

  onListening(listener: () => void) {
    this.listeners.push(listener);
  }

  private notify() {
    for (const l of this.listeners) l();
  }

  // Called when the screen is shown: reads what the app has now.
  async refresh() {
    try {
      const s = await this.api.get();
      this.listening = s.listening;
      this.output = s.output;
      this.presets = s.eqPresets ?? [];
      this.spaceIrs = s.spaceIrs ?? [];
      if (s.tubeAuto) {
        this.tubeAutoOn = s.tubeAuto.enabled;
        this.tubeAuto.load(s.tubeAuto, s.tubeAutoDefaultRules ?? []);
      }
      this.loaded = true;
      this.renderListening();
      await this.refreshOutputs();
    } catch (e) {
      this.showError(`設定を読めませんでした: ${String(e)}`);
    }
  }

  // The output check's button for the ISP warning (task 6-4): the ISP-safe
  // gain of the mode in use, measured.
  async enableIspSafe(mode: ListeningSettings["mode"]) {
    if (!this.loaded) await this.refresh(); // never send defaults over what is saved
    if (!this.loaded) return;
    this.change(mode === "processed" ? { ispSafeProcessed: "measured" } : { ispSafeOriginal: "measured" });
  }

  // The playback status (for the EQ editor: the limiter's count, the solo).
  onStatus(s: PlaybackStatus) {
    this.editor.showStatus(s.limiter, s.eqSolo);
    // The warm-up's time (task 9-8): only the minutes are shown.
    const t = s.tubeWarmUpSeconds ?? null;
    const minuteChanged = t === null || this.warmUpSeconds === null || Math.floor(t / 60) !== Math.floor(this.warmUpSeconds / 60);
    this.warmUpSeconds = t;
    this.playingTube = playingTubeType(s);
    if (minuteChanged) this.renderTubeShow();
    this.tubeAuto.onStatus(s);
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
    for (const o of this.options) groups[o.group].append(new Option(o.label, o.value));
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
    this.showDeviceStatus("");
    if (r.ok) this.output = choice;
    else this.showError(r.message ?? "この出力は選べませんでした");
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

  // Changes the settings and sends them after `delayMs` (restarted by every change).
  private change(part: Partial<ListeningSettings>, delayMs = 0) {
    this.listening = { ...this.listening, ...part };
    this.els.error.hidden = true;
    this.renderListening();
    clearTimeout(this.sendTimer);
    this.sendTimer = setTimeout(async () => {
      const r = await this.api.setListening(this.listening);
      if (!r.ok) this.showError(r.message ?? "設定を反映できませんでした");
      if (r.spaceIrs) {
        this.spaceIrs = r.spaceIrs;
        this.renderSpace();
      }
    }, delayMs);
  }

  private renderListening() {
    const l = this.listening;
    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="mode"]')) r.checked = r.value === l.mode;
    for (const r of document.querySelectorAll<HTMLInputElement>('input[name="replaygain"]'))
      r.checked = r.value === l.replayGain;
    this.els.ispOriginal.value = l.ispSafeOriginal;
    this.els.ispProcessed.value = l.ispSafeProcessed;
    this.els.eqEnabled.checked = l.eqEnabled;
    this.renderToneAndTube();
    this.els.summary.textContent = modeSummary(l, this.tubeAutoOn);
    this.els.section.classList.toggle("original-mode", l.mode === "original");
    this.drawCurve();
    this.editor.render();
    this.renderPresets();
    this.notify();
  }

  private renderToneAndTube() {
    const l = this.listening;
    const e = this.els;
    const db = (v: number) => `${v > 0 ? "+" : ""}${v.toFixed(1)} dB`;
    // Leave a slider alone while it is being dragged (it already shows the value).
    if (document.activeElement !== e.toneBass) e.toneBass.value = String(l.toneBassDb);
    if (document.activeElement !== e.toneTreble) e.toneTreble.value = String(l.toneTrebleDb);
    if (document.activeElement !== e.tubeDrive) e.tubeDrive.value = String(l.tubeDriveDb);
    e.toneBassValue.textContent = db(l.toneBassDb);
    e.toneTrebleValue.textContent = db(l.toneTrebleDb);
    e.tubeDriveValue.textContent = db(l.tubeDriveDb);
    e.tubeEnabled.checked = l.tubeEnabled;
    for (const r of e.tubeType.querySelectorAll<HTMLInputElement>('input[name="tube-type"]')) r.checked = r.value === l.tubeType;
    e.tubeTypeNote.textContent = TUBE_NOTES[l.tubeType];
    this.tubeAuto?.renderNow(); // names the manual type
    this.renderTubeShow();
    e.crossfeedEnabled.checked = l.crossfeedEnabled;
    for (const r of e.crossfeedLevel.querySelectorAll<HTMLInputElement>('input[name="crossfeed-level"]'))
      r.checked = r.value === l.crossfeedLevel;
    this.renderSpace();
    this.renderRate();
  }

  // The output rate (task 10-4): the rate for 固定・下限, the multiple for 整数倍.
  private renderRate() {
    const l = this.listening;
    const e = this.els;
    for (const r of e.rateMode.querySelectorAll<HTMLInputElement>('input[name="rate-mode"]')) r.checked = r.value === l.outputRateMode;
    e.rateHz.value = String(l.outputRateHz);
    e.rateMultiple.value = String(l.outputRateMultiple);
    e.rateHz.disabled = l.outputRateMode !== "fixed" && l.outputRateMode !== "atleast";
    e.rateMultiple.disabled = l.outputRateMode !== "multiple";
    e.rateNote.textContent = outputRateNote(l);
  }

  // The tube's show (task 9-8): the warm-up's time and the unit's tubes.
  private renderTubeShow() {
    const l = this.listening;
    const e = this.els;
    e.tubeWarmUp.checked = l.tubeWarmUp;
    e.tubeUnits.checked = l.tubeUnits;
    if (document.activeElement !== e.tubeUnit) e.tubeUnit.value = String(l.tubeUnit);
    e.tubeUnit.disabled = !l.tubeUnits;
    e.tubeWarmUpNow.textContent = !l.tubeWarmUp
      ? ""
      : this.warmUpSeconds === null
        ? "起動からの時間に合わせて移ります。"
        : warmUpText(this.warmUpSeconds);
    const type = this.playingTube ?? l.tubeType;
    e.tubeUnitNow.textContent = l.tubeUnits ? `${unitLabel(l.tubeUnit)}（${TUBE_LABELS[type]}）：${unitChannelsText(l.tubeUnit, type)}` : "";
  }

  // The space (task 9-6): the room, the sliders and each room's IR file.
  private renderSpace() {
    const l = this.listening;
    const e = this.els;
    for (const r of e.spaceRoom.querySelectorAll<HTMLInputElement>('input[name="space-room"]')) r.checked = r.value === l.spaceRoom;
    if (document.activeElement !== e.spaceMix) e.spaceMix.value = String(l.spaceMixPercent);
    if (document.activeElement !== e.spacePreDelay) e.spacePreDelay.value = String(l.spacePreDelayMs);
    if (document.activeElement !== e.spaceLowCut) e.spaceLowCut.value = String(l.spaceLowCutHz <= 0 ? 10 : l.spaceLowCutHz);
    e.spaceMixValue.textContent = `${Math.round(l.spaceMixPercent)}%`;
    e.spacePreDelayValue.textContent = `${Math.round(l.spacePreDelayMs)} ms${l.spacePreDelayMs < 3 ? "（最小 約 3 ms）" : ""}`;
    e.spaceLowCutValue.textContent = lowCutText(l.spaceLowCutHz);
    e.spaceRoomNote.textContent =
      l.spaceRoom === "off"
        ? "OFF：空間をかけません。"
        : `${SPACE_ROOMS[l.spaceRoom].label}：${spaceIrText(l, l.spaceRoom, this.spaceIrs[SPACE_ROOMS[l.spaceRoom].index])}`;
    // Each room: which IR it plays (measured where there is one, synthesized, the user's file).
    e.spaceIrs.replaceChildren(
      ...(Object.entries(SPACE_ROOMS) as [Exclude<SpaceRoom, "off">, (typeof SPACE_ROOMS)["club"]][]).map(([room, info]) => {
        const i = info.index;
        const li = document.createElement("li");
        li.className = "space-ir";
        const head = document.createElement("strong");
        head.textContent = info.label;
        const radios = document.createElement("span");
        radios.className = "radios";
        radios.setAttribute("role", "radiogroup");
        radios.setAttribute("aria-label", `${info.label} の IR`);
        const path = l.spaceIr[i];
        const fileName = path.split(/[\\/]/).pop() ?? path;
        const options: [SpaceIrSource, string][] = [
          ...(SPACE_MEASURED[room] ? ([["measured", "実測（St Paul's Hall）"]] as [SpaceIrSource, string][]) : []),
          ["synth", "合成"],
          ["file", path === "" ? "自分のファイル" : `自分のファイル（${fileName}）`],
        ];
        const chosen = l.spaceSource[i];
        for (const [value, label] of options) {
          const lab = document.createElement("label");
          const r = document.createElement("input");
          r.type = "radio";
          r.name = `space-source-${room}`;
          r.value = value;
          r.checked = value === chosen;
          r.disabled = value === "file" && path === ""; // choose a file first
          r.addEventListener("change", () => r.checked && this.setSource(i, value));
          lab.append(r, ` ${label}`);
          radios.append(lab);
        }
        const choose = document.createElement("button");
        choose.type = "button";
        choose.textContent = "ファイルを選ぶ…";
        choose.addEventListener("click", () => void this.chooseIr(i));
        const st = this.spaceIrs[i];
        const note = document.createElement("p");
        const text = spaceIrText(l, room, st);
        note.className = text.startsWith("⚠") ? "notice" : "note";
        note.textContent = text;
        li.append(head, " ", radios, " ", choose, note);
        return li;
      }),
    );
  }

  private async chooseIr(index: number) {
    const r = await this.api.chooseIr();
    if (!r.ok || !r.path) return;
    const ir = [...this.listening.spaceIr] as [string, string, string];
    ir[index] = r.path;
    const source = [...this.listening.spaceSource] as [SpaceIrSource, SpaceIrSource, SpaceIrSource];
    source[index] = "file"; // a chosen file is used at once
    this.change({ spaceIr: ir, spaceSource: source });
  }

  private setSource(index: number, value: SpaceIrSource) {
    const source = [...this.listening.spaceSource] as [SpaceIrSource, SpaceIrSource, SpaceIrSource];
    source[index] = value;
    this.change({ spaceSource: source });
  }

  private renderPresets() {
    for (const b of this.presetBars) b.render();
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
      notes.push(`ファイルのプリアンプ（${dbText(r.filePreampDb)}）は使いません（プリアンプは自動か、手で決めます）。`);
    this.els.warnings.hidden = notes.length === 0;
    this.els.warnings.textContent = notes.map((n) => `⚠ ${n}`).join("\n");
    this.editor.showWarnings(notes);
    this.change({ eqBands: r.bands.slice(0, MAX_BANDS), eqEnabled: true });
  }

  // --- the small curve (a preview; the editor has the large one) ---------------

  private drawCurve() {
    const l = this.listening;
    const used = l.eqEnabled ? l.eqBands : [];
    const points = responsePoints(used, RATE);
    const preamp = l.eqAutoPreamp ? autoPreampDb(used, RATE) : l.eqManualPreampDb;
    const frame = { ...CURVE, rangeDb: curveRange(points, preamp) };
    const el = (name: string, attrs: Record<string, string | number>) => {
      const e = document.createElementNS(SVG, name);
      for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, String(v));
      return e;
    };
    const children: SVGElement[] = [];
    for (const hz of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
      const x = (Math.log(hz / CURVE.minHz) / Math.log(CURVE.maxHz / CURVE.minHz)) * CURVE.width;
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
    const bands = activeBands(l);
    children.push(el("path", { d: curvePath(points, frame), class: "curve" }));
    if (bands > 0 && preamp < 0) children.push(el("path", { d: curvePath(points, frame, preamp), class: "curve-out" }));
    this.els.curve.replaceChildren(...children);

    this.els.preamp.textContent =
      bands === 0
        ? "EQ はかかりません。"
        : `${bands} バンド、プリアンプ ${dbText(preamp)}（${l.eqAutoPreamp ? "自動" : "手動"}）。実線が EQ、点線がプリアンプを含めた出力。` +
          (l.mode === "original" ? " 今は原音モードなのでかけません。" : "");
  }

  private showError(text: string) {
    this.els.error.hidden = false;
    this.els.error.textContent = text;
  }
}
