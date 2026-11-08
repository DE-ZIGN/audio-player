import { defineConfig } from "vite";

// The built UI is served by the app's resource provider from <exe dir>/ui,
// so asset URLs must be relative.
export default defineConfig({
  base: "./",
  server: {
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
