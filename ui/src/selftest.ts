// End-to-end self test, run when the page is opened with ?selftest=1 (the app
// does this for --self-test). Drives the real native functions and waits for
// the status events coming back from the engine. The app then checks the
// bytes that reached its dummy output.

import type { Backend } from "./backend";
import { autoPreampDb, responsePoints } from "./eqResponse";
import type { EqBand } from "./settings";
import type { PlaybackStatus } from "./status";

export interface SelfTestResult {
  ok: boolean;
  log: string[];
}

export interface ExpectedViewport {
  width: number;
  height: number;
}

// Reads ?w=&h= (the size of the app's web view component, in logical pixels).
export function expectedViewportFromQuery(search: string): ExpectedViewport | null {
  const q = new URLSearchParams(search);
  const w = Number(q.get("w"));
  const h = Number(q.get("h"));
  return q.has("w") && q.has("h") && Number.isFinite(w) && Number.isFinite(h) ? { width: w, height: h } : null;
}

// The window's inner size includes a scrollbar (clientWidth would not): a
// page taller than the web view still fills it.
const documentViewport = (): ExpectedViewport => ({
  width: window.innerWidth,
  height: window.innerHeight,
});

export async function runSelfTest(
  backend: Backend,
  expected: ExpectedViewport | null = null,
  viewport: () => ExpectedViewport = documentViewport,
): Promise<SelfTestResult> {
  const log: string[] = [];
  let latest: PlaybackStatus = await backend.getStatus();
  const waiters = new Set<() => void>();
  backend.onStatus((s) => {
    latest = s;
    for (const w of [...waiters]) w();
  });

  // Resolves when pred holds for the latest status event (also polled every 50 ms).
  const waitFor = (what: string, pred: (s: PlaybackStatus) => boolean, timeoutMs: number) =>
    new Promise<PlaybackStatus>((resolve, reject) => {
      const deadline = Date.now() + timeoutMs;
      const finish = () => {
        waiters.delete(check);
        clearInterval(timer);
      };
      const check = () => {
        if (pred(latest)) {
          finish();
          resolve(latest);
        } else if (Date.now() > deadline) {
          finish();
          reject(new Error(`timeout: ${what} (state=${latest.state}, pos=${latest.positionFrames})`));
        }
      };
      const timer = setInterval(check, 50);
      waiters.add(check);
      check();
    });

  try {
    // The page must actually have room to show itself (a 0x0 web view runs
    // scripts fine but shows nothing).
    const { width: w, height: h } = viewport();
    if (w < 200 || h < 200) throw new Error(`page is too small to be visible: ${w}x${h}`);
    if (expected && (Math.abs(w - expected.width) > 2 || Math.abs(h - expected.height) > 2)) {
      throw new Error(`page ${w}x${h} does not fill its web view ${expected.width}x${expected.height}`);
    }
    log.push(`page size ${w}x${h}${expected ? ` (web view ${expected.width}x${expected.height})` : ""}`);

    if (latest.state !== "stopped" || latest.totalFrames <= 0) throw new Error(`not loaded: state=${latest.state}`);
    const total = latest.totalFrames;
    log.push(`loaded ${latest.fileName}, ${total} frames`);

    await backend.play();
    await waitFor("playing with progress", (s) => s.state === "playing" && s.positionFrames > 0, 5000);
    log.push("playing, position advances (event)");

    // The meters (task 4-4) arrive through the bridge while playing.
    const measuring = await waitFor(
      "meter values while playing",
      (s) => s.meter.state === "measuring" && s.meter.reading !== null && s.meter.reading.momentaryLufs !== "-inf",
      5000,
    );
    log.push(`meter: momentary ${measuring.meter.reading!.momentaryLufs} LUFS`);

    await backend.seek(total / 2);
    const rate = latest.sourceFormat?.sampleRate || 48000;
    const remainingMs = ((total / 2) / rate) * 1000;
    const ended = await waitFor("end after seek", (s) => s.state === "ended", remainingMs + 5000);
    if (ended.positionFrames !== total) throw new Error(`end position ${ended.positionFrames} != ${total}`);
    log.push("seek to half, played to the end");

    // After the end the track's values stay: every frame measured (none skipped).
    const m = (await waitFor("meter idle after the end", (s) => s.meter.state === "idle" && s.meter.reading !== null, 5000))
      .meter;
    const r = m.reading!;
    if (m.droppedFrames !== 0) throw new Error(`meter skipped ${m.droppedFrames} frames`);
    if (r.truePeakMax.some((v) => v === "-inf") || r.integratedLufs === "-inf")
      throw new Error(`meter has no values: ${JSON.stringify(r)}`);
    log.push(`meter after the end: true peak max ${r.truePeakMax.join(" / ")} dBTP, integrated ${r.integratedLufs} LUFS`);

    await backend.play();
    await waitFor(
      "playing again from the start",
      (s) => s.state === "playing" && s.positionFrames > 0 && s.positionFrames < total / 2,
      5000,
    );
    await backend.stop();
    const stopped = await waitFor("stopped", (s) => s.state === "stopped", 5000);
    if (stopped.positionFrames <= 0 || stopped.positionFrames >= total) throw new Error(`stop position ${stopped.positionFrames}`);
    log.push(`restart and stop at ${stopped.positionFrames}`);

    // The settings (task 6-3): 音作りモード with an EQ takes the processing
    // path; back in 原音モード the direct path again. The last run stays a
    // direct one, so the app's byte check of the dummy output still applies.
    const saved = await backend.settings.get();
    if (saved.listening.mode !== "original" || saved.listening.ispSafeOriginal !== "off")
      throw new Error(`settings: unexpected defaults ${JSON.stringify(saved.listening)}`);
    const eqBand = { type: "peak" as const, frequency: 1000, gainDb: 6, q: 1, enabled: true };
    const set = await backend.settings.setListening({ ...saved.listening, mode: "processed", eqBands: [eqBand] });
    if (!set.ok || !set.eq || Math.abs(set.eq.preampDb + 6) > 0.05)
      throw new Error(`settings: EQ preamp ${JSON.stringify(set)}`);
    const back = await backend.settings.get();
    if (back.listening.mode !== "processed" || back.listening.eqBands.length !== 1)
      throw new Error(`settings: not kept ${JSON.stringify(back.listening)}`);
    const curve = await backend.settings.eqCurve([eqBand]);
    const at1k = curve.points.reduce((best, p) => (Math.abs(p[0] - 1000) < Math.abs(best[0] - 1000) ? p : best));
    if (Math.abs(at1k[1] - 6) > 0.3) throw new Error(`settings: curve at ${at1k[0]} Hz is ${at1k[1]} dB`);
    // The page draws the EQ with its own copy of the formulas (eqResponse.ts):
    // it must agree with the engine's, for every filter type.
    const mixed: EqBand[] = [
      { type: "lowShelf", frequency: 105, gainDb: 5.5, q: 0.7, enabled: true },
      { type: "peak", frequency: 2900, gainDb: 3.1, q: 2.1, enabled: true },
      { type: "highShelf", frequency: 10000, gainDb: -2, q: 0.7, enabled: true },
      { type: "highPass", frequency: 25, gainDb: 0, q: 0.71, enabled: true },
      { type: "lowPass", frequency: 18000, gainDb: 0, q: 0.71, enabled: true },
      { type: "bandPass", frequency: 440, gainDb: 0, q: 0.5, enabled: true },
      { type: "notch", frequency: 60, gainDb: 0, q: 8, enabled: true },
      { type: "peak", frequency: 700, gainDb: 9, q: 1, enabled: false },
    ];
    const engine = await backend.settings.eqCurve(mixed);
    const page = responsePoints(mixed, engine.sampleRate, engine.points.length);
    const worst = engine.points.reduce((m, [hz, db], k) => {
      if (Math.abs(page[k][0] - hz) > 1e-6 * hz) return Infinity;
      return db < -80 ? m : Math.max(m, Math.abs(page[k][1] - db)); // deep in the notch: both far down
    }, 0);
    const preampDiff = Math.abs(autoPreampDb(mixed, engine.sampleRate) - engine.preampDb);
    if (worst > 0.001 || preampDiff > 0.001)
      throw new Error(`settings: the page's EQ curve differs from the engine's by ${worst} dB (preamp ${preampDiff} dB)`);
    log.push(`settings: page and engine EQ curves agree within ${worst.toExponential(1)} dB (preamp ${engine.preampDb.toFixed(2)} dB)`);
    const outputs = await backend.settings.outputs();
    log.push(
      `settings: 音作りモード with EQ, preamp ${set.eq.preampDb.toFixed(2)} dB, ` +
        `${outputs.wasapi.length} WASAPI devices, ${outputs.asio.length} ASIO drivers listed`,
    );

    await backend.seek(0);
    await backend.play();
    const processed = await waitFor(
      "processing path in 音作りモード",
      (s) => s.state === "playing" && s.path !== null && s.path.processing && s.stages.some((st) => st.name === "eq"),
      5000,
    );
    log.push(`processing path: ${processed.stages.map((st) => st.name).join(", ")}`);
    // Solo (not saved) and back.
    await backend.settings.eqSolo(0);
    await waitFor("solo reported", (s) => s.eqSolo === 0, 5000);
    if ((await backend.settings.get()).listening.eqBands.length !== 1) throw new Error("settings: solo changed the saved bands");
    await backend.settings.eqSolo(null);
    await waitFor("solo off", (s) => (s.eqSolo ?? null) === null, 5000);
    log.push("EQ solo on and off");
    await backend.stop();
    await waitFor("stopped", (s) => s.state === "stopped", 5000);

    await backend.settings.setListening(saved.listening);
    if ((await backend.settings.get()).listening.mode !== "original") throw new Error("settings: not back to 原音モード");
    await backend.seek(0);
    await backend.play();
    await waitFor(
      "direct path in 原音モード",
      (s) => s.state === "playing" && s.positionFrames > 0 && s.path !== null && !s.path.processing,
      5000,
    );
    await backend.stop();
    const last = await waitFor("stopped", (s) => s.state === "stopped", 5000);
    if (last.positionFrames <= 0 || last.positionFrames >= total) throw new Error(`stop position ${last.positionFrames}`);
    log.push(`原音モード again: direct path, stopped at ${last.positionFrames}`);

    // The library (task 6-1): the test WAV's folder is watched; its track
    // reaches the list through the scan, the tag reading and the bridge.
    // (In the app only: the browser's mock library is checked by its own tests.)
    if (backend.kind !== "app") return { ok: true, log };
    const name = latest.fileName.replace(/\.[^.]+$/, "");
    let found = false;
    for (let i = 0; i < 100 && !found; i++) {
      const page = await backend.library.tracks({ search: name, offset: 0, limit: 10 });
      found = page.rows.some((r) => r.title === name);
      if (!found) await new Promise((r) => setTimeout(r, 100));
    }
    if (!found) throw new Error(`library: ${name} not in the track list`);
    const counts = await backend.library.counts();
    if (counts.folders !== 1 || counts.tracks < 1) throw new Error(`library counts ${JSON.stringify(counts)}`);
    log.push(`library: ${name} is in the track list (${counts.tracks} tracks, ${counts.folders} folder)`);

    return { ok: true, log };
  } catch (e) {
    log.push(String(e));
    return { ok: false, log };
  }
}
