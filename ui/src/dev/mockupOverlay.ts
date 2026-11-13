// The mockup overlay (task 6-8, development only; loaded by main.ts when the
// dev server's page has ?mockup=<file name>). Lays a concept image over the
// page at its own pixel size so the standard view can be lined up with it:
//   重ね   the image, half see-through
//   差分   the image in "difference" blending: where page and image agree it
//          goes dark, so edges that are off show as bright double lines
//   左右   the image left of a line, the page right of it
// 倍率 says which screen scaling the image is taken to be drawn for: at 150%
// it is shown at 2/3 of its size (and the browser window should be 2/3 too).
// The page stays usable under it (the image takes no clicks). Alt+M hides the
// image while held down, for a quick flick between the two. The panel shows
// the cursor in page and image coordinates for measuring.

import { mockupClip, parseMockupParams, writeMockupParams, type MockupMode, type MockupParams } from "./mockupParams";

const MODE_LABELS: [MockupMode, string][] = [
  ["overlay", "重ね"],
  ["diff", "差分"],
  ["wipe", "左右"],
];

export async function installMockupOverlay() {
  let p: MockupParams = parseMockupParams(location.search);
  if (!p.name) return;

  const img = document.createElement("img");
  img.alt = "";
  img.draggable = false;
  Object.assign(img.style, {
    position: "fixed",
    zIndex: "2147483646",
    pointerEvents: "none",
    maxWidth: "none",
    userSelect: "none",
  } satisfies Partial<CSSStyleDeclaration>);
  // One image pixel to 1/zoom CSS pixels, whatever the screen's own scaling.
  const fitSize = () => {
    if (!img.naturalWidth) return;
    const w = Math.round(img.naturalWidth / p.zoom);
    const h = Math.round(img.naturalHeight / p.zoom);
    img.style.width = `${w}px`;
    img.style.height = `${h}px`;
    const page = `${w}×${Math.round(h + p.dy)}`;
    size.textContent = `${img.naturalWidth}×${img.naturalHeight}`;
    size.title = `この見本に合う窓の中の大きさ：${page}（今は ${innerWidth}×${innerHeight}）`;
    size.style.color = Math.abs(w - innerWidth) <= 1 && Math.abs(h + p.dy - innerHeight) <= 1 ? "" : "#fc6";
  };
  img.addEventListener("load", fitSize);
  addEventListener("resize", () => fitSize());

  const panel = document.createElement("div");
  Object.assign(panel.style, {
    position: "fixed",
    zIndex: "2147483647",
    left: "50%",
    bottom: "8px",
    transform: "translateX(-50%)",
    display: "flex",
    gap: "10px",
    alignItems: "center",
    padding: "6px 10px",
    font: "12px/1.4 system-ui, sans-serif",
    color: "#eee",
    background: "rgba(10, 30, 60, 0.92)",
    border: "1px solid #4af",
    borderRadius: "6px",
    whiteSpace: "nowrap",
  } satisfies Partial<CSSStyleDeclaration>);

  const select = document.createElement("select");
  const modes = document.createElement("span");
  const radios = MODE_LABELS.map(([mode, label]) => {
    const l = document.createElement("label");
    const r = document.createElement("input");
    r.type = "radio";
    r.name = "mockup-mode";
    r.value = mode;
    l.append(r, label);
    modes.append(l);
    return r;
  });
  const range = (min: number, max: number, step: number) => {
    const r = document.createElement("input");
    r.type = "range";
    r.min = String(min);
    r.max = String(max);
    r.step = String(step);
    r.style.width = "90px";
    return r;
  };
  const opacity = range(0, 1, 0.05);
  const wipe = range(0, 1, 0.005);
  const zoom = document.createElement("select");
  zoom.title = "見本を、どの拡大率の画面で描いたものとして見るか";
  for (const z of [1, 1.25, 1.5, 2]) zoom.add(new Option(`${z * 100}%`, String(z)));
  const offset = (title: string) => {
    const n = document.createElement("input");
    n.type = "number";
    n.step = "1";
    n.title = title;
    n.style.width = "48px";
    return n;
  };
  const dx = offset("見本を右へずらす（px）");
  const dy = offset("見本を下へずらす（px）。既定の −33 は見本のタイトルバーの高さ");
  const size = document.createElement("span");
  size.style.cursor = "help";
  const cursor = document.createElement("span");
  cursor.style.minWidth = "200px";
  cursor.style.fontVariantNumeric = "tabular-nums";
  const close = document.createElement("button");
  close.type = "button";
  close.textContent = "×";
  close.title = "重ね比べをやめる";
  const labelled = (text: string, el: HTMLElement) => {
    const l = document.createElement("label");
    l.append(text, " ", el);
    return l;
  };
  const opacityLabel = labelled("濃さ", opacity);
  const wipeLabel = labelled("境目", wipe);
  panel.append(
    labelled("見本", select),
    size,
    modes,
    opacityLabel,
    wipeLabel,
    labelled("倍率", zoom),
    labelled("x", dx),
    labelled("y", dy),
    cursor,
    close,
  );
  // Clicks on the panel stay in the panel (the page's own handlers ignore them).
  for (const type of ["click", "keydown", "contextmenu"]) panel.addEventListener(type, (e) => e.stopPropagation());

  function apply() {
    img.src = `/__mockups/${encodeURIComponent(p.name!)}`;
    img.style.left = `${p.dx}px`;
    img.style.top = `${p.dy}px`;
    img.style.opacity = p.mode === "overlay" ? String(p.opacity) : "1";
    img.style.mixBlendMode = p.mode === "diff" ? "difference" : "normal";
    img.style.clipPath = mockupClip(p.mode, p.wipe);
    for (const r of radios) r.checked = r.value === p.mode;
    opacity.value = String(p.opacity);
    wipe.value = String(p.wipe);
    dx.value = String(p.dx);
    dy.value = String(p.dy);
    zoom.value = String(p.zoom);
    fitSize();
    opacityLabel.style.display = p.mode === "overlay" ? "" : "none";
    wipeLabel.style.display = p.mode === "wipe" ? "" : "none";
    if (select.value !== p.name) select.value = p.name!;
    history.replaceState(history.state, "", writeMockupParams(location.search, p) + location.hash);
  }
  const update = (change: Partial<MockupParams>) => {
    p = { ...p, ...change };
    apply();
  };

  for (const r of radios) r.addEventListener("change", () => update({ mode: r.value as MockupMode }));
  opacity.addEventListener("input", () => update({ opacity: Number(opacity.value) }));
  wipe.addEventListener("input", () => update({ wipe: Number(wipe.value) }));
  dx.addEventListener("change", () => update({ dx: Math.round(Number(dx.value) || 0) }));
  dy.addEventListener("change", () => update({ dy: Math.round(Number(dy.value) || 0) }));
  select.addEventListener("change", () => update({ name: select.value }));
  zoom.addEventListener("change", () => {
    // Keep the title bar off the page at the new size.
    const z = Number(zoom.value);
    update({ zoom: z, dx: Math.round((p.dx * p.zoom) / z), dy: Math.round((p.dy * p.zoom) / z) });
  });
  close.addEventListener("click", () => {
    img.remove();
    panel.remove();
    const q = new URLSearchParams(location.search);
    for (const k of ["mockup", "mode", "opacity", "dx", "dy", "wipe", "zoom"]) q.delete(k);
    const s = q.toString();
    history.replaceState(history.state, "", (s ? `?${s}` : location.pathname) + location.hash);
  });

  addEventListener("mousemove", (e) => {
    const mx = Math.round((e.clientX - p.dx) * p.zoom);
    const my = Math.round((e.clientY - p.dy) * p.zoom);
    cursor.textContent = `画面 ${e.clientX}, ${e.clientY} ／ 見本 ${mx}, ${my}`;
  });
  addEventListener("keydown", (e) => {
    if (e.altKey && e.code === "KeyM") img.style.visibility = "hidden";
  });
  addEventListener("keyup", (e) => {
    if (e.code === "KeyM" || e.key === "Alt") img.style.visibility = "";
  });

  let names: string[] = [];
  try {
    names = await (await fetch("/__mockups/")).json();
  } catch {
    // the list is a convenience; the named image still loads
  }
  if (!names.includes(p.name)) names.unshift(p.name);
  for (const n of names) select.add(new Option(n, n));

  document.body.append(img, panel);
  apply();
}
