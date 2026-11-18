import { describe, expect, it } from "vitest";
import cmake from "../../CMakeLists.txt?raw";
import appCmake from "../../app/CMakeLists.txt?raw";
import mainCpp from "../../app/src/Main.cpp?raw";
import html from "../index.html?raw";

// The product's name (2026-11-18): DE:ZIGN AUDIO PLAYER. A colon cannot be
// in a file or folder name on Windows, so the name shown to the user and the
// name used for files (the executable, the data folder) are kept apart.

const cached = (name: string) => new RegExp(`set\\(${name}\\s+"([^"]*)"\\s+CACHE STRING`).exec(cmake)?.[1] ?? "";

describe("the product's name", () => {
  it("is DE:ZIGN AUDIO PLAYER where it is shown", () => {
    expect(cached("AP_DISPLAY_NAME")).toBe("DE:ZIGN AUDIO PLAYER");
    expect(/<title>([^<]*)<\/title>/.exec(html)?.[1]).toBe(cached("AP_DISPLAY_NAME"));
  });
  it("reaches the window's title from the one place it is set", () => {
    expect(appCmake).toMatch(/AP_DISPLAY_NAME_STRING="\$\{AP_DISPLAY_NAME\}"/);
    expect(mainCpp).toMatch(/MainWindow>\(juce::String::fromUTF8\(AP_DISPLAY_NAME_STRING\)/);
  });
  it("the name used for files has nothing a file name cannot hold", () => {
    const fileName = cached("AP_PRODUCT_NAME");
    expect(fileName.length).toBeGreaterThan(0);
    expect(fileName).not.toMatch(/[\\/:*?"<>|]/);
    expect(fileName).not.toBe(cached("AP_DISPLAY_NAME"));
  });
});
