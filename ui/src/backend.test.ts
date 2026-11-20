import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { AppBackend, MockBackend } from "./backend";
import { setLang } from "./i18n";
import type { PlaybackStatus } from "./status";

// Task 11-5: the article reader shows its words in the page's language, which
// it gets in its address (it has no storage of the main page's).
describe("opening the article reader", () => {
  afterEach(() => {
    setLang("ja");
    vi.unstubAllGlobals();
  });

  it("tells the app the language in use", async () => {
    const calls: unknown[][] = [];
    const fn = (name: string) => async (...args: unknown[]) => {
      if (name === "openArticle") calls.push(args);
      return { ok: true };
    };
    const b = new AppBackend(fn, "/art/track/");
    setLang("en");
    expect(await b.openArticle("abx", "level-matching")).toEqual({ ok: true });
    setLang("ja");
    await b.openArticle();
    expect(calls).toEqual([
      ["abx", "level-matching", "en"],
      ["", "", "ja"],
    ]);
  });

  it("the mock opens the reader's page with the language in its address", async () => {
    const b = new MockBackend();
    const open = vi.fn();
    vi.stubGlobal("window", { open });
    setLang("en");
    await b.openArticle("abx", "level-matching");
    setLang("ja");
    await b.openArticle();
    expect(open.mock.calls.map((c) => c[0])).toEqual(["reader.html?lang=en&id=abx#level-matching", "reader.html?lang=ja"]);
  });
});

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

  it("plays a queue, recording gapless joins and rate switches", async () => {
    const b = new MockBackend([
      { name: "a", sampleRate: 44100, seconds: 1 },
      { name: "b", sampleRate: 44100, seconds: 1 },
      { name: "c", sampleRate: 96000, seconds: 1 },
    ]);
    await b.openFile();
    expect((await b.getStatus()).queueLength).toBe(3);
    await b.play();
    vi.advanceTimersByTime(4000);
    const s = await b.getStatus();
    expect(s.state).toBe("ended");
    expect(s.queueIndex).toBe(2);
    expect(s.transitions.map((t) => t.kind)).toEqual(["gapless", "reopened"]);
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
