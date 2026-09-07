import { defineConfig } from "@playwright/test";
import { launchOptions } from "./playwright.config";
export default defineConfig({
  testDir: "./tests",
  testMatch: "**/pages.spec.ts",
  timeout: 180_000,
  expect: { timeout: 60_000 },
  workers: 1,
  use: {
    viewport: { width: 1280, height: 800 },
    launchOptions,
    screenshot: "only-on-failure",
  },
  projects: [
    {
      name: "docs-source",
      use: { baseURL: "http://localhost:4173/Frontier/" },
    },
    {
      name: "root-source-recovery",
      use: { baseURL: "http://localhost:4174/Frontier/" },
    },
  ],
  webServer: [
    {
      command: "node scripts/serve-pages.mjs --port 4173",
      url: "http://localhost:4173/Frontier/",
      reuseExistingServer: false,
    },
    {
      command: "node scripts/serve-pages.mjs --source-root --port 4174",
      url: "http://localhost:4174/Frontier/",
      reuseExistingServer: false,
    },
  ],
});
