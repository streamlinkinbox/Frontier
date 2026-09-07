import { defineConfig } from "@playwright/test";
const software = process.env.FRONTIER_SOFTWARE_GPU === "1";
export const launchOptions = {
  executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH || undefined,
  args: software
    ? [
        "--no-sandbox",
        "--enable-unsafe-webgpu",
        "--ignore-gpu-blocklist",
        "--use-angle=swiftshader",
        "--use-vulkan=swiftshader",
        "--enable-features=Vulkan",
        "--disable-vulkan-surface",
      ]
    : ["--enable-unsafe-webgpu"],
};
export default defineConfig({
  testDir: "./tests",
  testIgnore: "**/pages.spec.ts",
  timeout: 180_000,
  expect: { timeout: 60_000 },
  workers: 1,
  use: {
    baseURL: "http://localhost:5173",
    viewport: { width: 1440, height: 900 },
    launchOptions,
    screenshot: "only-on-failure",
  },
  webServer: {
    command: "npm run dev -- --port 5173",
    url: "http://localhost:5173",
    reuseExistingServer: true,
    timeout: 120_000,
  },
});
