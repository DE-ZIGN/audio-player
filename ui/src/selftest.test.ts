import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { MockBackend } from "./backend";
import { runSelfTest } from "./selftest";

describe("runSelfTest", () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it("passes against the mock backend", async () => {
    const b = new MockBackend();
    await b.openFile();
    const pending = runSelfTest(b);
    await vi.advanceTimersByTimeAsync(40_000);
    const result = await pending;
    expect(result.log.join("\n")).not.toContain("timeout");
    expect(result.ok).toBe(true);
  });

  it("fails when nothing is loaded", async () => {
    const result = await runSelfTest(new MockBackend());
    expect(result.ok).toBe(false);
  });
});
