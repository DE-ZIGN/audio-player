import { describe, expect, it } from "vitest";
import appCmake from "../../app/CMakeLists.txt?raw";
import versionRc from "../../app/resources/version.rc.in?raw";
import iconSvg from "../../app/resources/icon/icon.svg?raw";
import ico from "../../app/resources/icon/icon.ico?inline";
import tool from "../../tools/make_icon.py?raw";

// The app's icon (task 8-3b): the product site's vacuum tube, with thicker
// lines (the user, 2026-11-21). tools/make_icon.py draws it and writes the
// .ico and the MSIX pictures; these tests check what it wrote is what the
// build and the package expect.

const bytes = (dataUrl: string) => Uint8Array.from(atob(dataUrl.split(",")[1] ?? ""), (c) => c.charCodeAt(0));
const msixPngs = import.meta.glob("../../packaging/msix/Assets/*.png", { query: "?inline", import: "default", eager: true }) as Record<string, string>;

/** Width and height of a PNG, from its IHDR. */
function pngSize(data: Uint8Array): [number, number] {
  const v = new DataView(data.buffer);
  return [v.getUint32(16), v.getUint32(20)];
}

describe("the executable's icon", () => {
  it("is compiled into the exe from the one .ico", () => {
    expect(versionRc).toMatch(/^IDI_ICON1 ICON "@AP_ICON_FILE@"$/m);
    expect(appCmake).toMatch(/set\(AP_ICON_FILE "\$\{CMAKE_CURRENT_SOURCE_DIR\}\/resources\/icon\/icon\.ico"\)/);
  });

  it("holds every size Windows asks for, 16 to 256 px", () => {
    const data = bytes(ico);
    const v = new DataView(data.buffer);
    expect([v.getUint16(0, true), v.getUint16(2, true)]).toEqual([0, 1]); // an icon file
    const count = v.getUint16(4, true);
    const sizes = Array.from({ length: count }, (_, i) => data[6 + 16 * i] || 256).sort((a, b) => a - b);
    expect(sizes).toEqual([16, 20, 24, 32, 40, 48, 64, 256]);
  });

  it("keeps 256 px as PNG and the smaller sizes as bitmaps, which every reader takes", () => {
    const data = bytes(ico);
    const v = new DataView(data.buffer);
    for (let i = 0; i < v.getUint16(4, true); i++) {
      const size = data[6 + 16 * i] || 256;
      const at = v.getUint32(6 + 16 * i + 12, true);
      const isPng = data[at] === 0x89 && data[at + 1] === 0x50;
      expect(isPng, `${size} px`).toBe(size === 256);
      if (!isPng) expect([v.getUint32(at, true), v.getInt32(at + 4, true), v.getUint16(at + 14, true)], `${size} px`).toEqual([40, size, 32]);
    }
  });

  it("is the site's tube in amber on the dark plate", () => {
    expect(iconSvg).toMatch(/fill="#0c0907"/);
    expect(iconSvg).toMatch(/stroke="#e8a24a"/);
    for (const d of ["M5 21V9a6 6 0 0 1 12 0v12", "M3 21h16", "M9 17v-5l2-2 2 2v5"]) expect(iconSvg).toContain(`d="${d}`);
  });
});

describe("the MSIX package's pictures (for task 8-3c)", () => {
  const byName = Object.fromEntries(Object.entries(msixPngs).map(([path, url]) => [path.split("/").pop()!, bytes(url)]));
  const expectSize = (name: string, size: number) => {
    expect(byName[name], name).toBeDefined();
    expect(pngSize(byName[name]!), name).toEqual([size, size]);
  };

  it("has each logo at each scale", () => {
    for (const [pct, k] of [[100, 1], [125, 1.25], [150, 1.5], [200, 2], [400, 4]] as const) {
      expectSize(`Square44x44Logo.scale-${pct}.png`, Math.round(44 * k));
      expectSize(`Square150x150Logo.scale-${pct}.png`, Math.round(150 * k));
      expectSize(`StoreLogo.scale-${pct}.png`, Math.round(50 * k));
    }
  });

  it("has the taskbar's and the app list's sizes, plated and unplated", () => {
    for (const t of [16, 20, 24, 30, 32, 36, 40, 48, 60, 64, 72, 80, 96, 256]) {
      for (const form of ["", "_altform-unplated", "_altform-lightunplated"]) expectSize(`Square44x44Logo.targetsize-${t}${form}.png`, t);
    }
  });

  it("are all written by the tool, nothing else", () => {
    expect(Object.keys(byName).length).toBe(3 * 5 + 14 * 3);
    expect(tool).toMatch(/MSIX_ASSETS = ROOT \/ "packaging" \/ "msix" \/ "Assets"/);
  });
});
