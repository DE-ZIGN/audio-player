// End-to-end self test, run when the page is opened with ?selftest=1 (the app
// does this for --self-test). Drives the real native functions and waits for
// the status events coming back from the engine. The app then checks the
// bytes that reached its dummy output.

import type { Backend } from "./backend";
import { sideRequest } from "./abx";
import { FLAT_PRESET, type EqPreset } from "./eqPresets";
import { autoPreampDb, responsePoints } from "./eqResponse";
import { ispWarning, pathModel, verdictModel } from "./outputCheck";
import { applyUiView, currentUiView, type UiView } from "./uiView";
import type { EqBand } from "./settings";
import type { PlaybackStatus } from "./status";
import { rateText } from "./outputRate";

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

    // The test plays its track once, to the end. The page has just applied
    // the repeat mode the user last chose (main.ts: it is kept in the web
    // view's storage, which this test shares with the app as used): with
    // repeat on, the track never ends (2026-11-19: "timeout: end after
    // seek"). Off for this run only; what is saved stays as it is.
    await backend.setRepeat("off");

    await backend.play();
    await waitFor("playing with progress", (s) => s.state === "playing" && s.positionFrames > 0, 5000);
    log.push("playing, position advances (event)");

    // The output check (task 6-4) from the real status: the direct path in
    // 原音モード, nothing against BIT-PERFECT but the dummy output.
    const direct = await waitFor("path reported", (s) => s.path !== null && s.bitPerfect !== null, 5000);
    const dm = pathModel(direct);
    const dv = verdictModel(direct);
    if (direct.mode !== "original" || direct.ispSafe !== "off") throw new Error(`output check: mode ${direct.mode} / ${direct.ispSafe}`);
    if (dm.processing.badge !== "BYPASS（原音経路）" || dm.processing.rows.some((r) => r.on))
      throw new Error(`output check: processing ${JSON.stringify(dm.processing)}`);
    if (!dv || dv.checks.some((c) => c.state === "ng")) throw new Error(`output check: ${JSON.stringify(dv)}`);
    if (ispWarning(direct) !== null) throw new Error("output check: ISP warning for a track without a measured peak");
    log.push(`output check: ${dm.file.lines.join(" ")} → ${dm.decoder.lines.join(" ")} → ${dm.processing.badge} → ${dm.output.lines.join(" ")}`);

    // The meters (task 4-4) arrive through the bridge while playing.
    const measuring = await waitFor(
      "meter values while playing",
      (s) => s.meter.state === "measuring" && s.meter.reading !== null && s.meter.reading.momentaryLufs !== "-inf",
      5000,
    );
    log.push(`meter: momentary ${measuring.meter.reading!.momentaryLufs} LUFS`);
    // The VU meters' value (task 6-8): per channel, a sine reads its RMS.
    const vu = measuring.meter.reading!.vu;
    if (vu.length !== 2 || vu.some((v) => v === "-inf")) throw new Error(`VU: ${JSON.stringify(vu)}`);
    log.push(`VU: ${vu.map((v) => (v as number).toFixed(1)).join(" / ")} dBFS`);

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
    const pv = verdictModel(processed);
    const eqRow = pathModel(processed).processing.rows.find((r) => r.name === "EQ");
    if (!pv || pv.bitPerfect || !eqRow?.on || processed.mode !== "processed")
      throw new Error(`output check (processing): ${JSON.stringify(pv)} ${JSON.stringify(eqRow)}`);
    log.push(`output check: ${pv.title}; EQ ${eqRow.value}`);
    // Solo (not saved) and back.
    await backend.settings.eqSolo(0);
    await waitFor("solo reported", (s) => s.eqSolo === 0, 5000);
    if ((await backend.settings.get()).listening.eqBands.length !== 1) throw new Error("settings: solo changed the saved bands");
    await backend.settings.eqSolo(null);
    await waitFor("solo off", (s) => (s.eqSolo ?? null) === null, 5000);
    log.push("EQ solo on and off");
    await backend.stop();
    await waitFor("stopped", (s) => s.state === "stopped", 5000);

    // The tone and the tube (task 9-3) on the real chain: in the path after
    // the EQ, with the tube's type and DRIVE, then back to the EQ alone.
    const eqListening = { ...saved.listening, mode: "processed" as const, eqBands: [eqBand] };
    await backend.settings.setListening({
      ...eqListening,
      toneBassDb: 3,
      tubeEnabled: true,
      tubeType: "ppel34",
      tubeDriveDb: -6,
      crossfeedEnabled: true, // task 9-5
      crossfeedLevel: "cmoy",
      spaceRoom: "club", // task 9-6
      spaceMixPercent: 15,
    });
    if ((await backend.settings.get()).listening.tubeType !== "ppel34") throw new Error("settings: the tube type was not kept");
    await backend.seek(0);
    await backend.play();
    const tubed = await waitFor(
      "the tone and the tube on the processing path",
      (s) =>
        s.state === "playing" &&
        s.stages.some((st) => st.name === "tube") &&
        s.stages.some((st) => st.name === "tone") &&
        s.stages.some((st) => st.name === "space") &&
        s.stages.some((st) => st.name === "crossfeed"),
      5000,
    );
    const names = tubed.stages.map((st) => st.name);
    const tubeStage = tubed.stages.find((st) => st.name === "tube")!;
    const tubeRow = pathModel(tubed).processing.rows.find((r) => r.name === "真空管");
    const spaceRow = pathModel(tubed).processing.rows.find((r) => r.name === "空間");
    if (
      !(names.indexOf("eq") < names.indexOf("tone") && names.indexOf("tone") < names.indexOf("tube")) ||
      names.indexOf("space") !== names.indexOf("tube") + 1 ||
      names.indexOf("crossfeed") !== names.indexOf("space") + 1 ||
      spaceRow?.value !== "CLUB・MIX 15%" ||
      tubed.stages.find((st) => st.name === "crossfeed")?.detail !== "cmoy" ||
      tubeStage.detail !== "ppel34" ||
      tubeStage.gainDb !== -6 ||
      tubeRow?.value !== "PP EL34・DRIVE −6.0 dB"
    )
      throw new Error(`tube: ${JSON.stringify(tubed.stages)} ${JSON.stringify(tubeRow)} ${JSON.stringify(spaceRow)}`);
    log.push(`tone and tube: ${names.join(", ")}; output check: 真空管 ${tubeRow.value}, 空間 ${spaceRow.value}`);

    // The space's user IR (task 9-6): a file that cannot be read is reported,
    // and the room keeps playing on its built-in IR.
    const spaceListening = (await backend.settings.get()).listening;
    const irSet = await backend.settings.setListening({
      ...spaceListening,
      spaceSource: ["file", spaceListening.spaceSource[1], spaceListening.spaceSource[2]],
      spaceIr: ["Z:\\no such folder\\selftest-ir.wav", "", ""],
    });
    const irState = irSet.spaceIrs?.[0];
    if (!irSet.ok || !irState || irState.loaded || !irState.error)
      throw new Error(`space IR: an unreadable file was not reported ${JSON.stringify(irSet)}`);
    const builtIn = await waitFor(
      "the space on its built-in IR",
      (s) => s.stages.find((st) => st.name === "space")?.detail === "club;15",
      3000,
    );
    log.push(`space IR: unreadable file reported (${irState.error}); ${builtIn.stages.find((st) => st.name === "space")?.detail}`);
    // HALL plays the bundled measured IR (St Paul's Hall) by default.
    await backend.settings.setListening({ ...(await backend.settings.get()).listening, spaceRoom: "hall" });
    const measured = await waitFor(
      "HALL on its measured IR",
      (s) => s.stages.find((st) => st.name === "space")?.detail === "hall;15;measured",
      3000,
    );
    const hallRow = pathModel(measured).processing.rows.find((r) => r.name === "空間");
    if (hallRow?.value !== "HALL（実測）・MIX 15%") throw new Error(`space: HALL ${JSON.stringify(hallRow)}`);
    log.push(`space: HALL plays the measured IR (${hallRow.value})`);

    // The tube's show (task 9-8): the warm-up and a unit reach the stage and
    // the output check (the app started moments ago: a cold tube).
    const showBase = (await backend.settings.get()).listening;
    await backend.settings.setListening({ ...showBase, tubeWarmUp: true, tubeUnits: true, tubeUnit: 7 });
    const shown = await waitFor(
      "the tube's warm-up and unit",
      (s) => /^ppel34;warmup=\d+;unit=7$/.test(s.stages.find((st) => st.name === "tube")?.detail ?? ""),
      5000,
    );
    const showRow = pathModel(shown).processing.rows.find((r) => r.name === "真空管");
    const showSub = showRow?.sub ?? [];
    if (
      showRow?.value !== "PP EL34・DRIVE −6.0 dB" ||
      showSub.length !== 2 ||
      !/^(ウォームアップ中（起動から \d+ 分）|暖まった状態)$/.test(showSub[0]) ||
      showSub[1] !== "球 No.7"
    )
      throw new Error(`tube show: ${JSON.stringify(shown.stages)} ${JSON.stringify(showRow)}`);
    log.push(`tube show: ${showRow.value} / ${showSub.join(" / ")}`);
    await backend.settings.setListening({ ...showBase, tubeWarmUp: false, tubeUnits: false });
    await waitFor("the tube without the show", (s) => s.stages.find((st) => st.name === "tube")?.detail === "ppel34", 3000);

    // The output rate (task 10-4): twice the track's rate in 音作りモード -
    // the real engine converts 48 to 96 kHz at the end of the path.
    const rateBase = (await backend.settings.get()).listening;
    await backend.settings.setListening({ ...rateBase, outputRateMode: "multiple", outputRateMultiple: 2 });
    const doubled = await waitFor(
      "the output at twice the rate",
      (s) => s.stages.some((st) => st.name === "resampler") && s.outputFormat?.sampleRate === 2 * (s.sourceFormat?.sampleRate ?? 0),
      5000,
    );
    const rateRow = pathModel(doubled).processing.rows.find((r) => r.name === "リサンプラー");
    const from = doubled.sourceFormat?.sampleRate ?? 0;
    if (rateRow?.value !== `${rateText(from)} → ${rateText(2 * from)}（設定）`)
      throw new Error(`output rate: ${JSON.stringify(doubled.stages)} ${JSON.stringify(rateRow)}`);
    log.push(`output rate: ${rateRow.value}`);
    await backend.settings.setListening({ ...rateBase, outputRateMode: "track" });
    await waitFor("the track's rate again", (s) => !s.stages.some((st) => st.name === "resampler") && s.outputFormat?.sampleRate === s.sourceFormat?.sampleRate, 5000);

    // The tube type by genre (task 9-7) through the real bridge: saved and read
    // back; a file opened by itself has no genre or album, so the manual type plays.
    const savedAuto = (await backend.settings.get()).tubeAuto;
    const auto = {
      enabled: true,
      rules: [{ genre: "Selftest Genre", type: "se300b" as const }],
      albums: [{ albumKey: "selftest\u001falbum", album: "Selftest Album", type: "ppclassa" as const }],
    };
    const setAuto = await backend.settings.setTubeAuto(auto);
    if (!setAuto.ok || JSON.stringify((await backend.settings.get()).tubeAuto) !== JSON.stringify(auto))
      throw new Error(`tube auto: not kept ${JSON.stringify(setAuto)}`);
    const autoStatus = await waitFor("the tube choice in the status", (s) => s.tubeAuto !== undefined, 3000);
    if (
      autoStatus.tubeAuto?.choice !== null ||
      autoStatus.tubeAuto.album !== null ||
      autoStatus.tubeAuto.genres.length !== 0 ||
      (backend.kind === "app" && autoStatus.stages.find((st) => st.name === "tube")?.detail !== "ppel34")
    )
      throw new Error(`tube auto: ${JSON.stringify(autoStatus.tubeAuto)} ${JSON.stringify(autoStatus.stages)}`);
    if (savedAuto) await backend.settings.setTubeAuto(savedAuto);
    log.push("tube auto: saved and read back; no genre, so the manual type (PP EL34) plays");
    await backend.stop();
    await waitFor("stopped", (s) => s.state === "stopped", 5000);
    await backend.settings.setListening(eqListening);

    // ABX (task 6-6) through the real bridge, still in 音作りモード with the EQ:
    // the 2 s file as the region, longer than the file plays (it loops), then
    // the queue and the position come back.
    const beforeAbx = latest;
    const started = await backend.abx.start({ kind: "processed", startSeconds: 0, seconds: 2, trials: 4 });
    if (!started.ok) throw new Error(`ABX: ${started.message}`);
    await waitFor(
      "ABX playing the region through the comparison",
      (s) => s.abx?.running === true && s.state === "playing" && s.path?.comparison === true,
      5000,
    );
    await new Promise((r) => setTimeout(r, 2600)); // past the end of the 2 s region
    const afterLoop = (await backend.getStatus()).state;
    if (afterLoop !== "playing") throw new Error(`ABX: the region did not loop (state ${afterLoop})`);
    await backend.abx.choose("X");
    await waitFor("ABX listening to X", (s) => s.abx?.listening === "X", 5000);
    for (let i = 0; i < 4; i++) await backend.abx.answer("A");
    const abxEnd = await waitFor("ABX result", (s) => s.abx?.result?.finished === true, 5000);
    const abxResult = abxEnd.abx!.result!;
    if (abxResult.correct === null || abxResult.correct < 0 || abxResult.correct > 4 || abxResult.pValue === null)
      throw new Error(`ABX: result ${JSON.stringify(abxResult)}`);
    const restored = await waitFor(
      "queue back after ABX",
      (s) => s.state === "stopped" && s.queueLength === beforeAbx.queueLength,
      5000,
    );
    if (restored.fileName !== beforeAbx.fileName) throw new Error(`ABX: track not put back (${restored.fileName})`);
    log.push(
      `ABX: 4 trials, ${abxResult.correct} correct, p = ${abxResult.pValue.toFixed(4)}, match ${started.matchDb?.toFixed(2)} dB, ` +
        `looped past the end, queue put back`,
    );
    await backend.abx.close();
    await waitFor("ABX closed", (s) => (s.abx ?? null) === null, 5000);

    // EQ presets and the ABX's 設定 A と設定 B (task 6-9): the list is saved
    // and read back; A = no EQ, B = a preset, each on the current settings.
    const savedPresets = saved.eqPresets ?? [];
    const presetB: EqPreset = { name: "自己テスト +6 dB", bands: [eqBand], autoPreamp: true, manualPreampDb: 0 };
    const stored = await backend.settings.setEqPresets([...savedPresets, presetB]);
    const readBack = (await backend.settings.get()).eqPresets ?? [];
    if (!stored.ok || readBack.at(-1)?.name !== presetB.name || readBack.at(-1)?.bands.length !== 1)
      throw new Error(`EQ presets: not kept ${JSON.stringify(readBack.at(-1))}`);
    const pair = await backend.abx.start({
      kind: "presets",
      startSeconds: 0,
      seconds: 2,
      trials: 2,
      a: FLAT_PRESET,
      b: presetB,
    });
    if (!pair.ok) throw new Error(`ABX 設定 A と設定 B: ${pair.message}`);
    const pairRunning = await waitFor(
      "ABX of two presets playing",
      (s) => s.abx?.running === true && s.abx.kind === "presets" && s.state === "playing" && s.path?.comparison === true,
      5000,
    );
    if (pairRunning.abx?.aName !== FLAT_PRESET.name || pairRunning.abx?.bName !== presetB.name)
      throw new Error(`ABX 設定 A と設定 B: names ${pairRunning.abx?.aName} / ${pairRunning.abx?.bName}`);
    await backend.abx.choose("B");
    await waitFor("ABX listening to B", (s) => s.abx?.listening === "B", 5000);
    await backend.abx.abort();
    await waitFor("ABX of two presets stopped", (s) => s.abx?.running === false && s.state === "stopped", 5000);
    await backend.abx.close();
    await waitFor("ABX closed", (s) => (s.abx ?? null) === null, 5000);
    await backend.settings.setEqPresets(savedPresets); // the user's list as it was
    log.push(
      `EQ presets saved and read back; ABX 設定 A「${FLAT_PRESET.name}」と 設定 B「${presetB.name}」: match ${pair.matchDb?.toFixed(2)} dB, aborted`,
    );

    // 設定 A と設定 B with the tube (task 9-4): no tube against PP EL34 at
    // DRIVE 0 dB - the tube on one side only, so the sides delay differently
    // and the engine lines them up. Answered to the end.
    const current = await backend.settings.get();
    const tubeA = sideRequest({ eq: "current", tone: "current", tube: "none", drive: 0 }, current.listening, []);
    const tubeB = sideRequest({ eq: "current", tone: "current", tube: "ppel34", drive: 0 }, current.listening, []);
    const tubes = await backend.abx.start({ kind: "presets", startSeconds: 0, seconds: 2, trials: 2, a: tubeA, b: tubeB });
    if (!tubes.ok) throw new Error(`ABX tube: ${tubes.message}`);
    const tubesRunning = await waitFor(
      "ABX of the tube playing",
      // (the mock engine does not run the sides' settings: the tube's stage only in the app)
      (s) =>
        s.abx?.running === true && s.state === "playing" && (backend.kind !== "app" || s.stages.some((st) => st.name === "tube")),
      5000,
    );
    if (tubesRunning.abx?.bName !== tubeB.name) throw new Error(`ABX tube: names ${tubesRunning.abx?.aName} / ${tubesRunning.abx?.bName}`);
    await backend.abx.choose("A");
    await waitFor("ABX listening to A", (s) => s.abx?.listening === "A", 5000);
    for (let i = 0; i < 2; i++) await backend.abx.answer("B");
    const tubesEnd = await waitFor("ABX tube result", (s) => s.abx?.result?.finished === true, 5000);
    await backend.abx.close();
    await waitFor("ABX closed", (s) => (s.abx ?? null) === null, 5000);
    log.push(
      `ABX 設定 A「${tubeA.name}」と 設定 B「${tubeB.name}」: match ${tubes.matchDb?.toFixed(2)} dB, ` +
        `${tubesEnd.abx!.result!.correct} of 2`,
    );

    // The tube's show in the ABX (task 9-8): B held at switch-on (cold) with
    // unit 5, against A as it is now. B plays through the main chain, so its
    // stage says the warm-up's time is held at 0.
    const showA = sideRequest({ eq: "current", tone: "current", tube: "ppel34", drive: 0, warmUp: "now" }, current.listening, []);
    const showB = sideRequest(
      { eq: "current", tone: "current", tube: "ppel34", drive: 0, warmUp: "cold", unit: 5 },
      current.listening,
      [],
    );
    const shows = await backend.abx.start({ kind: "presets", startSeconds: 0, seconds: 2, trials: 2, a: showA, b: showB });
    if (!shows.ok) throw new Error(`ABX show: ${shows.message}`);
    const showRunning = await waitFor(
      "ABX of the warm-up held cold",
      (s) =>
        s.abx?.running === true &&
        s.state === "playing" &&
        (backend.kind !== "app" || s.stages.find((st) => st.name === "tube")?.detail === "ppel34;warmup=0;unit=5"),
      5000,
    );
    await backend.abx.abort();
    await waitFor("ABX of the show stopped", (s) => s.abx?.running === false, 5000);
    await backend.abx.close();
    await waitFor("ABX closed", (s) => (s.abx ?? null) === null, 5000);
    log.push(
      `ABX show: A「${showA.name}」と B「${showB.name}」: match ${shows.matchDb?.toFixed(2)} dB; ` +
        `B's tube ${showRunning.stages.find((st) => st.name === "tube")?.detail ?? "(mock)"}`,
    );

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

    // The views (tasks 6-7, 9-9) show the same information: the output field
    // and the True Peak column are in view in each. Shown only, not saved:
    // the view the user last used stays as it was.
    const userView = currentUiView();
    const frame = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 50)));
    const seen: string[] = [];
    for (const v of ["standard", "library", "amp"] as UiView[]) {
      applyUiView(v);
      await frame();
      const output = document.getElementById("output")!.getBoundingClientRect();
      const body = document.getElementById("table-body")!.getBoundingClientRect();
      const tp = [...document.getElementById("table-head")!.children].find((c) => /True Peak/i.test(c.textContent ?? ""));
      const tpRect = tp?.getBoundingClientRect();
      if (output.width < 100 || output.right > window.innerWidth + 1) throw new Error(`${v} view: the output field is not in view`);
      if (!tpRect || tpRect.width === 0 || tpRect.right > body.right + 1) throw new Error(`${v} view: the True Peak column is not in view`);
      if (v === "amp") {
        // The amp's pictures are in the build, and its five knobs are on them.
        // Both amps (SE and push-pull), one shown at a time.
        const scenes = [...document.querySelectorAll<HTMLImageElement>(".amp-scene")];
        if (scenes.length !== 4 || scenes.some((i) => !i.complete || i.naturalWidth !== 1672))
          throw new Error(`amp view: the pictures did not load (${scenes.map((i) => i.naturalWidth).join(", ")})`);
        const shown = document.querySelectorAll(".amp-stage.active");
        if (shown.length !== 1) throw new Error(`amp view: ${shown.length} amps shown`);
        const knobs = [...shown[0].querySelectorAll(".amp-knob")].filter((k) => k.getBoundingClientRect().width > 10);
        if (knobs.length !== 5) throw new Error(`amp view: ${knobs.length} knobs in view`);
        const prev = document.getElementById("prev")!;
        if (!prev.closest(".amp-stage.active")) throw new Error("amp view: the transport is not on the amp shown");
      }
      // Shuffle and repeat are in every view (task 9-9).
      for (const id of ["shuffle", "repeat"]) {
        const r = document.getElementById(id)!.getBoundingClientRect();
        if (r.width < 10 || r.right > window.innerWidth + 1 || r.bottom > window.innerHeight + 1)
          throw new Error(`${v} view: the ${id} button is not in view`);
      }
      const rowH = getComputedStyle(document.documentElement).getPropertyValue("--row-h").trim();
      seen.push(`${v} (rows ${rowH})`);
    }
    applyUiView(userView);
    await frame();
    if (userView !== "amp" && document.getElementById("prev")!.closest(".amp-stage"))
      throw new Error("views: the transport stayed on the amp");
    log.push(`views: output field and True Peak column in view in ${seen.join(", ")}; back to ${userView}`);

    return { ok: true, log };
  } catch (e) {
    log.push(String(e));
    return { ok: false, log };
  }
}
