import { describe, expect, it } from "vitest";
import cmake from "../../CMakeLists.txt?raw";
import appCmake from "../../app/CMakeLists.txt?raw";
import mainCpp from "../../app/src/Main.cpp?raw";
import mainComponentCpp from "../../app/src/MainComponent.cpp?raw";
import articleWindowCpp from "../../app/src/ArticleWindow.cpp?raw";
import versionRc from "../../app/resources/version.rc.in?raw";
import html from "../index.html?raw";

// The product's name (2026-11-18): DE:ZIGN AUDIO PLAYER. A colon cannot be
// in a file or folder name on Windows, so the name shown to the user and the
// names used for files are kept apart. Those were decided on 2026-11-21
// (task 8-3a): the executable DEZIGNAudioPlayer.exe, the data folder
// %LOCALAPPDATA%\DEZIGN Audio Player, the publisher DE:ZIGN.

const cached = (name: string) => new RegExp(`set\\(${name}\\s+"([^"]*)"\\s+CACHE STRING`).exec(cmake)?.[1] ?? "";
const NOT_IN_A_FILE_NAME = /[\\/:*?"<>|]/;

describe("the product's name", () => {
  it("is DE:ZIGN AUDIO PLAYER where it is shown", () => {
    expect(cached("AP_DISPLAY_NAME")).toBe("DE:ZIGN AUDIO PLAYER");
    expect(/<title>([^<]*)<\/title>/.exec(html)?.[1]).toBe(cached("AP_DISPLAY_NAME"));
  });
  it("reaches the window's title from the one place it is set", () => {
    expect(appCmake).toMatch(/AP_DISPLAY_NAME_STRING="\$\{AP_DISPLAY_NAME\}"/);
    expect(mainCpp).toMatch(/MainWindow>\(juce::String::fromUTF8\(AP_DISPLAY_NAME_STRING\)/);
  });
});

describe("the names used for files (task 8-3a)", () => {
  it("are the ones decided, and hold nothing a file name cannot", () => {
    expect(cached("AP_PRODUCT_NAME")).toBe("DEZIGNAudioPlayer"); // DEZIGNAudioPlayer.exe
    expect(cached("AP_DATA_FOLDER")).toBe("DEZIGN Audio Player");
    for (const name of ["AP_PRODUCT_NAME", "AP_DATA_FOLDER"]) expect(cached(name), name).not.toMatch(NOT_IN_A_FILE_NAME);
  });
  it("the publisher is DE:ZIGN (as the Store will show it)", () => {
    expect(cached("AP_COMPANY_NAME")).toBe("DE:ZIGN");
  });
  it("every folder of the app's data is under the data folder, named in one place", () => {
    expect(appCmake).toMatch(/AP_DATA_FOLDER_STRING="\$\{AP_DATA_FOLDER\}"/);
    for (const [file, text] of [
      ["Main.cpp", mainCpp],
      ["MainComponent.cpp", mainComponentCpp],
      ["ArticleWindow.cpp", articleWindowCpp],
    ] as const) {
      expect(text, file).toMatch(/windowsLocalAppData\)\s*\.getChildFile\(AP_DATA_FOLDER_STRING\)/);
      // The executable's name is not a folder's any more.
      expect(text, file).not.toMatch(/getChildFile\(JUCE_APPLICATION_NAME_STRING\)/);
    }
  });
});

describe("the executable's version information (task 8-3a)", () => {
  it("is the app's own resource file, in place of JUCE's (which would describe the exe by its file name)", () => {
    expect(appCmake).toMatch(/JUCE_USER_DEFINED_RC_FILE/);
    expect(appCmake).toMatch(/configure_file\([^)]*version\.rc\.in/);
  });
  it("describes the product by the name shown, the publisher by DE:ZIGN, and keeps the file's own name", () => {
    expect(versionRc).toMatch(/VALUE "FileDescription",\s*"@AP_DISPLAY_NAME@\\0"/);
    expect(versionRc).toMatch(/VALUE "ProductName",\s*"@AP_DISPLAY_NAME@\\0"/);
    expect(versionRc).toMatch(/VALUE "CompanyName",\s*"@AP_COMPANY_NAME@\\0"/);
    expect(versionRc).toMatch(/VALUE "OriginalFilename",\s*"@AP_PRODUCT_NAME@\.exe\\0"/);
    expect(versionRc).toMatch(/VALUE "FileVersion",\s*"@PROJECT_VERSION@\\0"/);
    expect(versionRc).toMatch(/#pragma code_page\(65001\)/); // UTF-8, as the names are
  });
});
