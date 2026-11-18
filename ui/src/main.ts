import "./style.css";
import { createBackend, type Backend, type QueueInfo } from "./backend";
import { afterInsert, afterMove, afterRemove, dbToSlider, sliderToDb, volumeLabel, type Db } from "./queueOps";
import { countsText } from "./libraryFormat";
import { LibraryView, loadTheme, type PlayRequest } from "./libraryView";
import { applyStandardZoom, applyUiView, currentUiView, loadUiView, onUiViewChange, otherViews, saveUiView, VIEW_LABELS, type UiView } from "./uiView";
import { AmpView } from "./ampView";
import {
  loadPlayModes,
  nextRepeat,
  nextTrackIndex,
  originalAfterInsert,
  prevTrackIndex,
  repeatLabel,
  restoreOrder,
  savePlayModes,
  shuffled,
  shuffleLabel,
  shuffleStart,
  type PlayModes,
} from "./playModes";
import { SettingsView } from "./settingsView";
import { SettingsTabsView } from "./settingsTabsView";
import { VuMeter } from "./vuMeter";
import { paintRange } from "./rangeFill";
import { OutputView } from "./outputView";
import { AbxView } from "./abxView";
import { articleScreen } from "./articleCommand";
import { playIcon, repeatIcon, setIcon, volumeIcon } from "./icons";
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
  shuffle: $<HTMLButtonElement>("shuffle"),
  repeat: $<HTMLButtonElement>("repeat"),
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
  outputNotice: $("output-notice"),
  outputMode: $("output-mode"),
  integrity: $("integrity"),
  integrityFailures: $("integrity-failures"),
  volume: $("volume"),
  outputFormat: $("output-format"),
  repack: $("repack"),
  error: $("error"),
  backendKind: $("backend-kind"),
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
let ampView: AmpView | null = null; // task 9-9

const queueIds = () => queue.items.map((i) => i.id);

// Shuffle and repeat (task 9-9). While shuffle is on, the order the queue had
// before it was mixed, to put back what is left when it goes off.
let modes: PlayModes = { shuffle: false, repeat: "off" };

// The transport's symbols, drawn before anything else runs (icons.ts); play
// and repeat then follow the state.
setIcon(els.shuffle, "shuffle");
setIcon(els.prev, "prev");
setIcon(els.play, playIcon(false));
setIcon(els.stop, "stop");
setIcon(els.next, "next");
setIcon(els.repeat, repeatIcon(modes.repeat));
setIcon(els.mute, volumeIcon(false));
let shuffleOriginal: number[] | null = null;

function storageOrNull(): Storage | null {
  try {
    return localStorage;
  } catch {
    return null;
  }
}

function renderModes() {
  els.shuffle.setAttribute("aria-pressed", String(modes.shuffle));
  els.shuffle.title = `${shuffleLabel(modes.shuffle)}（この先の曲の順番を混ぜる）`;
  els.repeat.setAttribute("aria-pressed", String(modes.repeat !== "off"));
  els.repeat.dataset.repeat = modes.repeat;
  setIcon(els.repeat, repeatIcon(modes.repeat)); // repeat one carries a 1
  els.repeat.title = `${repeatLabel(modes.repeat)}（押すごとに オフ → 全曲 → 1 曲）`;
  if (current) render(current);
}

// Mixes the tracks after the current one, or puts them back in their order.
// A queue of opened files is not reordered (as the other queue edits).
async function applyShuffle(on: boolean) {
  if (!current || current.state === "empty") return;
  queue = await backend.getQueue();
  if (queue.items.length === 0 || queue.items.some((i) => i.id === 0)) return;
  const ids = queueIds();
  const cur = current.queueIndex;
  const rest = ids.slice(cur + 1);
  if (on) {
    shuffleOriginal = ids;
    await backend.library.replaceUpcoming(shuffled(rest));
  } else if (shuffleOriginal) {
    await backend.library.replaceUpcoming(restoreOrder(rest, shuffleOriginal));
    shuffleOriginal = null;
  }
}

function setUpPlayModes() {
  modes = loadPlayModes(storageOrNull());
  void backend.setRepeat(modes.repeat);
  renderModes();
  els.shuffle.addEventListener("click", () => {
    modes = { ...modes, shuffle: !modes.shuffle };
    savePlayModes(storageOrNull(), modes);
    renderModes();
    void applyShuffle(modes.shuffle);
  });
  els.repeat.addEventListener("click", () => {
    modes = { ...modes, repeat: nextRepeat(modes.repeat) };
    savePlayModes(storageOrNull(), modes);
    renderModes();
    void backend.setRepeat(modes.repeat);
  });
}

// The standard view's VU meters (task 6-8).
const vuMeters = [new VuMeter("L"), new VuMeter("R")];
let shownTagsKey = "";
document.getElementById("vu-meters")!.append(...vuMeters.map((m) => m.el));

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
  // VU: the needles keep moving in 原音モード; only the light is lowered (CLAUDE.md chapter 8).
  const vu = measuring ? r.vu : [];
  vuMeters[0].set(vu[0]);
  vuMeters[1].set(vu.length > 1 ? vu[1] : vu[0]);
  document.getElementById("vu-meters")!.classList.toggle("lamp-off", s.mode === "original");
  els.levelR.classList.toggle("over", right !== undefined && right !== "-inf" && right > 0);

  // Redrawn only when they change: the status comes 10 times a second, and a
  // tag replaced between pressing and releasing the mouse loses the click.
  const tags = outputTags(s);
  const tagsKey = JSON.stringify(tags);
  if (tagsKey !== shownTagsKey) {
    shownTagsKey = tagsKey;
    els.outputTags.replaceChildren(
      ...tags.map((t) => {
        const span = document.createElement("span");
        span.className = `tag ${t.kind === "plain" ? "" : t.kind}`;
        span.textContent = t.text;
        return span;
      }),
    );
  }

  els.prev.disabled = !loaded;
  els.next.disabled = !loaded || nextTrackIndex(s.queueIndex, s.queueLength, modes.repeat) === null;
  const playing = s.state === "playing";
  setIcon(els.play, playIcon(playing));
  els.play.title = playing ? "一時停止（スペース）" : "再生（スペース）";
  els.play.setAttribute("aria-label", playing ? "一時停止" : "再生");

  // The volume as the app has it (unless the slider is being moved).
  if (!volumeDragging) els.volSlider.value = String(Math.round(dbToSlider(s.volumeDb) * 1000));
  paintRange(els.volSlider);
  els.volLabel.textContent = volumeLabel(s.volumeDb);
  els.volLabel.classList.toggle("changed", s.volumeDb !== 0);
  setIcon(els.mute, volumeIcon(s.volumeDb === "-inf"));
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
  ampView?.update(s);
  const rate = s.sourceFormat?.sampleRate ?? 0;
  const loaded = s.state !== "empty";

  // Stopped part-way is a pause (the play button goes on from there).
  els.state.textContent = s.state === "stopped" && s.positionFrames > 0 ? "一時停止中" : stateLabel(s.state);
  // One button plays and pauses (task 6-2): usable whenever something is loaded.
  els.play.disabled = !loaded;
  // Stop (back to the start of the track): also from a pause, not when already there.
  els.stop.disabled = !loaded || (s.state !== "playing" && s.positionFrames === 0);
  els.seek.disabled = !loaded || s.totalFrames === 0;

  els.duration.textContent = formatTime(s.totalFrames, rate);
  if (!dragging) {
    els.position.textContent = formatTime(s.positionFrames, rate);
    els.seek.value = String(s.totalFrames > 0 ? Math.round((s.positionFrames / s.totalFrames) * SEEK_STEPS) : 0);
  }
  paintRange(els.seek);
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
  els.outputNotice.hidden = !s.outputNotice;
  els.outputNotice.textContent = s.outputNotice ? `⚠ ${s.outputNotice}` : "";
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
  // Shuffle on: the chosen track first, the rest mixed.
  shuffleOriginal = modes.shuffle ? [...req.ids] : null;
  const r = modes.shuffle
    ? await backend.library.play(shuffleStart(req.ids, req.start), 0)
    : await backend.library.play(req.ids, req.start);
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
  if (modes.shuffle && shuffleOriginal)
    shuffleOriginal = originalAfterInsert(shuffleOriginal, queueIds(), current!.queueIndex, ids, where);
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

// The views (tasks 6-7, 9-9): the standard view first, then the view last
// used. The side bar offers the other two.
function setUpViews() {
  const toggles = [
    document.getElementById("ui-view-toggle") as HTMLButtonElement,
    document.getElementById("ui-view-toggle-2") as HTMLButtonElement,
  ];
  const radios = document.querySelectorAll<HTMLInputElement>('input[name="ui-view"]');
  const themeSet = document.getElementById("theme-set") as HTMLFieldSetElement;
  const show = (v: UiView) => {
    otherViews(v).forEach((o, i) => {
      toggles[i].textContent = `${VIEW_LABELS[o]}にする`;
      toggles[i].dataset.view = o;
    });
    for (const r of radios) r.checked = r.value === v;
    themeSet.disabled = v !== "library";
  };
  let storage: Storage | null = null;
  try {
    storage = localStorage;
  } catch {
    storage = null; // no storage: the view lasts until the window closes
  }
  const choose = (v: UiView) => {
    applyUiView(v);
    if (storage) saveUiView(storage, v);
  };
  // The standard view's scale first: what redraws on a change reads sizes.
  onUiViewChange(() => applyStandardZoom());
  addEventListener("resize", () => applyStandardZoom());
  onUiViewChange(show);
  for (const t of toggles) t.addEventListener("click", () => choose((t.dataset.view as UiView) ?? currentUiView()));
  for (const r of radios) r.addEventListener("change", () => r.checked && choose(r.value as UiView));
  applyUiView(storage ? loadUiView(storage) : "standard");
}

async function main() {
  loadTheme();
  setUpViews();
  // The concept image over the page (task 6-8). Dev server only: the app's
  // build leaves this out.
  if (import.meta.env.DEV && new URLSearchParams(location.search).has("mockup")) {
    void import("./dev/mockupOverlay").then((m) => m.installMockupOverlay());
  }
  backend = await createBackend();
  els.backendKind.textContent = backend.kind === "mock" ? "模擬エンジン（ブラウザー単体で表示中）" : "";

  library = new LibraryView(
    backend,
    (req) => void playRequest(req),
    (ids, where) => void enqueue(ids, where),
  );
  const settings = new SettingsView(backend.settings);
  const settingsTabs = new SettingsTabsView(); // the tabs down the left of the settings screen
  document
    .querySelector<HTMLButtonElement>('.nav[data-view="settings"]')
    ?.addEventListener("click", () => void settings.refresh());
  void settings.refresh(); // the settings as saved, before anything changes them
  const outputView = new OutputView((mode) => settings.enableIspSafe(mode));
  ampView = new AmpView({
    listening: () => settings.current,
    tubeAuto: () => settings.tubeAutoEnabled,
    change: (part, delayMs) => settings.changeListening(part, delayMs),
    onListening: (l) => settings.onListening(l),
    volumeSlider: els.volSlider,
    openOutputCheck: () => outputView.open(),
  });
  const abxView = new AbxView(backend.abx, () => backend.settings.get());
  const openAbx = async () => {
    els.outputDialog.close();
    abxView.render(await backend.getStatus()); // the track and position to start from
    abxView.open();
  };
  document.getElementById("oc-abx")?.addEventListener("click", () => void openAbx());
  document.getElementById("settings-abx")?.addEventListener("click", () => void openAbx());

  // The article reader (task 7-1): a window of its own. A link in an article
  // may ask for one of three screens here; it changes no setting and plays nothing.
  document.getElementById("open-articles")?.addEventListener("click", async () => {
    const r = await backend.openArticle();
    if (!r.ok && r.message) {
      els.error.hidden = false;
      els.error.dataset.from = "action";
      els.error.textContent = r.message;
    }
  });
  backend.onArticleCommand((payload) => {
    const to = articleScreen(payload);
    const dialog = (id: string) => document.getElementById(id) as HTMLDialogElement | null;
    if (!to || dialog("abx-dialog")?.open) return; // an ABX under way is left alone
    if (to.screen === "abx") return void openAbx();
    dialog("eq-dialog")?.close();
    if (to.screen === "output-check") return outputView.open();
    els.outputDialog.close();
    document.querySelector<HTMLButtonElement>('.nav[data-view="settings"]')?.click();
    if (to.section) settingsTabs.showSection(to.section); // its tab, and the heading in view
  });
  backend.onStatus((s) => {
    render(s);
    settings.onStatus(s);
    outputView.render(s);
    abxView.render(s);
  });
  backend.library.onChanged((c) => {
    els.counts.textContent = countsText(c);
    els.statusDot.classList.toggle("busy", c.scanning || c.toMeasure > 0);
  });
  render(await backend.getStatus());
  const counts = await backend.library.counts();
  els.counts.textContent = countsText(counts);
  els.statusDot.classList.toggle("busy", counts.scanning || counts.toMeasure > 0);
  await library.start();

  setUpPlayModes();
  els.open.addEventListener("click", async () => {
    shuffleOriginal = null; // opened files are not reordered
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
    const before = prevTrackIndex(current.queueIndex, current.queueLength, modes.repeat);
    if (rate > 0 && current.positionFrames > rate * 3) void backend.seek(0);
    else if (before !== null) void backend.playAt(before);
    else void backend.seek(0);
  });
  els.next.addEventListener("click", () => {
    if (!current) return;
    const after = nextTrackIndex(current.queueIndex, current.queueLength, modes.repeat);
    if (after !== null) void backend.playAt(after);
  });
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
  els.output.addEventListener("click", () => outputView.open());
  els.outputClose.addEventListener("click", () => els.outputDialog.close());

  for (const r of [els.seek, els.volSlider]) {
    r.addEventListener("input", () => paintRange(r));
    paintRange(r);
  }
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
