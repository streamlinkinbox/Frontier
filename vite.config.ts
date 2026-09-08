import { defineConfig, type Plugin } from "vite";
import { fileURLToPath } from "node:url";
import react from "@vitejs/plugin-react";
export default defineConfig(({ mode }) => ({
  plugins: [
    react(),
    ...(mode === "material-lab"
      ? [
          {
            name: "material-editor-preview",
            configureServer(server) {
              server.middlewares.use((req, _res, next) => {
                if (req.url === "/" || req.url?.startsWith("/?"))
                  req.url = "/material-editor.html" + req.url.slice(1);
                next();
              });
            },
          } satisfies Plugin,
        ]
      : []),
  ],
  base: "/",
  build: {
    rollupOptions: {
      input: {
        main: fileURLToPath(new URL("./index.html", import.meta.url)),
        "material-editor": fileURLToPath(
          new URL("./material-editor.html", import.meta.url),
        ),
      },
    },
  },
  server: { host: "0.0.0.0", allowedHosts: [".e2b.app", "localhost"] },
  preview: { host: "0.0.0.0", allowedHosts: [".e2b.app", "localhost"] },
}));
