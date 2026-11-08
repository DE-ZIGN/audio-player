import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { MockBackend } from "./backend";
import type { PlaybackStatus } from "./status";

describe("MockBackend", () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it("does nothing before a file is opened", async () => {
    const b = new MockBackend();
    await b.play();
    expect((await b.getStatus()).state).toBe("empty");
  });

  it("plays, reports progress, stops and seeks", async () => {
    const b = new MockBackend();
    const seen: PlaybackStatus[] = [];
    b.onStatus((s) => seen.push(s));

    await b.openFile();
    await b.play();
    expect((await b.getStatus()).state).toBe("playing");

    vi.advanceTimersByTime(1000);
    const playing = await b.getStatus();
    expect(playing.positionFrames).toBeGreaterThan(0);

    await b.stop();
    expect((await b.getStatus()).state).toBe("stopped");

    await b.seek(96000 * 10);
    expect((await b.getStatus()).positionFrames).toBe(96000 * 10);
    expect(seen.length).toBeGreaterThan(3);
  });

  it("ends at the last frame and restarts from zero", async () => {
    const b = new MockBackend();
    await b.openFile();
    await b.play();
    vi.advanceTimersByTime(31_000);
    const ended = await b.getStatus();
    expect(ended.state).toBe("ended");
    expect(ended.positionFrames).toBe(ended.totalFrames);

    await b.play();
    expect((await b.getStatus()).positionFrames).toBe(0);
  });
});
