// Checks the Particle Editor's scene handoff — the only part of the page a host depends on.
//
//     node Experimental/ParticleEditor/CheckHandoff.mjs
//
// The page is ES modules now, so this imports the two under test directly rather than evaluating them
// into a hand-built window. It still has to supply a `window`, because Install() legitimately reaches for
// one -- that is what it is for.
import { PE } from "./js/pe.js";
import { readFileSync, existsSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const Here = dirname(fileURLToPath(import.meta.url));

let Checks = 0;
function Check(Condition, Claim) {
  Checks++;
  if (!Condition) {
    console.error("  FAIL  " + Claim);
    process.exit(1);
  }
  console.log("  PASS  " + Claim);
}
function Banner(Title) {
  console.log("\n" + Title);
}

// ── Load handoff.js into a window of our own ────────────────────────────────────────────────────────
const Posted = [];
const Listeners = [];
const Window = {
  addEventListener: (Kind, Handler) => Kind === "message" && Listeners.push(Handler),
  removeEventListener: () => {},
  setInterval: () => 0,
  clearInterval: () => {},
  parent: null,
};
Window.window = Window;
globalThis.window = Window;
await import("./js/edits.js");
await import("./js/handoff.js");
const Handoff = PE.Handoff;
const Edits = PE.Edits;

Banner("Describing what is selected");
{
  const State = {
    selection: { type: "system", id: 2 },
    systems: [
      { id: 1, presetId: "sparks", name: "Sparks", p: { rate: 100 } },
      { id: 2, presetId: "embers", name: "Hearth embers", p: { rate: 40, origin: [0, 1, 0], loop: true, blend: "add" } },
    ],
  };
  const Said = Handoff.Describe(State);
  Check(Said.Frontier === "particle-system-changed", "a description is tagged the way GasPanel.jsx tags its own");
  Check(Said.Preset === "embers" && Said.Name === "Hearth embers",
        "and it names the preset and the system that is actually selected, not the first one");
  Check(Said.Settings.rate === 40 && Said.Settings.loop === true && Said.Settings.blend === "add",
        "numbers, booleans and strings all cross");
  Check(Array.isArray(Said.Settings.origin) && Said.Settings.origin !== State.systems[1].p.origin,
        "an array crosses as a copy, so the host cannot reach back into the page's live parameters");

  Check(Handoff.Describe({ selection: { type: "wind" }, systems: [] }) === null,
        "with the wind selected there is no system to describe, and it says null rather than inventing one");
  Check(Handoff.Describe({ selection: { type: "system", id: 9 }, systems: [] }) === null,
        "and a selection pointing at a system that is gone describes nothing");
}

Banner("Admitting what a host sends");
{
  const Parameters = { rate: 40, origin: [0, 1, 0], loop: true, blend: "add", capacity: 1024 };
  const Taken = Handoff.Admit(Parameters, { rate: 90, origin: [1, 2, 3], loop: false });
  Check(Taken === 3, "three known fields are taken");
  Check(Parameters.rate === 90 && Parameters.loop === false, "and written in place");
  Check(Parameters.origin[0] === 1 && Parameters.origin[2] === 3, "including the vector");

  Check(Handoff.Admit(Parameters, { nothingAtAll: 5 }) === 0,
        "a field the system does not have is ignored -- a message is untrusted input, not a merge source");
  Check(Parameters.nothingAtAll === undefined, "and it is not added either");
  Check(Handoff.Admit(Parameters, { rate: "fast" }) === 0 && Parameters.rate === 90,
        "a number sent as a string is refused rather than coerced to NaN by the uniform packer");
  Check(Handoff.Admit(Parameters, { rate: Number.NaN }) === 0 && Parameters.rate === 90,
        "and so is a NaN, which is the one value that would silently blank a whole draw");
  Check(Handoff.Admit(Parameters, { origin: [1, 2] }) === 0,
        "a vector of the wrong length is refused; a three-component origin is not two");
  Check(Handoff.Admit(Parameters, { loop: 1 }) === 0,
        "and a boolean sent as 1 is refused, because 1 is not false and guessing which was meant is worse");
  Check(Handoff.Admit(null, { rate: 1 }) === 0 && Handoff.Admit({}, null) === 0,
        "neither side being absent throws");
}

Banner("Only plain values cross");
{
  const Out = Handoff.Writable({ rate: 1, name: "x", on: true, origin: [1, 2, 3], gpu: { buffer: 1 },
                                 tick: () => 0, stops: [{ col: [1, 1, 1] }] });
  Check(Out.rate === 1 && Out.name === "x" && Out.on === true, "scalars cross");
  Check(Array.isArray(Out.origin), "and numeric vectors");
  Check(Out.gpu === undefined && Out.tick === undefined,
        "a GPU handle and a function do not -- postMessage would throw on the function and clone "
        + "nonsense out of the handle");
  Check(Out.stops === undefined,
        "and neither does an array of objects, which is the one case where a shallow copy would have "
        + "aliased the page's own state into the message");
}

Banner("Noticing a change");
{
  const A = { Frontier: "particle-system-changed", Preset: "p", Name: "n", Settings: { rate: 1 } };
  const B = { Frontier: "particle-system-changed", Preset: "p", Name: "n", Settings: { rate: 1 } };
  const C = { Frontier: "particle-system-changed", Preset: "p", Name: "n", Settings: { rate: 2 } };
  Check(Handoff.Same(A, B), "two identical descriptions are the same scene");
  Check(!Handoff.Same(A, C), "one moved slider is not");
  Check(Handoff.Same(null, null) && !Handoff.Same(A, null), "and null is only the same as null");
}

Banner("One place an edit passes through");
{
  const Heard = [];
  const Stop = Edits.Subscribe((Edit) => Heard.push(Edit));

  const System = { id: 1, p: { rate: 10 } };
  Edits.For(System);
  Check(Edits.Announce("Rate", 42) === 1, "an announcement reaches the one watcher listening");
  Check(Heard[0].What === "Rate" && Heard[0].Value === 42 && Heard[0].System === System,
        "carrying what was edited, to what, and on which system");

  // The whole point: a row helper's setter is wrapped once and every row built by it is observable.
  let Written = 0;
  const Set = Edits.Through("Smoke", (Value) => { Written = Value; });
  Set(7);
  Check(Written === 7 && Heard.length === 2 && Heard[1].What === "Smoke",
        "a setter put Through the funnel still writes, and is heard -- the row does not have to remember");

  Check(Edits.Quietly(() => Edits.Announce("Rate", 1)) === 0,
        "\U0001f534 a programmatic edit announces nothing, so admitting a host's message cannot echo back "
        + "to it as an edit and loop");
  Check(Heard.length === 2, "and nothing was heard during it");

  // A watcher behind a throwing one must still be reached, and the row must still have written.
  let Behind = 0;
  const StopNoisy = Edits.Subscribe(() => { throw new Error("watcher trouble"); });
  const StopBehind = Edits.Subscribe(() => { Behind++; });
  Check(Edits.Announce("Rate", 2) === 2 && Behind === 1,
        "one watcher throwing is logged and stepped over; the watchers behind it, and the drag, carry on");

  StopNoisy();
  StopBehind();
  Stop();
  const Before = Heard.length;
  Edits.Announce("Rate", 3);
  Check(Heard.length === Before && Edits.Watchers.length === 0,
        "unsubscribing is honoured, and leaves nothing behind -- a watcher that cannot leave is a leak");
  Edits.For(null);
}

Banner("Standing alone");
{
  Window.parent = Window;   // not framed
  const Watching = Edits.Watchers.length;
  const Stop = Handoff.Install({ selection: { type: "wind" }, systems: [] }, {});
  Check(typeof Stop === "function" && Listeners.length === 0,
        "with no host above it the page installs nothing at all -- no listener, no interval, no posting "
        + "into its own window");
  Check(Edits.Watchers.length === Watching, "and it does not subscribe to edits nobody asked it to report");
}

Banner("Framed, it reports an edit instead of waiting for a poll");
{
  const Sent = [];
  const Host = { postMessage: (Message) => Sent.push(Message) };
  Window.parent = Host;
  let Fire = null;
  Window.setTimeout = (Work) => { Fire = Work; return 1; };
  Window.clearTimeout = () => {};
  Window.setInterval = () => 2;

  const State = { selection: { type: "system", id: 1 },
                  systems: [{ id: 1, presetId: "embers", name: "Embers", p: { rate: 10 } }] };
  const Stop = Handoff.Install(State, {});
  Check(Sent.length === 1 && Sent[0].Frontier === "particle-editor-ready",
        "it announces itself to the host on boot, so the host knows the page is alive");
  Check(Edits.Watchers.length > 0, "and it subscribes to the funnel rather than polling for a change");

  State.systems[0].p.rate = 55;
  Edits.Announce("Emission Rate", 55);
  Check(Fire !== null, "an edit schedules a post rather than sending one per input event of a drag");
  Fire();
  Check(Sent.length === 2 && Sent[1].Settings.rate === 55,
        "and when it settles the host is told, carrying the new reading");

  Fire = null;
  Edits.Announce("Emission Rate", 55);
  if (Fire) Fire();
  Check(Sent.length === 2, "an edit that changed nothing is not reported -- the host hears scenes, not events");

  Stop();
  const After = Edits.Watchers.length;
  Edits.Announce("Emission Rate", 9);
  Check(Edits.Watchers.length === After && After === 0,
        "and closing the page lets go of the funnel");
  Window.parent = null;
}

Banner("How the page loads");
{
  const Page = readFileSync(join(Here, "index.html"), "utf8");
  const Modules = ["pe", "fibres", "presets", "shaders", "engine", "lightning", "forcefields",
                   "edits", "handoff", "app"];
  const Sources = Object.fromEntries(
    Modules.map((Name) => [Name, readFileSync(join(Here, "js", Name + ".js"), "utf8")]));

  Check(!/\?v=\d/.test(Page.replace(/<!--[\s\S]*?-->/g, "")),
        "no hand-maintained cache-busting query strings are left in the page -- forgetting to bump one "
        + "was how a stale script got served");
  Check((Page.match(/<script/g) || []).length === 1 && Page.includes('type="module" src="js/app.js"'),
        "one entry module replaces the eight ordered script tags");

  for (const Name of Modules) {
    Check(!Sources[Name].includes("(function () {"),
          `js/${Name}.js is a module, not an IIFE over a global`);
  }
  Check(Modules.filter((Name) => Name !== "pe" && Name !== "app")
               .every((Name) => Sources[Name].includes('import { PE } from "./pe.js"')),
        "and every one of them imports the namespace rather than reaching for window");

  // Dependency order used to live in the HTML, where nothing could check it. Now it is in app.js.
  const Imported = [...Sources.app.matchAll(/import "\.\/(\w+)\.js"/g)].map((One) => One[1]);
  Check(Imported.join(",") === "fibres,presets,shaders,engine,lightning,forcefields,edits,handoff",
        "the entry imports the rest in the dependency order the script tags used to imply");
  Check(Imported.indexOf("fibres") < Imported.indexOf("presets"),
        "fibres before presets, because presets reads PE.hexLinear while it is being evaluated");
  Check(Sources.app.indexOf('import "./engine.js"') < Sources.app.indexOf("PE.WIND"),
        "and engine before this file's own top-level read of PE.WIND");

  for (const Specifier of [...Sources.app.matchAll(/from "\.\/([\w./]+)"|import "\.\/([\w./]+)"/g)]) {
    const Target = Specifier[1] || Specifier[2];
    Check(existsSync(join(Here, "js", Target)),
          `the entry's import of ./${Target} resolves to a file that exists`);
  }

  Check(Sources.app.includes('document.readyState === "loading"'),
        "\u{1f534} and it does not wait on DOMContentLoaded unconditionally -- a module script is deferred, "
        + "so that event has already fired by the time it runs and the page would never boot");
}

console.log("\nPASS " + Checks);
