import { describe, expect, it } from "vitest";
import readme from "../../README.md?raw";
import dependencies from "../../cmake/Dependencies.cmake?raw";
import { ja as about } from "./i18n/parts/about";

// The repository's README: the Store listing links here as the source, so it
// says what the app is now, and no more than the listing does (CLAUDE.md
// chapter 1; the distribution was decided 2026-11-22).

describe("the repository's README", () => {
  it("is not the planning stage's any more", () => {
    expect(readme).not.toMatch(/企画と設計の段階|仮称/);
    expect(readme).toContain("DEZIGNAudioPlayer.exe");
  });

  it("never says the sound is better", () => {
    expect(readme).not.toMatch(/良くな|高音質|音質(が|の)?(向上|改善)|原音に近/);
  });

  it("says where it is distributed: the Store, and only there at first", () => {
    expect(readme).toMatch(/## 入手/);
    expect(readme).toMatch(/Microsoft Store だけ/);
    expect(readme).toContain("Media Feature Pack");
  });

  it("the claims have the listing's conditions", () => {
    expect(readme).toMatch(/音量 100%.*ISP セーフがオフ/);
    expect(readme).toMatch(/MP3.*ディザ/);
    expect(readme).toMatch(/DoP.*自動では分かりません.*大きな雑音/);
  });

  it("names every dependency that the build downloads", () => {
    const shown: Record<string, string> = {
      flac: "libFLAC",
      mpg123: "mpg123",
      ebur128: "libebur128",
      r8brain: "r8brain-free-src",
      sqlite: "SQLite",
      taglib: "TagLib",
      webview2: "WebView2 SDK",
      juce: "JUCE",
      catch2: "Catch2",
      lame: "LAME",
    };
    for (const m of dependencies.matchAll(/^\s*FetchContent_Declare\(\s*([A-Za-z0-9_]+)/gm))
      expect(readme, m[1]).toContain(shown[m[1]!.toLowerCase()] ?? m[1]);
  });

  it("carries the ASIO trademark notice", () => {
    expect(readme).toContain(about["about.trademark.asio"]);
  });
});
