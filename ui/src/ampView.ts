// The amp view (task 9-9, CLAUDE.md chapter 8): a tube amp drawn from its
// concept images (UI-AMP-SE-v1 and UI-AMP-SE-PURE-v2), with the standard
// view's panels under it. The pictures have their print, lamps and pointers
// removed (tools/ui_amp_parts.py); the page draws the words, lights the
// lamps, turns the pointers and moves the VU needles. 原音モード and
// 音作りモード change only the lights, what is enabled and what is shown,
// never the layout: the tubes fade between the dark picture and the lit one.
//
// The same parts as the other views: the transport, the title, the seek bar
// and the output field are moved here while the amp view is shown (and back
// after), so they keep their handlers; the knobs set the same settings as
// the settings screen, and VOLUME moves the volume slider.

import {
  AMP_SIZE,
  AMP_TOP,
  ampState,
  BADGE,
  BUTTONS,
  dragValue,
  knobAngle,
  KNOB_SPECS,
  KNOBS,
  knobText,
  LAMPS,
  modeChange,
  stepValue,
  TOGGLE,
  VOLUME_READOUT,
  VU,
  WORDS,
  type AmpState,
  type KnobName,
  type LampName,
} from "./amp";
import { sliderToDb, volumeLabel } from "./queueOps";
import { TUBE_LABELS, type ListeningSettings, type TubeType } from "./settings";
import type { Level, PlaybackStatus } from "./status";
import { currentUiView, onUiViewChange } from "./uiView";
import { needleAngle, VU_MARKS, vuOf } from "./vuMeter";
import sePure from "./textures/amp/se-pure.jpg";
import seLit from "./textures/amp/se-lit.jpg";
import toggleUp from "./textures/amp/toggle-up.jpg";
import toggleDown from "./textures/amp/toggle-down.jpg";

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
    private readonly name: KnobName,
    private readonly label: string,
    private readonly onChange: (v: number) => void,
  ) {
    const k = KNOBS[name];
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

  set(v: number) {
    if (this.el.classList.contains("turning")) return; // the drag shows it
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

  constructor(ch: "L" | "R") {
    const { paper, hump } = VU[ch];
    const w = paper.x1 - paper.x0;
    const h = paper.y1 - paper.y0;
    // As on a real meter (and the standard view's): the pivot under the paper,
    // hidden by the bezel; the scale on an arc above it.
    const cx = (paper.x0 + paper.x1) / 2;
    const cy = 335;
    const R = 110;
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
    const label = svg("text", { x: cx, y: 283, class: "vu-label" });
    label.textContent = "VU";
    this.el.append(label);
    this.needle = svg("line", { x1: cx, y1: cy, x2: cx, y2: cy - R - 8, class: "vu-needle" });
    this.needle.style.transformOrigin = `${cx}px ${cy}px`;
    this.el.append(this.needle);
    // The black hump over the pivot, as the picture has it (the needle goes under it).
    this.el.append(svg("path", { d: `M ${hump.x - 22} ${hump.y + 1} A 22 12 0 0 1 ${hump.x + 22} ${hump.y + 1} Z`, class: "amp-vu-hump" }));
    this.set("-inf");
  }

  set(dbfs: Level | undefined) {
    this.needle.style.transform = `rotate(${needleAngle(vuOf(dbfs ?? "-inf")).toFixed(2)}deg)`;
  }
}

// --- the view ------------------------------------------------------------------------------------

export class AmpView {
  private readonly stage: HTMLDivElement;
  private readonly knobs: Record<KnobName, AmpKnob>;
  private readonly lamps = new Map<LampName, HTMLButtonElement>();
  private readonly toggle: HTMLButtonElement;
  private readonly badge: HTMLButtonElement;
  private readonly vus: [AmpVu, AmpVu];
  private readonly now: HTMLElement;
  private status: PlaybackStatus | null = null;
  private moved: { el: Element; mark: Comment }[] = [];
  private shownState = "";

  constructor(private readonly host: AmpHost) {
    const root = document.getElementById("amp")!;
    this.now = document.getElementById("amp-now")!;
    this.stage = html("div", "amp-stage", { width: px(AMP_SIZE.width), height: px(AMP_SIZE.height) });
    const pure = html("img", "amp-scene amp-scene-pure") as HTMLImageElement;
    pure.src = sePure;
    pure.alt = "";
    const lit = html("img", "amp-scene amp-scene-lit") as HTMLImageElement;
    lit.src = seLit;
    lit.alt = "";
    this.stage.append(pure, lit);

    for (const w of WORDS) {
      const d = html("div", `amp-word${w.align === "left" ? " left" : ""}`, {
        left: px(w.x),
        top: px(top(w.y)),
        fontSize: px(w.size),
      });
      d.replaceChildren(...w.text.map((t) => Object.assign(html("span", ""), { textContent: t })));
      if (w.text[0] === "CROSSFEED") d.dataset.word = "crossfeed";
      if (w.text[0] === "AUTO") d.dataset.word = "auto";
      this.stage.append(d);
    }

    // The MODE switch and its lamps.
    this.toggle = html("button", "amp-toggle", {
      left: px(TOGGLE.x),
      top: px(top(TOGGLE.y)),
      width: px(TOGGLE.width),
      height: px(TOGGLE.height),
    }) as HTMLButtonElement;
    this.toggle.type = "button";
    const up = html("img", "amp-toggle-up") as HTMLImageElement;
    up.src = toggleUp;
    up.alt = "";
    const down = html("img", "amp-toggle-down") as HTMLImageElement;
    down.src = toggleDown;
    down.alt = "";
    this.toggle.append(up, down);
    this.toggle.addEventListener("click", () => this.setMode(this.state().mode === "tube" ? "pure" : "tube"));
    this.stage.append(this.toggle);

    const lampTitles: Record<LampName, string> = {
      pure: "PURE：原音モード（処理をかけません。音量 100% で BIT-PERFECT になりえます）",
      tube: "TUBE：音作りモードで真空管をかけます",
      off: "SPACE OFF：空間（IR の畳み込み）は 9-6 で作ります。今はいつもオフです",
      club: "SPACE CLUB：空間（IR の畳み込み）は 9-6 で作ります。今は選べません",
      hall: "SPACE HALL：空間（IR の畳み込み）は 9-6 で作ります。今は選べません",
      arena: "SPACE ARENA：空間（IR の畳み込み）は 9-6 で作ります。今は選べません",
      se300b: `TUBE TYPE：${TUBE_LABELS.se300b}`,
      ppel34: `TUBE TYPE：${TUBE_LABELS.ppel34}`,
      ppclassa: `TUBE TYPE：${TUBE_LABELS.ppclassa}`,
      crossfeed: "CROSSFEED：ヘッドホン向けのクロスフィード（強さは設定画面で）",
      auto: "AUTO：曲のジャンルで真空管の種類を選んでいます（設定画面の「ジャンルで種類を選ぶ」）",
    };
    for (const [name, l] of Object.entries(LAMPS) as [LampName, (typeof LAMPS)[LampName]][]) {
      const b = html("button", `amp-lamp amp-lamp-${name}`, {
        left: px(l.x - l.r),
        top: px(top(l.y) - l.r),
        width: px(2 * l.r),
        height: px(2 * l.r),
      }) as HTMLButtonElement;
      b.type = "button";
      b.title = lampTitles[name];
      b.setAttribute("aria-label", lampTitles[name].split("：")[0]);
      this.lamps.set(name, b);
      this.stage.append(b);
    }
    this.lamps.get("pure")!.addEventListener("click", () => this.setMode("pure"));
    this.lamps.get("tube")!.addEventListener("click", () => this.setMode("tube"));
    for (const t of ["se300b", "ppel34", "ppclassa"] as TubeType[])
      this.lamps.get(t)!.addEventListener("click", () => this.host.change({ tubeType: t }));
    this.lamps.get("crossfeed")!.addEventListener("click", () => {
      const l = this.host.listening();
      if (l) this.host.change({ crossfeedEnabled: !l.crossfeedEnabled });
    });
    for (const n of ["off", "club", "hall", "arena", "auto"] as LampName[]) this.lamps.get(n)!.disabled = true;

    // The knobs.
    const send = (part: Partial<ListeningSettings>) => this.host.change(part, 150);
    this.knobs = {
      drive: new AmpKnob("drive", "DRIVE", (v) => send({ tubeDriveDb: v })),
      bass: new AmpKnob("bass", "BASS", (v) => send({ toneBassDb: v })),
      treble: new AmpKnob("treble", "TREBLE", (v) => send({ toneTrebleDb: v })),
      volume: new AmpKnob("volume", "VOLUME（デジタル音量。100% なら処理をしません）", (v) => this.setVolume(v)),
      mix: new AmpKnob("mix", "MIX", () => undefined),
    };
    this.knobs.mix.setDisabled(true, "空間（IR の畳み込み）は 9-6 で作ります。今は動きません");
    for (const k of Object.values(this.knobs)) this.stage.append(k.el);

    this.vus = [new AmpVu("L"), new AmpVu("R")];
    this.stage.append(...this.vus.map((v) => v.el));
    for (const ch of ["L", "R"] as const) {
      const c = html("span", "amp-ch", { left: px(VU[ch].letter.x), top: px(top(VU[ch].letter.y)) });
      c.textContent = ch;
      this.stage.append(c);
    }

    this.badge = html("button", "amp-badge", {
      left: px(BADGE.x - BADGE.width / 2),
      top: px(top(BADGE.y) - BADGE.height / 2),
      width: px(BADGE.width),
      height: px(BADGE.height),
    }) as HTMLButtonElement;
    this.badge.type = "button";
    this.badge.title = "出力の確認（経路と BIT-PERFECT の判定）";
    this.badge.addEventListener("click", () => this.host.openOutputCheck());
    this.stage.append(this.badge);

    // Where the transport and the volume's readout go while the amp is shown.
    const transport = html("div", "amp-transport-slot");
    transport.dataset.slot = "transport";
    const readout = html("div", "amp-volume-slot", { left: px(VOLUME_READOUT.x), top: px(top(VOLUME_READOUT.y)) });
    readout.dataset.slot = "volume";
    this.stage.append(transport, readout);
    BUTTONS.forEach((b, i) => this.stage.style.setProperty(`--b${i}x`, px(b.x - b.r)));

    root.append(this.stage);
    host.onListening(() => this.render());
    onUiViewChange((v) => this.place(v === "amp"));
    this.place(currentUiView() === "amp");
  }

  update(s: PlaybackStatus) {
    this.status = s;
    const r = s.meter.reading;
    const vu = s.meter.state === "measuring" && r !== null ? r.vu : [];
    this.vus[0].set(vu[0]);
    this.vus[1].set(vu.length > 1 ? vu[1] : vu[0]);
    this.knobs.volume.set(Number(this.host.volumeSlider.value) / 1000);
    this.render();
  }

  private state(): AmpState {
    return ampState(this.host.listening(), this.status, this.host.tubeAuto());
  }

  private setMode(to: "pure" | "tube") {
    if (this.host.listening()) this.host.change(modeChange(to));
  }

  private setVolume(v: number) {
    const slider = this.host.volumeSlider;
    slider.value = String(Math.round(v * 1000));
    slider.dispatchEvent(new Event("input"));
    if (!this.knobs?.volume.el.classList.contains("turning")) slider.dispatchEvent(new Event("change"));
  }

  private render() {
    const a = this.state();
    const l = this.host.listening();
    if (l) {
      this.knobs.drive.set(l.tubeDriveDb);
      this.knobs.bass.set(l.toneBassDb);
      this.knobs.treble.set(l.toneTrebleDb);
    }
    const key = JSON.stringify(a);
    if (key === this.shownState) return;
    this.shownState = key;
    const st = this.stage;
    st.dataset.mode = a.mode;
    st.dataset.lit = a.tubesLit ? "1" : "0";
    st.dataset.look = a.look;
    this.toggle.classList.toggle("down", a.mode === "tube");
    this.toggle.title = a.mode === "tube" ? "MODE：TUBE（音作りモード）→ 押すと PURE（原音モード）" : "MODE：PURE（原音モード）→ 押すと TUBE（音作りモードで真空管）";
    this.toggle.setAttribute("aria-label", this.toggle.title);
    const lit = (n: LampName, on: boolean) => this.lamps.get(n)!.classList.toggle("on", on);
    lit("pure", a.mode === "pure");
    lit("tube", a.mode === "tube");
    lit("off", true); // there is no space stage yet: it is off
    for (const t of ["se300b", "ppel34", "ppclassa"] as TubeType[]) lit(t, a.type === t);
    lit("crossfeed", a.crossfeed);
    lit("auto", a.auto);
    const why = "音作りモード（TUBE）でかかります";
    this.knobs.drive.setDisabled(!a.processingControls, why);
    this.knobs.bass.setDisabled(!a.processingControls, why);
    this.knobs.treble.setDisabled(!a.processingControls, why);
    for (const n of ["se300b", "ppel34", "ppclassa", "crossfeed"] as LampName[]) this.lamps.get(n)!.disabled = !a.processingControls;
    for (const n of ["se300b", "ppel34", "ppclassa", "crossfeed", "off", "auto"] as LampName[])
      this.lamps.get(n)!.classList.toggle("dim", !a.processingControls);
    this.badge.textContent = a.badge.text;
    this.badge.dataset.kind = a.badge.kind;
    this.badge.disabled = a.badge.kind === "none";
  }

  // Moves the shared parts in (amp view) or back where they were.
  private place(amp: boolean) {
    if (!amp) {
      for (const { el, mark } of this.moved) mark.replaceWith(el);
      this.moved = [];
      this.now.hidden = true;
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
    const cover = this.now.querySelector(".amp-now-cover")!;
    const info = this.now.querySelector(".amp-now-info")!;
    move(document.getElementById("now-cover"), cover);
    move(document.getElementById("now-cover-empty"), cover);
    move(document.querySelector(".topbar .now"), info);
    move(document.querySelector(".topbar .seek"), info);
    move(document.getElementById("output"), info);
    move(document.querySelector(".topbar .transport"), this.stage.querySelector('[data-slot="transport"]')!);
    const vol = this.stage.querySelector('[data-slot="volume"]')!;
    move(document.getElementById("mute"), vol);
    move(document.getElementById("vol-label"), vol);
    this.now.hidden = false;
  }
}
