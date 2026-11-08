import "./style.css";
import { createBackend, type Backend } from "./backend";
import { describeFormat, errorText, formatTime, repackText, stateLabel } from "./messages";
import type { PlaybackStatus } from "./status";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const els = {
  open: $<HTMLButtonElement>("open"),
  play: $<HTMLButtonElement>("play"),
  stop: $<HTMLButtonElement>("stop"),
  seek: $<HTMLInputElement>("seek"),
  fileName: $("file-name"),
  state: $("state"),
  position: $("position"),
  duration: $("duration"),
  sourceFormat: $("source-format"),
  outputName: $("output-name"),
  outputFormat: $("output-format"),
  repack: $("repack"),
  error: $("error"),
  backendKind: $("backend-kind"),
};

const SEEK_STEPS = 1000;
let dragging = false;
let current: PlaybackStatus | null = null;

function render(s: PlaybackStatus) {
  current = s;
  const rate = s.sourceFormat?.sampleRate ?? 0;
  const loaded = s.state !== "empty";

  els.fileName.textContent = loaded ? s.fileName : "ファイルを開いてください";
  els.state.textContent = stateLabel(s.state);
  els.play.disabled = !loaded || s.state === "playing";
  els.stop.disabled = s.state !== "playing";
  els.seek.disabled = !loaded || s.totalFrames === 0;

  els.duration.textContent = formatTime(s.totalFrames, rate);
  if (!dragging) {
    els.position.textContent = formatTime(s.positionFrames, rate);
    els.seek.value = String(s.totalFrames > 0 ? Math.round((s.positionFrames / s.totalFrames) * SEEK_STEPS) : 0);
  }

  els.sourceFormat.textContent = s.sourceFormat ? describeFormat(s.sourceFormat) : "—";
  els.outputName.textContent = s.outputName ? `${s.outputName}（排他 WASAPI）` : "—";
  els.outputFormat.textContent = s.outputFormat ? describeFormat(s.outputFormat) : "—";
  els.repack.textContent = s.outputFormat ? repackText(s) : "—";

  const err = errorText(s);
  els.error.hidden = err === "";
  els.error.textContent = err;
}

function seekTarget(): number {
  const total = current?.totalFrames ?? 0;
  return Math.round((Number(els.seek.value) / SEEK_STEPS) * total);
}

async function main() {
  const backend: Backend = await createBackend();
  els.backendKind.textContent = backend.kind === "mock" ? "模擬エンジン（ブラウザー単体で表示中）" : "";

  backend.onStatus(render);
  render(await backend.getStatus());

  els.open.addEventListener("click", async () => {
    const r = await backend.openFile();
    if (!r.ok && !r.cancelled && r.message) {
      els.error.hidden = false;
      els.error.textContent = r.message;
    }
    render(await backend.getStatus());
  });
  els.play.addEventListener("click", () => void backend.play());
  els.stop.addEventListener("click", () => void backend.stop());

  els.seek.addEventListener("input", () => {
    dragging = true;
    els.position.textContent = formatTime(seekTarget(), current?.sourceFormat?.sampleRate ?? 0);
  });
  els.seek.addEventListener("change", async () => {
    await backend.seek(seekTarget());
    dragging = false;
    render(await backend.getStatus());
  });

  if (new URLSearchParams(location.search).has("selftest")) {
    const { runSelfTest, expectedViewportFromQuery } = await import("./selftest");
    const result = await runSelfTest(backend, expectedViewportFromQuery(location.search));
    await backend.selfTestResult(result);
    return;
  }

  await backend.uiReady();
}

void main();
