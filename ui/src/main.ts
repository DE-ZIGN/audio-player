import "./style.css";
import { createBackend, type Backend, type QueueInfo } from "./backend";
import { afterInsert, afterMove, afterRemove, dbToSlider, sliderToDb, volumeLabel, type Db } from "./queueOps";
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
  queueClear: $<HTMLButtonElement>("queue-clear"),
  volSlider: $<HTMLInputElement>("vol-slider"),
  volLabel: $("vol-label"),
  mute: $<HTMLButtonElement>("mute"),
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

// The play queue as the app holds it (task 6-2), fetched when its version changes.
let queue: QueueInfo = { version: -1, items: [] };
let queueFetching = false;
let library: LibraryView | null = null;
let backend: Backend;
let volumeDragging = false;
let unmutedDb: Db = 0;

const queueIds = () => queue.items.map((i) => i.id);

function levelWidth(db: number | "-inf" | undefined): number {
  if (db === undefined || db === "-inf") return 0;
  return Math.max(0, Math.min(100, ((db + 60) / 60) * 100)); // -60..0 dB
}

function renderTopBar(s: PlaybackStatus) {
  const item = queue.items[s.queueIndex];
  const id = item && item.id > 0 ? item.id : null;
  const loaded = s.state !== "empty";
  els.fileName.textContent = loaded ? s.fileName : "曲を選んでください";
  els.nowArtist.textContent = loaded ? item?.artist ?? "" : "";
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

  els.prev.disabled = !loaded;
  els.next.disabled = !loaded || s.queueIndex + 1 >= s.queueLength;
  const playing = s.state === "playing";
  els.play.textContent = playing ? "❚❚" : "▶";
  els.play.title = playing ? "一時停止（スペース）" : "再生（スペース）";
  els.play.setAttribute("aria-label", playing ? "一時停止" : "再生");

  // The volume as the app has it (unless the slider is being moved).
  if (!volumeDragging) els.volSlider.value = String(Math.round(dbToSlider(s.volumeDb) * 1000));
  els.volLabel.textContent = volumeLabel(s.volumeDb);
  els.volLabel.classList.toggle("changed", s.volumeDb !== 0);
  els.mute.textContent = s.volumeDb === "-inf" ? "🔇" : "🔊";
}

let drawnQueue = { version: -2, index: -1, state: "" };
let dragFrom = -1;

function renderQueue(s: PlaybackStatus) {
  const loaded = s.state !== "empty";
  if (s.queueVersion !== queue.version && !queueFetching) {
    queueFetching = true;
    void backend.getQueue().then((q) => {
      queue = q;
      queueFetching = false;
      if (current) render(current);
    });
  }
  const fromFiles = queue.items.some((i) => i.id === 0);
  els.queueCount.textContent = !loaded
    ? "—"
    : `${queue.items.length} 曲${fromFiles ? "（ファイルを開いて再生）" : ""}・あと ${Math.max(0, queue.items.length - s.queueIndex - 1)} 曲`;
  els.queueClear.disabled = !loaded || s.queueIndex + 1 >= queue.items.length || fromFiles;
  if (drawnQueue.version === queue.version && drawnQueue.index === s.queueIndex && drawnQueue.state === s.state) return;
  drawnQueue = { version: queue.version, index: s.queueIndex, state: s.state };

  els.queueList.replaceChildren(
    ...queue.items.map((item, i) => {
      const li = document.createElement("li");
      const now = loaded && i === s.queueIndex;
      li.classList.toggle("current", now);
      li.classList.toggle("past", loaded && i < s.queueIndex);
      const no = document.createElement("span");
      no.className = "q-no";
      no.textContent = now && s.state === "playing" ? "▶" : String(i + 1);
      const n = document.createElement("span");
      n.className = "q-name";
      n.textContent = item.name;
      n.title = `${item.name}${item.artist ? ` — ${item.artist}` : ""}`;
      const t = document.createElement("span");
      t.className = "q-time";
      t.textContent = lengthText(item.seconds);
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "q-remove";
      remove.textContent = "×";
      remove.title = "キューから外す";
      const upcoming = loaded && i > s.queueIndex && !fromFiles;
      remove.hidden = !upcoming;
      remove.addEventListener("click", (e) => {
        e.stopPropagation();
        const next = afterRemove(queueIds(), s.queueIndex, i);
        if (next) void backend.library.replaceUpcoming(next);
      });
      li.append(no, n, t, remove);
      li.addEventListener("dblclick", () => void backend.playAt(i));
      if (upcoming) {
        // Drag to reorder what comes next.
        li.draggable = true;
        li.addEventListener("dragstart", (e) => {
          dragFrom = i;
          e.dataTransfer?.setData("text/plain", String(i));
        });
        li.addEventListener("dragover", (e) => {
          if (dragFrom < 0) return;
          e.preventDefault();
          li.classList.add("drop-before");
        });
        li.addEventListener("dragleave", () => li.classList.remove("drop-before"));
        li.addEventListener("drop", (e) => {
          e.preventDefault();
          li.classList.remove("drop-before");
          const next = afterMove(queueIds(), s.queueIndex, dragFrom, i);
          dragFrom = -1;
          if (next) void backend.library.replaceUpcoming(next);
        });
      }
      if (now) requestAnimationFrame(() => li.scrollIntoView({ block: "nearest" }));
      return li;
    }),
  );
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
  els.queue.textContent = queue.items.some((i) => i.id > 0) ? "" : queueText(s);
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
  const r = await backend.library.play(req.ids, req.start);
  if (!r.ok && r.message) {
    els.error.hidden = false;
    els.error.dataset.from = "action";
    els.error.textContent = r.message;
  }
  render(await backend.getStatus());
}

// "Play next" and "add to the end" (task 6-2): the tracks after the current
// one are replaced; the current track plays on.
async function enqueue(ids: number[], where: "next" | "end") {
  if (ids.length === 0) return;
  const loaded = current !== null && current.state !== "empty";
  if (!loaded || queue.items.length === 0) return playRequest({ ids, start: 0 });
  if (queue.items.some((i) => i.id === 0)) {
    els.error.hidden = false;
    els.error.dataset.from = "action";
    els.error.textContent = "ファイルを開いて再生している列には、ライブラリの曲を足せません（ライブラリから再生し直してください）";
    return;
  }
  await backend.library.replaceUpcoming(afterInsert(queueIds(), current!.queueIndex, ids, where));
}

async function togglePlay() {
  if (!current || current.state === "empty") return;
  if (current.state === "playing") await backend.stop(); // a pause: the position stays
  else await backend.play();
}

function setVolume(db: Db) {
  void backend.setVolume(db);
  try {
    localStorage.setItem("volumeDb", String(db));
  } catch {
    // not kept
  }
}

function seekTarget(): number {
  const total = current?.totalFrames ?? 0;
  return Math.round((Number(els.seek.value) / SEEK_STEPS) * total);
}

async function main() {
  loadTheme();
  backend = await createBackend();
  els.backendKind.textContent = backend.kind === "mock" ? "模擬エンジン（ブラウザー単体で表示中）" : "";

  library = new LibraryView(
    backend,
    (req) => void playRequest(req),
    (ids, where) => void enqueue(ids, where),
  );
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
    render(await backend.getStatus());
  });
  els.play.addEventListener("click", () => void togglePlay());
  els.stop.addEventListener("click", async () => {
    await backend.stop();
    await backend.seek(0);
  });
  els.prev.addEventListener("click", () => {
    if (!current) return;
    const rate = current.sourceFormat?.sampleRate ?? 0;
    // Past the first 3 seconds: back to the start of this track; else the previous one.
    if (rate > 0 && current.positionFrames > rate * 3) void backend.seek(0);
    else if (current.queueIndex > 0) void backend.playAt(current.queueIndex - 1);
    else void backend.seek(0);
  });
  els.next.addEventListener("click", () => current && void backend.playAt(current.queueIndex + 1));
  els.queueClear.addEventListener("click", () => void backend.library.replaceUpcoming([]));
  document.addEventListener("keydown", (e) => {
    const t = e.target as HTMLElement;
    if (e.code !== "Space" || ["INPUT", "SELECT", "TEXTAREA", "BUTTON"].includes(t.tagName)) return;
    e.preventDefault();
    void togglePlay();
  });

  // The digital volume: 100% (top) is no processing; the last setting is kept.
  els.volSlider.addEventListener("input", () => {
    volumeDragging = true;
    const db = sliderToDb(Number(els.volSlider.value) / 1000);
    els.volLabel.textContent = volumeLabel(db);
    setVolume(db);
  });
  els.volSlider.addEventListener("change", () => (volumeDragging = false));
  els.mute.addEventListener("click", () => {
    const now = current?.volumeDb ?? 0;
    if (now === "-inf") setVolume(unmutedDb === "-inf" ? 0 : unmutedDb);
    else {
      unmutedDb = now;
      setVolume("-inf");
    }
  });
  try {
    const saved = localStorage.getItem("volumeDb");
    if (saved !== null) setVolume(saved === "-inf" ? "-inf" : Math.min(0, Number(saved) || 0));
  } catch {
    // no storage: 100%
  }
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
