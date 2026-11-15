/// <reference types="vitest/config" />
import { defineConfig, type Plugin } from "vite";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { safeMockupName } from "./src/dev/mockupParams";

// The concept images (task 6-8) live in the work folder outside the
// repository: <work folder>/imagegen/accepted. MOCKUP_DIR overrides it.
const MOCKUP_DIR = process.env.MOCKUP_DIR ?? fileURLToPath(new URL("../../imagegen/accepted", import.meta.url));

// Development server only: /__mockups/ lists the images, /__mockups/<name>
// sends one, for the overlay (src/dev/mockupOverlay.ts, ?mockup=<name>).
function mockups(): Plugin {
  return {
    name: "mockups",
    apply: "serve",
    configureServer(server) {
      server.middlewares.use("/__mockups", async (req, res) => {
        try {
          const rel = decodeURIComponent((req.url ?? "/").split("?")[0]).replace(/^\//, "");
          if (rel === "") {
            const names = (await readdir(MOCKUP_DIR)).filter((n) => safeMockupName(n) === n).sort();
            res.setHeader("Content-Type", "application/json");
            res.end(JSON.stringify(names));
            return;
          }
          if (safeMockupName(rel) !== rel) throw new Error("bad name");
          const data = await readFile(path.join(MOCKUP_DIR, rel));
          res.setHeader("Content-Type", /\.png$/i.test(rel) ? "image/png" : "image/jpeg");
          res.setHeader("Cache-Control", "no-cache");
          res.end(data);
        } catch {
          res.statusCode = 404;
          res.end();
        }
      });
    },
  };
}

// The built UI is served by the app's resource provider from <exe dir>/ui,
// so asset URLs must be relative.
export default defineConfig({
  base: "./",
  plugins: [mockups()],
  server: {
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
  // Tests read style.css as text (src/fonts.test.ts); vitest leaves CSS out otherwise.
  test: {
    css: true,
  },
});
