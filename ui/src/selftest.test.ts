import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { MockBackend } from "./backend";
import { expectedViewportFromQuery, runSelfTest } from "./selftest";

const viewport900 = () => ({ width: 900, height: 620 });

describe("runSelfTest", () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it("passes against the mock backend", async () => {
    const b = new MockBackend();
    await b.openFile();
    const pending = runSelfTest(b, { width: 900, height: 620 }, viewport900);
    await vi.advanceTimersByTimeAsync(40_000);
    const result = await pending;
    expect(result.log.join("\n")).not.toContain("timeout");
    expect(result.ok).toBe(true);
  });

  it("fails when nothing is loaded", async () => {
    const result = await runSelfTest(new MockBackend(), null, viewport900);
    expect(result.ok).toBe(false);
  });

  it("fails when the page does not fill its web view", async () => {
    const b = new MockBackend();
    await b.openFile();
    const result = await runSelfTest(b, { width: 0, height: 0 }, viewport900);
    expect(result.ok).toBe(false);
    expect(result.log.join("\n")).toContain("does not fill");
  });

  it("fails when the page is too small to see", async () => {
    const b = new MockBackend();
    await b.openFile();
    const result = await runSelfTest(b, null, () => ({ width: 0, height: 0 }));
    expect(result.ok).toBe(false);
    expect(result.log.join("\n")).toContain("too small");
  });
});

describe("expectedViewportFromQuery", () => {
  it("reads w and h", () => {
    expect(expectedViewportFromQuery("?selftest=1&w=900&h=620")).toEqual({ width: 900, height: 620 });
  });
  it("is null without them", () => {
    expect(expectedViewportFromQuery("?selftest=1")).toBeNull();
  });
});
