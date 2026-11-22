import { describe, expect, it } from "vitest";
import { errorLine } from "./errorLine";
import libraryView from "./libraryView.ts?raw";
import main from "./main.ts?raw";

// The status comes up to 10 times a second: its error, unchanged, must not
// take away the answer to what was just pressed.

describe("the error line", () => {
  it("shows the playback's error", () => {
    expect(errorLine("", null, "load:1")).toBe("status");
    expect(errorLine("status", "load:1", "load:1")).toBe("status"); // again, in the language in use
  });

  it("keeps an action's message over an error that was there before it", () => {
    expect(errorLine("action", "load:1", "load:1")).toBe("keep");
  });

  it("shows an error that comes after it", () => {
    expect(errorLine("action", "load:1", "load:2")).toBe("status");
    expect(errorLine("action", null, "load:1")).toBe("status");
  });

  it("hides the playback's error when it is over, not an action's message", () => {
    expect(errorLine("status", "load:1", null)).toBe("hide");
    expect(errorLine("action", "load:1", null)).toBe("keep");
  });

  it("the library's messages are an action's", () => {
    const show = /showError\(text: \(\) => string\) \{[\s\S]*?\n {2}\}/.exec(libraryView)?.[0];
    expect(show).toMatch(/dataset\.from = "action"/);
  });

  it("an action's message goes when playing or an edit of the queue then works", () => {
    expect(main).toMatch(/if \(r\.ok\) clearActionError\(\)/);
    expect(main).toMatch(/if \(done === "done"\) clearActionError\(\)/);
  });
});
