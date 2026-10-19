export async function bootStage(message, step) {
  const label = document.getElementById("boot-message");
  const progress = document.getElementById("boot-progress");
  if (label) label.textContent = message;
  if (progress) progress.value = step;
  window.frontierStartup?.phases.push({ message, step, at: performance.now() });
  // Give the browser a paint between the CPU-heavy migration and geometry phases.
  await new Promise((resolve) => setTimeout(resolve, 40));
}
