// The amp view (task 9-9, CLAUDE.md chapter 8): a tube amp drawn from its
// concept images, with the standard view's panels under it. There are two
// amps (the user, 2026-11-14): the single-ended one (UI-AMP-SE, for SE 300B)
// and the push-pull one (UI-AMP-PP, for PP EL34 and PP CLASS A), each with
// its own panel; the amp shown follows the tube type, and they fade into one
// another. Both have the same controls.
//
// The pictures have their print, lit lamps and buttons and pointers removed
// (tools/ui_amp_parts.py, tools/ui_amp_pp_parts.py); the page draws the words,
// lights the lamps and buttons, turns the pointers and moves the VU needles.
// 原音モード and 音作りモード change only the lights, what is enabled and what is
// shown, never the layout: the tubes fade between the dark picture and the lit one.
//
// The same parts as the other views: the transport, the title, the seek bar
// and the output field are moved here while the amp view is shown (and back
// after), so they keep their handlers; the knobs set the same settings as
// the settings screen, and VOLUME moves the volume slider.

import {
  AMP_SIZE,
  AMP_TOP,
  ampState,
  dragValue,
  knobAngle,
  KNOB_SPECS,
  knobText,
  LAYOUTS,
  modeChange,
  stepValue,
  type AmpLayout,
  type AmpLook,
  type AmpState,
  type Box,
  type KeyName,
  type KnobName,
  type LampName,
  type TransportName,
  type VuPlace,
} from "./amp";
import { sliderToDb, volumeLabel } from "./queueOps";
import { TUBE_LABELS, type ListeningSettings, type SpaceRoom, type TubeType } from "./settings";
import type { Level, PlaybackStatus } from "./status";
import { currentUiView, onUiViewChange } from "./uiView";
import { needleAngle, VU_MARKS, vuOf } from "./vuMeter";
import sePure from "./textures/amp/se-pure.jpg";
import seLit from "./textures/amp/se-lit.jpg";
import ppPure from "./textures/amp/pp-pure.jpg";
import ppLit from "./textures/amp/pp-lit.jpg";
import toggleUp from "./textures/amp/toggle-up.png";
import toggleDown from "./textures/amp/toggle-down.png";
import xfeedOn from "./textures/amp/pp-xfeed-on.png";
import xfeedOff from "./textures/amp/pp-xfeed-off.png";
import seButton from "./textures/amp/button.jpg";
import sePlay from "./textures/amp/play.jpg";
import ppButton from "./textures/amp/pp-button.jpg";
import ppPlay from "./textures/amp/pp-play.jpg";

const PICTURES: Record<AmpLook, { pure: string; lit: string; button: string; play: string }> = {
  se: { pure: sePure, lit: seLit, button: seButton, play: sePlay },
  pp: { pure: ppPure, lit: ppLit, button: ppButton, play: ppPlay },
};

const SVG = "http://www.w3.org/2000/svg";

function svg<K extends keyof SVGElementTagNameMap>(name: K, attrs: Record<string, string | number>) {
  const e = document.createElementNS(SVG, name);
  for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, String(v));
  return e;
}

function html<K extends keyof HTMLElementTagNameMap>(name: K, cls: string, style: Partial<CSSStyleDeclaration> = {}) {
  const e = document.createElement(name);
  e.className = cls;
  Object.assign(e.style, style);
  return e;
}

const px = (v: number) => `${v}px`;
const top = (y: number) => y - AMP_TOP; // image y to the picture's own y
const boxStyle = (b: Box): Partial<CSSStyleDeclaration> => ({
  left: px(b.x0),
  top: px(top(b.y0)),
  width: px(b.x1 - b.x0),
  height: px(b.y1 - b.y0),
});

// What the amp needs from the rest of the page.
export interface AmpHost {
  listening(): ListeningSettings | null; // null until the settings are read
  tubeAuto(): boolean;
  change(part: Partial<ListeningSettings>, delayMs?: number): void;
  onListening(listener: () => void): void;
  volumeSlider: HTMLInputElement;
  openOutputCheck(): void;
}

// --- a knob on the picture: its body is drawn, the page turns a pointer --------------

class AmpKnob {
  readonly el: HTMLDivElement;
  private readonly pointer: SVGGElement;
  private value: number;
  private disabled = false;

  constructor(
    layout: AmpLayout,
    private readonly name: KnobName,
    private readonly label: string,
    private readonly onChange: (v: number) => void,
  ) {
    const k = layout.knobs[name];
    const spec = KNOB_SPECS[name];
    this.value = spec.defaultValue;
    this.el = html("div", `amp-knob amp-knob-${name}`, {
      left: px(k.x - k.r),
      top: px(top(k.y) - k.r),
      width: px(2 * k.r),
      height: px(2 * k.r),
    });
    this.el.tabIndex = 0;
    this.el.setAttribute("role", "slider");
    this.el.setAttribute("aria-label", label);
    const s = svg("svg", { viewBox: `${-k.r} ${-k.r} ${2 * k.r} ${2 * k.r}`, "aria-hidden": "true" });
    this.pointer = svg("g", { class: "amp-pointer" });
    if (name === "volume") {
      // A line of light from the centre to the rim, as the picture had it.
      this.pointer.append(svg("line", { x1: 0, y1: -0.14 * k.r, x2: 0, y2: -0.93 * k.r, class: "amp-pointer-glow" }));
      this.pointer.append(svg("line", { x1: 0, y1: -0.14 * k.r, x2: 0, y2: -0.93 * k.r, class: "amp-pointer-light" }));
    } else {
      // An engraved line near the rim.
      this.pointer.append(svg("line", { x1: 0.6, y1: -0.5 * k.r, x2: 0.6, y2: -0.86 * k.r, class: "amp-pointer-edge" }));
      this.pointer.append(svg("line", { x1: 0, y1: -0.5 * k.r, x2: 0, y2: -0.86 * k.r, class: "amp-pointer-line" }));
    }
    s.append(this.pointer);
    this.el.append(s);
    this.bind();
    this.render();
  }

  get turning() {
    return this.el.classList.contains("turning");
  }

  set(v: number) {
    if (this.turning) return; // the drag shows it
    this.value = v;
    this.render();
  }

  setDisabled(disabled: boolean, why = "") {
    this.disabled = disabled;
    this.el.classList.toggle("disabled", disabled);
    this.el.setAttribute("aria-disabled", String(disabled));
    this.el.tabIndex = disabled ? -1 : 0;
    this.el.dataset.why = why;
    this.render();
  }

  private commit(v: number) {
    if (v === this.value) return;
    this.value = v;
    this.render();
    this.onChange(v);
  }

  private bind() {
    const spec = KNOB_SPECS[this.name];
    let startY = 0;
    let startValue = 0;
    let fine = false;
    this.el.addEventListener("pointerdown", (e) => {
      if (this.disabled || e.button !== 0) return;
      e.preventDefault();
      this.el.focus();
      this.el.setPointerCapture(e.pointerId);
      startY = e.clientY;
      startValue = this.value;
      fine = e.shiftKey;
      this.el.classList.add("turning");
    });
    this.el.addEventListener("pointermove", (e) => {
      if (!this.el.hasPointerCapture(e.pointerId)) return;
      if (e.shiftKey !== fine) {
        startY = e.clientY;
        startValue = this.value;
        fine = e.shiftKey;
      }
      this.commit(dragValue(spec, startValue, e.clientY - startY, fine));
    });
    const end = (e: PointerEvent) => {
      if (this.el.hasPointerCapture(e.pointerId)) this.el.releasePointerCapture(e.pointerId);
      this.el.classList.remove("turning");
      if (this.name === "volume") this.onChange(this.value); // the end of a drag
    };
    this.el.addEventListener("pointerup", end);
    this.el.addEventListener("pointercancel", end);
    this.el.addEventListener(
      "wheel",
      (e) => {
        if (this.disabled) return;
        e.preventDefault();
        this.commit(stepValue(spec, this.value, -e.deltaY, e.shiftKey));
      },
      { passive: false },
    );
    this.el.addEventListener("dblclick", () => !this.disabled && this.commit(spec.defaultValue));
    this.el.addEventListener("keydown", (e) => {
      if (this.disabled) return;
      const dir = e.key === "ArrowUp" || e.key === "ArrowRight" ? 1 : e.key === "ArrowDown" || e.key === "ArrowLeft" ? -1 : 0;
      if (dir === 0) return;
      e.preventDefault();
      e.stopPropagation();
      this.commit(stepValue(spec, this.value, dir, e.shiftKey));
    });
  }

  private render() {
    const spec = KNOB_SPECS[this.name];
    this.pointer.setAttribute("transform", `rotate(${knobAngle(spec, this.value).toFixed(2)})`);
    const text = this.name === "volume" ? volumePercent(this.value) : knobText(this.name, this.value);
    this.el.setAttribute("aria-valuenow", String(this.value));
    this.el.setAttribute("aria-valuetext", text);
    const why = this.el.dataset.why;
    this.el.title = this.disabled
      ? `${this.label}：${why || "使えません"}`
      : `${this.label}：${text}（上下にドラッグ、Shift で細かく。ホイール、ダブルクリックで既定値）`;
  }
}

// As the volume's own label says it (100% at the top, then dB).
function volumePercent(slider: number): string {
  return volumeLabel(sliderToDb(slider));
}

// --- the VU meters, drawn on the picture's paper ------------------------------------------

class AmpVu {
  readonly el: SVGSVGElement;
  private readonly needle: SVGLineElement;

  constructor(ch: "L" | "R", place: VuPlace) {
    const { paper, hump, pivotY: cy, radius: R, labelY } = place;
    const w = paper.x1 - paper.x0;
    const h = paper.y1 - paper.y0;
    // As on a real meter (and the standard view's): the pivot under the paper,
    // hidden by the bezel; the scale on an arc above it.
    const cx = (paper.x0 + paper.x1) / 2;
    const point = (angle: number, radius: number) => {
      const a = ((angle - 90) * Math.PI) / 180;
      return { x: cx + radius * Math.cos(a), y: cy + radius * Math.sin(a) };
    };
    this.el = svg("svg", {
      class: "amp-vu",
      viewBox: `${paper.x0} ${paper.y0} ${w} ${h}`,
      role: "img",
      "aria-label": `VU メーター ${ch}`,
    }) as SVGSVGElement;
    Object.assign(this.el.style, { left: px(paper.x0), top: px(top(paper.y0)), width: px(w), height: px(h) });
    const a0 = point(needleAngle(0), R - 4);
    const a3 = point(needleAngle(3), R - 4);
    this.el.append(svg("path", { d: `M ${a0.x} ${a0.y} A ${R - 4} ${R - 4} 0 0 1 ${a3.x} ${a3.y}`, class: "vu-red" }));
    const s0 = point(needleAngle(-20), R);
    const s1 = point(needleAngle(3), R);
    this.el.append(svg("path", { d: `M ${s0.x} ${s0.y} A ${R} ${R} 0 0 1 ${s1.x} ${s1.y}`, class: "vu-arc" }));
    for (const m of VU_MARKS) {
      const angle = needleAngle(m);
      const p1 = point(angle, R);
      const p2 = point(angle, R + (m % 5 === 0 || m >= -3 ? 10 : 6));
      this.el.append(svg("line", { x1: p1.x, y1: p1.y, x2: p2.x, y2: p2.y, class: m > 0 ? "vu-tick red" : "vu-tick" }));
      if ([-20, -10, -7, -5, -3, -1, 0, 1, 3].includes(m)) {
        const t = point(angle, R + 20);
        const text = svg("text", { x: t.x, y: t.y + 5, class: m > 0 ? "vu-num red" : "vu-num" });
        text.textContent = m > 0 ? `+${m}` : String(m);
        this.el.append(text);
      }
    }
    const label = svg("text", { x: cx, y: labelY, class: "vu-label" });
    label.textContent = "VU";
    this.el.append(label);
    this.needle = svg("line", { x1: cx, y1: cy, x2: cx, y2: cy - R - 8, class: "vu-needle" });
    this.needle.style.transformOrigin = `${cx}px ${cy}px`;
    this.el.append(this.needle);
    // The black hump over the pivot, as the picture has it (the needle goes under it).
    const r = hump.r;
    this.el.append(svg("path", { d: `M ${hump.x - r} ${hump.y + 1} A ${r} ${r * 0.55} 0 0 1 ${hump.x + r} ${hump.y + 1} Z`, class: "amp-vu-hump" }));
    this.set("-inf");
  }

  set(dbfs: Level | undefined) {
    this.needle.style.transform = `rotate(${needleAngle(vuOf(dbfs ?? "-inf")).toFixed(2)}deg)`;
  }
}

// --- one amp: its picture and controls -------------------------------------------------------

const LAMP_TITLES: Record<LampName, string> = {
  pure: "PURE：原音モード（処理をかけません。音量 100% で BIT-PERFECT になりえます）",
  tube: "TUBE：音作りモードで真空管をかけます",
  off: "SPACE OFF：空間（部屋の響きの畳み込み）をかけません",
  club: "SPACE CLUB：小さな部屋の響きを畳み込んで混ぜます（IR は合成か自分のファイル。MIX・プリディレイとあわせて設定画面で）",
  hall: "SPACE HALL：ホールの響きを畳み込んで混ぜます（IR は実測の St Paul's Hall・合成・自分のファイル。MIX・プリディレイとあわせて設定画面で）",
  arena: "SPACE ARENA：大きな屋内空間の響きを畳み込んで混ぜます（IR は合成か自分のファイル。MIX・プリディレイとあわせて設定画面で）",
  se300b: `TUBE TYPE：${TUBE_LABELS.se300b}`,
  ppel34: `TUBE TYPE：${TUBE_LABELS.ppel34}`,
  ppclassa: `TUBE TYPE：${TUBE_LABELS.ppclassa}`,
  crossfeed: "CROSSFEED：ヘッドホン向けのクロスフィード（強さは設定画面で）",
  auto: "AUTO：曲のジャンルで真空管の種類を選んでいます（設定画面の「ジャンルで種類を選ぶ」）",
};

const KEY_LABELS: Record<KeyName, string> = {
  pure: "PURE",
  tube: "TUBE",
  se300b: "SE 300B",
  ppel34: "PP EL34",
  ppclassa: "PP CLASS A",
  off: "OFF",
  club: "CLUB",
  hall: "HALL",
  arena: "ARENA",
};

const TUBE_TYPES: TubeType[] = ["se300b", "ppel34", "ppclassa"];
const SPACES = ["off", "club", "hall", "arena"] as const;

class AmpPanel {
  readonly stage: HTMLDivElement;
  readonly knobs: Record<KnobName, AmpKnob>;
  private readonly lamps = new Map<LampName, HTMLButtonElement>();
  private readonly keys = new Map<KeyName, HTMLButtonElement>();
  private readonly modeToggle: HTMLButtonElement | null = null;
  private readonly crossfeedToggle: HTMLButtonElement | null = null;
  private readonly badge: HTMLButtonElement;
  private readonly vus: [AmpVu, AmpVu];
  readonly transportSlot: HTMLElement;
  readonly volumeSlot: HTMLElement;

  constructor(
    readonly layout: AmpLayout,
    private readonly host: AmpHost,
    private readonly setMode: (to: "pure" | "tube") => void,
    private readonly setVolume: (v: number, turning: boolean) => void,
  ) {
    const pic = PICTURES[layout.look];
    this.stage = html("div", `amp-stage amp-stage-${layout.look}`, { width: px(AMP_SIZE.width), height: px(AMP_SIZE.height) });
    this.stage.dataset.look = layout.look;
    // The round buttons cut from this picture, and where the transport goes on it.
    this.stage.style.setProperty("--button", `url("${pic.button}")`);
    this.stage.style.setProperty("--button-play", `url("${pic.play}")`);
    for (const [name, b] of Object.entries(layout.transport) as [TransportName, (typeof layout.transport)[TransportName]][]) {
      this.stage.style.setProperty(`--${name}-x`, px(b.x - b.r));
      this.stage.style.setProperty(`--${name}-y`, px(top(b.y) - b.r));
      this.stage.style.setProperty(`--${name}-d`, px(2 * b.r));
    }
    const pure = html("img", "amp-scene amp-scene-pure") as HTMLImageElement;
    pure.src = pic.pure;
    pure.alt = "";
    const lit = html("img", "amp-scene amp-scene-lit") as HTMLImageElement;
    lit.src = pic.lit;
    lit.alt = "";
    this.stage.append(pure, lit);

    for (const w of layout.words) {
      const d = html("div", `amp-word${w.align === "left" ? " left" : ""}`, {
        left: px(w.x),
        top: px(top(w.y)),
        fontSize: px(w.size),
      });
      d.replaceChildren(...w.text.map((t) => Object.assign(html("span", ""), { textContent: t })));
      this.stage.append(d);
    }

    // SE: the MODE switch. PP: the CROSSFEED switch.
    if (layout.modeToggle) {
      this.modeToggle = this.toggle(layout.modeToggle, toggleUp, toggleDown);
      this.modeToggle.addEventListener("click", () => this.setMode(this.modeToggle!.classList.contains("down") ? "pure" : "tube"));
    }
    if (layout.crossfeedToggle) {
      this.crossfeedToggle = this.toggle(layout.crossfeedToggle, xfeedOn, xfeedOff);
      this.crossfeedToggle.addEventListener("click", () => this.flipCrossfeed());
    }

    for (const [name, l] of Object.entries(layout.lamps) as [LampName, { x: number; y: number; r: number }][]) {
      const b = html("button", `amp-lamp amp-lamp-${name}`, {
        left: px(l.x - l.r),
        top: px(top(l.y) - l.r),
        width: px(2 * l.r),
        height: px(2 * l.r),
      }) as HTMLButtonElement;
      b.type = "button";
      b.title = LAMP_TITLES[name];
      b.setAttribute("aria-label", LAMP_TITLES[name].split("：")[0]);
      this.lamps.set(name, b);
      this.stage.append(b);
    }
    for (const [name, k] of Object.entries(layout.keys) as [KeyName, Box][]) {
      const b = html("button", `amp-key amp-key-${name}`, boxStyle(k)) as HTMLButtonElement;
      b.type = "button";
      b.textContent = KEY_LABELS[name];
      b.title = LAMP_TITLES[name];
      this.keys.set(name, b);
      this.stage.append(b);
    }
    const control = (name: LampName | KeyName) => this.lamps.get(name as LampName) ?? this.keys.get(name as KeyName);
    control("pure")?.addEventListener("click", () => this.setMode("pure"));
    control("tube")?.addEventListener("click", () => this.setMode("tube"));
    for (const t of TUBE_TYPES) control(t)?.addEventListener("click", () => this.host.change({ tubeType: t }));
    this.lamps.get("crossfeed")?.addEventListener("click", () => this.flipCrossfeed());
    for (const n of SPACES) control(n)?.addEventListener("click", () => this.host.change({ spaceRoom: n }));
    const auto = control("auto");
    if (auto) auto.disabled = true;

    // The knobs.
    const send = (part: Partial<ListeningSettings>) => this.host.change(part, 150);
    this.knobs = {
      drive: new AmpKnob(layout, "drive", "DRIVE", (v) => send({ tubeDriveDb: v })),
      bass: new AmpKnob(layout, "bass", "BASS", (v) => send({ toneBassDb: v })),
      treble: new AmpKnob(layout, "treble", "TREBLE", (v) => send({ toneTrebleDb: v })),
      volume: new AmpKnob(layout, "volume", "VOLUME（デジタル音量。100% なら処理をしません）", (v) =>
        this.setVolume(v, this.knobs.volume.turning),
      ),
      mix: new AmpKnob(layout, "mix", "MIX（空間の響きの割合。0% で元の音だけ）", (v) => send({ spaceMixPercent: v })),
    };
    for (const k of Object.values(this.knobs)) this.stage.append(k.el);

    this.vus = [new AmpVu("L", layout.vu.L), new AmpVu("R", layout.vu.R)];
    this.stage.append(...this.vus.map((v) => v.el));
    for (const ch of ["L", "R"] as const) {
      const c = html("span", "amp-ch", { left: px(layout.vu[ch].letter.x), top: px(top(layout.vu[ch].letter.y)) });
      c.textContent = ch;
      this.stage.append(c);
    }

    const bd = layout.badge;
    this.badge = html("button", "amp-badge", {
      left: px(bd.x - bd.width / 2),
      top: px(top(bd.y) - bd.height / 2),
      width: px(bd.width),
      height: px(bd.height),
    }) as HTMLButtonElement;
    this.badge.type = "button";
    this.badge.title = "出力の確認（経路と BIT-PERFECT の判定）";
    this.badge.addEventListener("click", () => this.host.openOutputCheck());
    this.stage.append(this.badge);

    // Where the transport and the volume's readout go while this amp is shown.
    this.transportSlot = html("div", "amp-transport-slot");
    this.volumeSlot = html("div", "amp-volume-slot", { left: px(layout.readout.x), top: px(top(layout.readout.y)) });
    this.stage.append(this.transportSlot, this.volumeSlot);
  }

  private toggle(b: Box, up: string, down: string): HTMLButtonElement {
    const t = html("button", "amp-toggle", boxStyle(b)) as HTMLButtonElement;
    t.type = "button";
    const u = html("img", "amp-toggle-up") as HTMLImageElement;
    u.src = up;
    u.alt = "";
    const d = html("img", "amp-toggle-down") as HTMLImageElement;
    d.src = down;
    d.alt = "";
    t.append(u, d);
    this.stage.append(t);
    return t;
  }

  private flipCrossfeed() {
    const l = this.host.listening();
    if (l) this.host.change({ crossfeedEnabled: !l.crossfeedEnabled });
  }

  meters(s: PlaybackStatus) {
    const r = s.meter.reading;
    const vu = s.meter.state === "measuring" && r !== null ? r.vu : [];
    this.vus[0].set(vu[0]);
    this.vus[1].set(vu.length > 1 ? vu[1] : vu[0]);
  }

  render(a: AmpState, l: ListeningSettings | null, volume: number) {
    if (l) {
      this.knobs.drive.set(l.tubeDriveDb);
      this.knobs.bass.set(l.toneBassDb);
      this.knobs.treble.set(l.toneTrebleDb);
      this.knobs.mix.set(l.spaceMixPercent);
    }
    this.knobs.volume.set(volume);
    const st = this.stage;
    st.dataset.mode = a.mode;
    st.dataset.lit = a.tubesLit ? "1" : "0";
    if (this.modeToggle) {
      const t = this.modeToggle;
      t.classList.toggle("down", a.mode === "tube");
      t.title = a.mode === "tube" ? "MODE：TUBE（音作りモード）→ 押すと PURE（原音モード）" : "MODE：PURE（原音モード）→ 押すと TUBE（音作りモードで真空管）";
      t.setAttribute("aria-label", t.title);
    }
    if (this.crossfeedToggle) {
      const t = this.crossfeedToggle;
      t.classList.toggle("down", !a.crossfeed);
      t.disabled = !a.processingControls;
      t.classList.toggle("dim", !a.processingControls);
      t.title = `CROSSFEED：${a.crossfeed ? "オン" : "オフ"}（ヘッドホン向けのクロスフィード。強さは設定画面で。音作りモードでかかります）`;
      t.setAttribute("aria-label", t.title);
    }
    const on = new Map<LampName | KeyName, boolean>([
      ["pure", a.mode === "pure"],
      ["tube", a.mode === "tube"],
      ...SPACES.map((n) => [n, a.space === n] as [SpaceRoom, boolean]),
      ...TUBE_TYPES.map((t) => [t, a.type === t] as [TubeType, boolean]),
      ["crossfeed", a.crossfeed],
      ["auto", a.auto],
    ]);
    const onlyProcessing = new Set<LampName | KeyName>([...TUBE_TYPES, "crossfeed", ...SPACES, "auto"]);
    for (const [name, c] of [...this.lamps, ...this.keys] as [LampName | KeyName, HTMLButtonElement][]) {
      c.classList.toggle("on", on.get(name) ?? false);
      c.classList.toggle("dim", onlyProcessing.has(name) && !a.processingControls);
      if (TUBE_TYPES.includes(name as TubeType) || name === "crossfeed" || (SPACES as readonly string[]).includes(name))
        c.disabled = !a.processingControls;
      if (this.keys.has(name as KeyName)) c.setAttribute("aria-pressed", String(on.get(name) ?? false));
    }
    const why = "音作りモード（TUBE）でかかります";
    this.knobs.drive.setDisabled(!a.processingControls, why);
    this.knobs.bass.setDisabled(!a.processingControls, why);
    this.knobs.treble.setDisabled(!a.processingControls, why);
    this.knobs.mix.setDisabled(!a.processingControls, why);
    this.badge.textContent = a.badge.text;
    this.badge.dataset.kind = a.badge.kind;
    this.badge.disabled = a.badge.kind === "none";
  }

  show(on: boolean) {
    this.stage.classList.toggle("active", on);
    this.stage.inert = !on;
  }
}

// --- the view ------------------------------------------------------------------------------------

export class AmpView {
  private readonly panels: Record<AmpLook, AmpPanel>;
  private active: AmpLook = "se";
  private readonly now: HTMLElement;
  private status: PlaybackStatus | null = null;
  private moved: { el: Element; mark: Comment }[] = [];
  private shownState = "";
  private placed = false;

  constructor(private readonly host: AmpHost) {
    const root = document.getElementById("amp")!;
    this.now = document.getElementById("amp-now")!;
    const setMode = (to: "pure" | "tube") => {
      if (this.host.listening()) this.host.change(modeChange(to));
    };
    const setVolume = (v: number, turning: boolean) => {
      const slider = this.host.volumeSlider;
      slider.value = String(Math.round(v * 1000));
      slider.dispatchEvent(new Event("input"));
      if (!turning) slider.dispatchEvent(new Event("change"));
    };
    this.panels = {
      se: new AmpPanel(LAYOUTS.se, host, setMode, setVolume),
      pp: new AmpPanel(LAYOUTS.pp, host, setMode, setVolume),
    };
    root.append(this.panels.se.stage, this.panels.pp.stage);
    this.panels.se.show(true);
    this.panels.pp.show(false);
    host.onListening(() => this.render());
    onUiViewChange((v) => this.place(v === "amp"));
    this.place(currentUiView() === "amp");
  }

  update(s: PlaybackStatus) {
    this.status = s;
    this.panels[this.active].meters(s);
    this.render();
  }

  private render() {
    const a = ampState(this.host.listening(), this.status, this.host.tubeAuto());
    if (a.look !== this.active) this.switchTo(a.look);
    const volume = Number(this.host.volumeSlider.value) / 1000;
    const l = this.host.listening();
    const key = JSON.stringify([a, l, volume]);
    if (key === this.shownState) return;
    this.shownState = key;
    for (const p of Object.values(this.panels)) p.render(a, l, volume);
  }

  // The amp for the tube type: the other one fades in, and the transport
  // and the volume's readout move onto it.
  private switchTo(look: AmpLook) {
    this.active = look;
    this.panels.se.show(look === "se");
    this.panels.pp.show(look === "pp");
    if (this.placed) {
      const p = this.panels[look];
      const transport = document.querySelector(".amp-stage .transport");
      if (transport) p.transportSlot.append(transport);
      for (const id of ["mute", "vol-label"]) {
        const el = document.getElementById(id);
        if (el?.closest(".amp-stage")) p.volumeSlot.append(el);
      }
    }
    if (this.status) this.panels[look].meters(this.status);
  }

  // Moves the shared parts in (amp view) or back where they were.
  private place(amp: boolean) {
    if (!amp) {
      for (const { el, mark } of this.moved) mark.replaceWith(el);
      this.moved = [];
      this.now.hidden = true;
      this.placed = false;
      return;
    }
    if (this.moved.length) return;
    const move = (el: Element | null, to: Element) => {
      if (!el) return;
      const mark = document.createComment("amp");
      el.replaceWith(mark);
      to.append(el);
      this.moved.push({ el, mark });
    };
    const p = this.panels[this.active];
    const cover = this.now.querySelector(".amp-now-cover")!;
    const info = this.now.querySelector(".amp-now-info")!;
    move(document.getElementById("now-cover"), cover);
    move(document.getElementById("now-cover-empty"), cover);
    move(document.querySelector(".topbar .now"), info);
    move(document.querySelector(".topbar .seek"), info);
    move(document.getElementById("output"), info);
    move(document.querySelector(".topbar .transport"), p.transportSlot);
    move(document.getElementById("mute"), p.volumeSlot);
    move(document.getElementById("vol-label"), p.volumeSlot);
    this.now.hidden = false;
    this.placed = true;
  }
}
