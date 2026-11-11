import "./style.css";
import { createBackend, type Backend } from "./backend";
import { countsText } from "./libraryFormat";
import { LibraryView, loadTheme, type PlayRequest } from "./libraryView";
import { lengthText } from "./libraryFormat";
import {
  bitPerfectText,
  selfCheckText,
  describeFormat,
  dopNotice,
  inputText,
  processingText,
  dropoutText,
  errorText,
  formatTime,
  integrityFailuresText,
  integrityText,
  meterIntegratedText,
  meterLoudnessText,
  meterNote,
  meterPeakText,
  meterTrackPeakText,
  outputModeText,
  outputTags,
  priorityText,
  queueText,
  rateChangeText,
  repackText,
  stateLabel,
  transitionText,
  volumeText,
} from "./messages";
import type { PlaybackStatus } from "./status";

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

const els = {
  open: $<HTMLButtonElement>("open"),
  play: $<HTMLButtonElement>("play"),
  stop: $<HTMLButtonElement>("stop"),
  prev: $<HTMLButtonElement>("prev"),
  next: $<HTMLButtonElement>("next"),
  seek: $<HTMLInputElement>("seek"),
  fileName: $("file-name"),
  nowArtist: $("now-artist"),
  nowCover: $<HTMLImageElement>("now-cover"),
  nowCoverEmpty: $("now-cover-empty"),
  queue: $("queue"),
  levelL: $("level-l"),
  levelR: $("level-r"),
  output: $<HTMLButtonElement>("output"),
  outputTags: $("output-tags"),
  outputDialog: $<HTMLDialogElement>("output-dialog"),
  outputClose: $<HTMLButtonElement>("output-close"),
  queueCount: $("queue-count"),
  queueList: $("queue-list"),
  counts: $("counts"),
  statusDot: $("status-dot"),
  transitions: $("transitions"),
  dropouts: $("dropouts"),
  priority: $("priority"),
  processing: $("processing"),
  bitPerfect: $("bit-perfect"),
  selfCheck: $("self-check"),
  dopNote: $("dop-note"),
  state: $("state"),
  position: $("position"),
  duration: $("duration"),
  sourceFormat: $("source-format"),
  outputName: $("output-name"),
  outputMode: $("output-mode"),
  integrity: $("integrity"),
  integrityFailures: $("integrity-failures"),
  volume: $("volume"),
  outputFormat: $("output-format"),
  repack: $("repack"),
  error: $("error"),
  backendKind: $("backend-kind"),
  meterPeak: $("meter-peak"),
  meterTrackPeak: $("meter-track-peak"),
  meterLoudness: $("meter-loudness"),
  meterIntegrated: $("meter-integrated"),
  meterNote: $("meter-note"),
};

const SEEK_STEPS = 1000;
let dragging = false;
let current: PlaybackStatus | null = null;

// What was asked to play from the library (the status knows only names and indices).
let queue: PlayRequest | null = null;
let library: LibraryView | null = null;
let backend: Backend;

function levelWidth(db: number | "-inf" | undefined): number {
  if (db === undefined || db === "-inf") return 0;
  return Math.max(0, Math.min(100, ((db + 60) / 60) * 100)); // -60..0 dB
}

function renderTopBar(s: PlaybackStatus) {
  const id = queue && s.queueIndex < queue.ids.length ? queue.ids[s.queueIndex] : null;
  const loaded = s.state !== "empty";
  els.fileName.textContent = loaded ? s.fileName : "曲を選んでください";
  els.nowArtist.textContent = queue && loaded ? queue.artists[s.queueIndex] ?? "" : "";
  const cover = id !== null ? backend.library.coverUrl(id) : "";
  if (els.nowCover.dataset.src !== cover) {
    els.nowCover.dataset.src = cover;
    els.nowCover.hidden = !cover;
    els.nowCoverEmpty.hidden = !!cover;
    if (cover) els.nowCover.src = cover;
  }
  library?.setPlaying(s.state === "playing" || s.state === "stopped" ? id : null);

  // Levels: the true peak of about the last 0.1 s of what goes to the output (task 4-4).
  const r = s.meter.reading;
  const measuring = s.meter.state === "measuring" && r !== null;
  const peaks = measuring ? r.truePeakRecent : [];
  const left = peaks[0];
  const right = peaks.length > 1 ? peaks[1] : peaks[0];
  els.levelL.style.width = `${levelWidth(left)}%`;
  els.levelR.style.width = `${levelWidth(right)}%`;
  els.levelL.classList.toggle("over", left !== undefined && left !== "-inf" && left > 0);
  els.levelR.classList.toggle("over", right !== undefined && right !== "-inf" && right > 0);

  els.outputTags.replaceChildren(
    ...outputTags(s).map((t) => {
      const span = document.createElement("span");
      span.className = `tag ${t.kind === "plain" ? "" : t.kind}`;
      span.textContent = t.text;
      return span;
    }),
  );

  const hasQueue = !!queue && loaded;
  els.prev.disabled = !hasQueue || s.queueIndex <= 0;
  els.next.disabled = !hasQueue || s.queueIndex + 1 >= (queue?.ids.length ?? 0);
}

function renderQueue(s: PlaybackStatus) {
  const loaded = s.state !== "empty";
  if (!queue) {
    els.queueCount.textContent = loaded ? `${s.queueLength} 曲（ファイルを開いて再生）` : "—";
    els.queueList.replaceChildren();
    return;
  }
  els.queueCount.textContent = `${queue.ids.length} 曲`;
  const items = els.queueList.children;
  if (items.length !== queue.ids.length) {
    els.queueList.replaceChildren(
      ...queue.names.map((name, i) => {
        const li = document.createElement("li");
        const no = document.createElement("span");
        no.className = "q-no";
        no.textContent = String(i + 1);
        const n = document.createElement("span");
        n.className = "q-name";
        n.textContent = name;
        n.title = `${name}${queue!.artists[i] ? ` — ${queue!.artists[i]}` : ""}`;
        const t = document.createElement("span");
        t.className = "q-time";
        t.textContent = lengthText(queue!.seconds[i]);
        li.append(no, n, t);
        li.addEventListener("dblclick", () => void playRequest({ ...queue!, start: i }));
        return li;
      }),
    );
  }
  for (let i = 0; i < items.length; i++) {
    const li = items[i] as HTMLElement;
    const now = loaded && i === s.queueIndex;
    if (li.classList.contains("current") !== now) {
      li.classList.toggle("current", now);
      (li.firstElementChild as HTMLElement).textContent = now && s.state === "playing" ? "▶" : String(i + 1);
      if (now) li.scrollIntoView({ block: "nearest" });
    }
  }
}

function render(s: PlaybackStatus) {
  current = s;
  const rate = s.sourceFormat?.sampleRate ?? 0;
  const loaded = s.state !== "empty";

  els.state.textContent = stateLabel(s.state);
  els.play.disabled = !loaded || s.state === "playing";
  els.stop.disabled = s.state !== "playing";
  els.seek.disabled = !loaded || s.totalFrames === 0;

  els.duration.textContent = formatTime(s.totalFrames, rate);
  if (!dragging) {
    els.position.textContent = formatTime(s.positionFrames, rate);
    els.seek.value = String(s.totalFrames > 0 ? Math.round((s.positionFrames / s.totalFrames) * SEEK_STEPS) : 0);
  }
  renderTopBar(s);
  renderQueue(s);

  // The output check (simple; the full screen with the path diagram is 6-4).
  els.sourceFormat.textContent = inputText(s);
  els.integrity.textContent = loaded ? integrityText(s) : "—";
  const failures = integrityFailuresText(s);
  els.integrityFailures.hidden = failures === "";
  els.integrityFailures.textContent = failures;
  els.processing.textContent = processingText(s);
  els.bitPerfect.textContent = bitPerfectText(s);
  els.selfCheck.textContent = selfCheckText(s);
  const note = dopNotice(s);
  els.dopNote.hidden = note === "";
  els.dopNote.textContent = note;
  els.outputName.textContent = s.outputName || "—";
  els.outputMode.textContent = s.path ? outputModeText(s.path) : "—";
  els.volume.textContent = (s.path && volumeText(s.path)) || "—";
  const rateChange = rateChangeText(s);
  els.outputFormat.textContent = s.outputFormat
    ? describeFormat(s.outputFormat) + (rateChange ? `（${rateChange}）` : "")
    : "—";
  els.repack.textContent = s.outputFormat ? repackText(s) : "—";
  els.queue.textContent = queue ? "" : queueText(s);
  const started = s.outputFormat !== null;
  els.dropouts.textContent = started ? dropoutText(s.dropouts) : "—";
  els.priority.textContent = started ? priorityText(s.dropouts.realtimePriority) : "—";

  const recent = s.transitions.slice(-3);
  els.transitions.replaceChildren(
    ...(recent.length > 0 ? recent : [null]).map((t) => {
      const li = document.createElement("li");
      li.textContent = t ? transitionText(t) : "—";
      return li;
    }),
  );

  els.meterPeak.textContent = meterPeakText(s);
  els.meterTrackPeak.textContent = meterTrackPeakText(s);
  els.meterLoudness.textContent = meterLoudnessText(s);
  els.meterIntegrated.textContent = meterIntegratedText(s);
  els.meterNote.textContent = meterNote(s);

  const err = errorText(s);
  if (err) {
    els.error.hidden = false;
    els.error.textContent = err;
  } else if (els.error.dataset.from === "status") {
    els.error.hidden = true;
  }
  els.error.dataset.from = err ? "status" : els.error.dataset.from ?? "";
}

async function playRequest(req: PlayRequest) {
  queue = req;
  els.queueList.replaceChildren(); // drawn again for the new queue
  const r = await backend.library.play(req.ids, req.start);
  if (!r.ok && r.message) {
    els.error.hidden = false;
    els.error.dataset.from = "action";
    els.error.textContent = r.message;
  }
  render(await backend.getStatus());
}

function seekTarget(): number {
  const total = current?.totalFrames ?? 0;
  return Math.round((Number(els.seek.value) / SEEK_STEPS) * total);
}

async function main() {
  loadTheme();
  backend = await createBackend();
  els.backendKind.textContent = backend.kind === "mock" ? "模擬エンジン（ブラウザー単体で表示中）" : "";

  library = new LibraryView(backend, (req) => void playRequest(req));
  backend.onStatus(render);
  backend.library.onChanged((c) => {
    els.counts.textContent = countsText(c);
    els.statusDot.classList.toggle("busy", c.scanning || c.toMeasure > 0);
  });
  render(await backend.getStatus());
  const counts = await backend.library.counts();
  els.counts.textContent = countsText(counts);
  els.statusDot.classList.toggle("busy", counts.scanning || counts.toMeasure > 0);
  await library.start();

  els.open.addEventListener("click", async () => {
    const r = await backend.openFile();
    if (!r.ok && !r.cancelled && r.message) {
      els.error.hidden = false;
      els.error.dataset.from = "action";
      els.error.textContent = r.message;
    }
    if (r.ok) queue = null; // a file queue, not the library's
    render(await backend.getStatus());
  });
  els.play.addEventListener("click", () => void backend.play());
  els.stop.addEventListener("click", () => void backend.stop());
  els.prev.addEventListener("click", () => queue && current && void playRequest({ ...queue, start: current.queueIndex - 1 }));
  els.next.addEventListener("click", () => queue && current && void playRequest({ ...queue, start: current.queueIndex + 1 }));
  els.output.addEventListener("click", () => els.outputDialog.showModal());
  els.outputClose.addEventListener("click", () => els.outputDialog.close());

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
