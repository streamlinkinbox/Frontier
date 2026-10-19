const started = performance.now();
window.frontierStartup = { phases: [], failed: false, ready: false };
const screen = document.getElementById("boot-screen");
const label = document.getElementById("boot-message");
const elapsed = document.getElementById("boot-elapsed");
document
  .getElementById("boot-retry")
  .addEventListener("click", () => location.reload());
document.getElementById("boot-backup").addEventListener("click", () => {
  try {
    const saved = localStorage.getItem("frontier-project-v1");
    if (!saved) {
      label.textContent = "No saved map is stored in this browser yet.";
      return;
    }
    const url = URL.createObjectURL(
      new Blob([saved], { type: "application/json" }),
    );
    const a = document.createElement("a");
    a.href = url;
    a.download = "frontier-recovery-backup.json";
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  } catch {
    label.textContent =
      "Browser storage is unavailable. Do not clear your browser data.";
  }
});
const timer = setInterval(() => {
  elapsed.textContent = `${Math.floor((performance.now() - started) / 1000)} seconds · large maps take longer on the first open`;
}, 1000);
try {
  await import("./main.js");
  window.frontierStartup.ready = true;
  screen.remove();
} catch (error) {
  window.frontierStartup.failed = true;
  document.getElementById("boot-title").textContent =
    "The editor could not start";
  label.textContent =
    "The app failed to load. Retry below; you can download your saved map before trying again. Do not clear browser storage.";
  const detail = document.getElementById("boot-error");
  detail.textContent = String(error?.message || error).slice(0, 700);
  detail.hidden = false;
  document.getElementById("boot-progress").hidden = true;
  console.error("Frontier startup failed", error);
} finally {
  clearInterval(timer);
}
