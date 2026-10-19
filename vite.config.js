import { defineConfig } from "vite";

// Arena uses both its current arena.site proxy and legacy e2b previews.
const allowedHosts = [".arena.site", ".e2b.app"];
export default defineConfig({
  server: { host: "0.0.0.0", allowedHosts },
  preview: { host: "0.0.0.0", allowedHosts },
  build: { chunkSizeWarningLimit: 800 },
});
