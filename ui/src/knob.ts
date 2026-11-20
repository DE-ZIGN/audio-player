// A knob (task 6-3): drag up and down to turn it (Shift: fine), the wheel
// turns it too, double-click goes back to the default, and a click on the
// value lets you type it. Arrow keys when it has the focus. Drawn as an SVG
// arc in the theme's colours.

import { dragKnob, formatParam, toNorm, wheelKnob, type Param } from "./eqEdit";
import { onLangChange, t } from "./i18n";

const SVG = "http://www.w3.org/2000/svg";
const SWEEP = 270; // degrees of travel
const R = 22;

function arc(fromDeg: number, toDeg: number): string {
  const p = (deg: number) => {
    const a = ((deg - 90) * Math.PI) / 180;
    return `${(28 + R * Math.cos(a)).toFixed(2)} ${(28 + R * Math.sin(a)).toFixed(2)}`;
  };
  const large = toDeg - fromDeg > 180 ? 1 : 0;
  return `M ${p(fromDeg)} A ${R} ${R} 0 ${large} 1 ${p(toDeg)}`;
}

export interface KnobOptions {
  param: Param;
  // The knob's name; a function is read again when the language changes.
  label: string | (() => string);
  value: number;
  defaultValue: () => number;
  // A typed value: the number to use, or null when it is not one.
  parse: (text: string) => number | null;
  onChange: (value: number) => void;
  // Gain-like knobs light the arc from the centre (0 dB) rather than from the left.
  centred?: boolean;
}

export class Knob {
  readonly el: HTMLDivElement;
  private value: number;
  private disabled = false;
  private readonly track: SVGPathElement;
  private readonly fill: SVGPathElement;
  private readonly pointer: SVGLineElement;
  private readonly text: HTMLButtonElement;
  private readonly dial: HTMLDivElement;
  private readonly labelEl: HTMLDivElement;

  constructor(private readonly o: KnobOptions) {
    this.value = o.value;
    this.el = document.createElement("div");
    this.el.className = "knob";

    this.dial = document.createElement("div");
    this.dial.className = "knob-dial";
    this.dial.tabIndex = 0;
    this.dial.setAttribute("role", "slider");
    const svg = document.createElementNS(SVG, "svg");
    svg.setAttribute("viewBox", "0 0 56 56");
    this.track = document.createElementNS(SVG, "path");
    this.track.setAttribute("class", "knob-track");
    this.track.setAttribute("d", arc(-SWEEP / 2, SWEEP / 2));
    this.fill = document.createElementNS(SVG, "path");
    this.fill.setAttribute("class", "knob-fill");
    const cap = document.createElementNS(SVG, "circle");
    cap.setAttribute("class", "knob-cap");
    cap.setAttribute("cx", "28");
    cap.setAttribute("cy", "28");
    cap.setAttribute("r", "15");
    this.pointer = document.createElementNS(SVG, "line");
    this.pointer.setAttribute("class", "knob-pointer");
    svg.append(this.track, this.fill, cap, this.pointer);
    this.dial.append(svg);

    this.labelEl = document.createElement("div");
    this.labelEl.className = "knob-label";

    this.text = document.createElement("button");
    this.text.type = "button";
    this.text.className = "knob-value";
    this.text.addEventListener("click", () => this.edit());

    this.el.append(this.labelEl, this.dial, this.text);
    this.bind();
    this.render();
    this.name();
    // Named again in the language chosen; a knob taken off the page (a band's
    // knobs are made anew for each band) stops listening.
    const stop = onLangChange(() => {
      if (this.el.isConnected) this.name();
      else stop();
    });
  }

  private label(): string {
    return typeof this.o.label === "function" ? this.o.label() : this.o.label;
  }

  private name() {
    const label = this.label();
    this.dial.setAttribute("aria-label", label);
    this.dial.title = t("page.knob.title", { label });
    this.labelEl.textContent = label;
    this.text.title = t("page.knob.typeValue");
  }

  set(value: number) {
    this.value = value;
    this.render();
  }

  setDisabled(disabled: boolean) {
    this.disabled = disabled;
    this.el.classList.toggle("disabled", disabled);
    this.dial.tabIndex = disabled ? -1 : 0;
    this.text.disabled = disabled;
  }

  private commit(v: number) {
    if (v === this.value) return;
    this.value = v;
    this.render();
    this.o.onChange(v);
  }

  private bind() {
    let startY = 0;
    let startValue = 0;
    this.dial.addEventListener("pointerdown", (e) => {
      if (this.disabled || e.button !== 0) return;
      e.preventDefault();
      this.dial.focus();
      this.dial.setPointerCapture(e.pointerId);
      startY = e.clientY;
      startValue = this.value;
      this.el.classList.add("turning");
    });
    this.dial.addEventListener("pointermove", (e) => {
      if (!this.dial.hasPointerCapture(e.pointerId)) return;
      if (e.shiftKey !== (this.dial.dataset.fine === "1")) {
        // Shift pressed or released mid-drag: carry on from here.
        startY = e.clientY;
        startValue = this.value;
        this.dial.dataset.fine = e.shiftKey ? "1" : "";
      }
      this.commit(dragKnob(this.o.param, startValue, e.clientY - startY, e.shiftKey));
    });
    const end = (e: PointerEvent) => {
      if (this.dial.hasPointerCapture(e.pointerId)) this.dial.releasePointerCapture(e.pointerId);
      this.el.classList.remove("turning");
      this.dial.dataset.fine = "";
    };
    this.dial.addEventListener("pointerup", end);
    this.dial.addEventListener("pointercancel", end);
    this.dial.addEventListener(
      "wheel",
      (e) => {
        if (this.disabled) return;
        e.preventDefault();
        this.commit(wheelKnob(this.o.param, this.value, e.deltaY, e.shiftKey));
      },
      { passive: false },
    );
    this.dial.addEventListener("dblclick", () => !this.disabled && this.commit(this.o.defaultValue()));
    this.dial.addEventListener("keydown", (e) => {
      if (this.disabled) return;
      const dir = e.key === "ArrowUp" || e.key === "ArrowRight" ? -1 : e.key === "ArrowDown" || e.key === "ArrowLeft" ? 1 : 0;
      if (dir === 0) return;
      e.preventDefault();
      e.stopPropagation();
      this.commit(wheelKnob(this.o.param, this.value, dir, e.shiftKey));
    });
  }

  // Typing a value in place of the value text.
  private edit() {
    if (this.disabled) return;
    const input = document.createElement("input");
    input.type = "text";
    input.inputMode = "decimal";
    input.className = "knob-input";
    input.value = String(this.value);
    input.setAttribute("aria-label", t("page.knob.valueLabel", { label: this.label() }));
    let done = false;
    const finish = (apply: boolean) => {
      if (done) return;
      done = true;
      const v = apply ? this.o.parse(input.value) : null;
      input.replaceWith(this.text);
      if (v !== null) this.commit(v);
      this.text.focus();
    };
    input.addEventListener("keydown", (e) => {
      e.stopPropagation(); // Delete and Space belong to the field here
      if (e.key === "Enter") finish(true);
      else if (e.key === "Escape") {
        e.preventDefault();
        finish(false);
      }
    });
    input.addEventListener("blur", () => finish(true));
    this.text.replaceWith(input);
    input.focus();
    input.select();
  }

  private render() {
    const n = toNorm(this.o.param, this.value);
    const deg = -SWEEP / 2 + n * SWEEP;
    const from = this.o.centred ? Math.min(0, deg) : -SWEEP / 2;
    const to = this.o.centred ? Math.max(0, deg) : deg;
    this.fill.setAttribute("d", to - from < 0.5 ? "" : arc(from, to));
    const a = ((deg - 90) * Math.PI) / 180;
    this.pointer.setAttribute("x1", String(28 + 6 * Math.cos(a)));
    this.pointer.setAttribute("y1", String(28 + 6 * Math.sin(a)));
    this.pointer.setAttribute("x2", String(28 + 13 * Math.cos(a)));
    this.pointer.setAttribute("y2", String(28 + 13 * Math.sin(a)));
    this.text.textContent = formatParam(this.o.param, this.value);
    this.dial.setAttribute("aria-valuenow", String(this.value));
    this.dial.setAttribute("aria-valuetext", formatParam(this.o.param, this.value));
  }
}
