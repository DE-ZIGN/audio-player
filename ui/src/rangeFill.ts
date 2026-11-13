// The standard view's thin sliders (seek and volume, task 6-8): the track is
// drawn by CSS, lit amber up to the knob. The page tells it how far through
// --fill, whenever the value changes (by hand or from the app).

export function rangeFill(value: number, min: number, max: number): string {
  if (!(max > min) || !Number.isFinite(value)) return "0%";
  const t = Math.min(1, Math.max(0, (value - min) / (max - min)));
  return `${Math.round(t * 10000) / 100}%`;
}

export function paintRange(input: HTMLInputElement) {
  input.style.setProperty("--fill", rangeFill(Number(input.value), Number(input.min || 0), Number(input.max || 100)));
}
