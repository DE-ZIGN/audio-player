// End-to-end self test, run when the page is opened with ?selftest=1 (the app
// does this for --self-test). Drives the real native functions and waits for
// the status events coming back from the engine. The app then checks the
// bytes that reached its dummy output.

import type { Backend } from "./backend";
import type { PlaybackStatus } from "./status";

export interface SelfTestResult {
  ok: boolean;
  log: string[];
}

export async function runSelfTest(backend: Backend): Promise<SelfTestResult> {
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
    if (latest.state !== "stopped" || latest.totalFrames <= 0) throw new Error(`not loaded: state=${latest.state}`);
    const total = latest.totalFrames;
    log.push(`loaded ${latest.fileName}, ${total} frames`);

    await backend.play();
    await waitFor("playing with progress", (s) => s.state === "playing" && s.positionFrames > 0, 5000);
    log.push("playing, position advances (event)");

    await backend.seek(total / 2);
    const rate = latest.sourceFormat?.sampleRate || 48000;
    const remainingMs = ((total / 2) / rate) * 1000;
    const ended = await waitFor("end after seek", (s) => s.state === "ended", remainingMs + 5000);
    if (ended.positionFrames !== total) throw new Error(`end position ${ended.positionFrames} != ${total}`);
    log.push("seek to half, played to the end");

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

    return { ok: true, log };
  } catch (e) {
    log.push(String(e));
    return { ok: false, log };
  }
}
