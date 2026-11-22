import { describe, expect, it } from "vitest";
import cmake from "../../CMakeLists.txt?raw";
import appCmake from "../../app/CMakeLists.txt?raw";
import manifest from "../../packaging/msix/AppxManifest.xml.in?raw";
import script from "../../packaging/msix/build-msix.ps1?raw";
import reswJa from "../../packaging/msix/Strings/ja-JP/Resources.resw?raw";
import reswEn from "../../packaging/msix/Strings/en-US/Resources.resw?raw";

// The app's MSIX package (task 8-3c): the manifest's template, filled by
// CMake, and the script that packs it. The words are in Japanese and English
// (M11), and the identity is the one Partner Center gave (task 8-5e).

const cached = (name: string) => new RegExp(`set\\(${name}\\s+"([^"]*)"\\s+CACHE STRING`).exec(cmake)?.[1];
const assets = Object.keys(import.meta.glob("../../packaging/msix/Assets/*.png")).map((p) => p.split("/").pop()!);
const words = (resw: string) =>
  Object.fromEntries([...resw.matchAll(/<data name="([^"]+)"[^>]*>\s*<value>([^<]*)<\/value>/g)].map((m) => [m[1], m[2]]));

describe("the MSIX manifest", () => {
  it("takes every name from CMakeLists.txt, set in one place", () => {
    for (const name of new Set([...manifest.matchAll(/@([A-Z_]+)@/g)].map((m) => m[1]!))) {
      if (name === "AP_MSIX_VERSION") continue; // made from the project's version, below
      expect(cached(name), name).toBeDefined();
    }
    expect(manifest).toMatch(/Executable="@AP_PRODUCT_NAME@\.exe"/);
    expect(manifest).toMatch(/<DisplayName>@AP_DISPLAY_NAME@<\/DisplayName>/);
    expect(manifest).toMatch(/<PublisherDisplayName>@AP_COMPANY_NAME@<\/PublisherDisplayName>/);
  });

  it("is filled in by CMake, with the version as the Store wants it (four numbers, the last 0)", () => {
    expect(appCmake).toMatch(/configure_file\("\$\{PROJECT_SOURCE_DIR\}\/packaging\/msix\/AppxManifest\.xml\.in"/);
    expect(appCmake).toMatch(
      /set\(AP_MSIX_VERSION "\$\{PROJECT_VERSION_MAJOR\}\.\$\{PROJECT_VERSION_MINOR\}\.\$\{PROJECT_VERSION_PATCH\}\.0"\)/,
    );
  });

  it("has the identity Partner Center gave when the name was reserved (task 8-5e)", () => {
    // Product identity, Partner Center, 2026-11-22. The Store refuses a package whose identity differs.
    expect(cached("AP_MSIX_IDENTITY_NAME")).toBe("DEZIGN.DEZIGNAUDIOPLAYER");
    expect(cached("AP_MSIX_PUBLISHER")).toBe("CN=955E5C32-0C31-4594-88D0-3D3367E883F3");
    expect(cached("AP_COMPANY_NAME")).toBe("DE:ZIGN"); // Package/Properties/PublisherDisplayName
  });

  it("is a desktop app with full trust, as the app is", () => {
    expect(manifest).toMatch(/EntryPoint="Windows\.FullTrustApplication"/);
    expect(manifest).toMatch(/<rescap:Capability Name="runFullTrust" \/>/);
  });

  it("names only pictures the package has (tools/make_icon.py)", () => {
    const named = [...manifest.matchAll(/Assets\\([A-Za-z0-9]+)\.png/g)].map((m) => m[1]!);
    expect(named.length).toBeGreaterThan(0);
    for (const n of named) expect(assets, n).toContain(`${n}.scale-100.png`);
  });
});

describe("the package's words (Japanese first, then English)", () => {
  it("lists Japanese as the default language, and English", () => {
    const langs = [...manifest.matchAll(/<Resource Language="([^"]+)"/g)].map((m) => m[1]);
    expect(langs).toEqual(["ja-JP", "en-US"]);
  });

  it("has the same words in both languages, and every one the manifest asks for", () => {
    const ja = words(reswJa);
    const en = words(reswEn);
    expect(Object.keys(en).sort()).toEqual(Object.keys(ja).sort());
    for (const m of manifest.matchAll(/ms-resource:([A-Za-z0-9_]+)/g)) {
      expect(ja[m[1]!], m[1]).toBeTruthy();
      expect(en[m[1]!], m[1]).toBeTruthy();
    }
  });

  it("says what the app does, not that it sounds better (CLAUDE.md chapter 1)", () => {
    const all = [...Object.values(words(reswJa)), ...Object.values(words(reswEn))].join(" ");
    expect(all).not.toMatch(/良くな|高音質|better|improv/i);
  });
});

describe("the build script", () => {
  it("keeps one index of the words and pictures (a single package does not read split ones)", () => {
    expect(script).toMatch(/SelectNodes\("\/\/packaging"\)/);
    expect(script).toMatch(/makepri new \/pr \$stage/);
  });

  it("puts the test certificate's subject in place of the Publisher only when signing at hand", () => {
    expect(script).toMatch(/if \(\$Thumbprint\) \{[\s\S]*?\$manifest\.Package\.Identity\.Publisher = \$cert\.Subject/);
    expect(script).toMatch(/if \(\$kind -eq "test"\) \{\s*& \$signtool sign/);
  });

  it("starts with a byte-order mark, so Windows PowerShell 5.1 reads its Japanese as UTF-8", () => {
    expect(script.charCodeAt(0)).toBe(0xfeff);
  });
});
