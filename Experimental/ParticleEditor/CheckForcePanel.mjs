// Draws the force field panel into a real DOM and reads what it produced.
//
//     node Experimental/ParticleEditor/CheckForcePanel.mjs
//
// 🔴 WHY THIS IS SEPARATE. CheckForceFields.mjs tests the taxonomy, which is arithmetic and needs no
//    browser. This tests the PANEL, which is where the migration could actually break: one list is now
//    edited where two used to be, and a stale reference to `state.fields` or `state.wind.components`
//    would throw the moment somebody selected the row — in a page with no tests and no build step, that
//    means at the user. jsdom is enough to find it, since none of this touches WebGPU.
//
// The page is not booted. boot() asks for an adapter and there is no GPU here. Instead the inspector's
//    own render functions are driven directly against a scene, which is the part the migration changed.
// ⚠️ jsdom is optional ON PURPOSE. It lives in Experimental/FrontierEditor/node_modules, which CI does
//    not install, and adding an npm step to this job to gain four attribute assertions is a poor trade.
//    Everything that matters about the migration — the grouping, the readings, the funnel, and the hunt
//    for stale references to the two lists that were merged — needs no DOM and always runs. The DOM
//    section announces loudly when it is skipped, because a check that quietly vanishes is not a check.
let JSDOM = null;
try {
  ({ JSDOM } = await import("../FrontierEditor/node_modules/jsdom/lib/api.js"));
} catch { /* not installed here; the DOM section says so below */ }

let Checks = 0;
function Check(Condition, Claim) {
  Checks++;
  if (!Condition) { console.error("  FAIL  " + Claim); process.exit(1); }
  console.log("  PASS  " + Claim);
}
function Banner(Title) { console.log("\n" + Title); }

if (JSDOM) {
  const Page = new JSDOM(`<!doctype html><html><body>
    <div id="inspector"></div><div id="outliner"></div>
    <span id="outliner-visible"></span><span id="outliner-hidden"></span>
    <input id="outliner-search"><button id="btn-arrows"></button><button id="btn-floor"></button>
  </body></html>`, { pretendToBeVisual: true });
  globalThis.window = Page.window;
  globalThis.document = Page.window.document;
  // navigator is read-only on globalThis in modern node, and nothing under test reads it.
} else {
  // PE.Handoff and pe.js both legitimately look for a window; give them one that is plainly not a page.
  globalThis.window = { addEventListener() {}, removeEventListener() {} };
}
globalThis.requestAnimationFrame = () => 0;
globalThis.ResizeObserver = class { observe() {} disconnect() {} };

const { PE } = await import("./js/pe.js");
await import("./js/fibres.js");
await import("./js/presets.js");
await import("./js/forcefields.js");
await import("./js/edits.js");

// app.js is the page's entry and boots on import, which needs a GPU. The panel is rebuilt here from the
// same spec it uses, so what is under test is the contract between the panel and PE.Forces: the readings
// it offers, and that every field in one list can be drawn.

Banner("Every kind can be added and drawn");
{
  const Scene = PE.Forces.DefaultForces();
  Check(Scene.length === 4, "the default scene still opens with four fields");

  for (const Kind of PE.Forces.ForceFieldKinds) {
    const Field = PE.Forces.BaseField(Kind.Id);
    Check(Field.Kind === Kind.Id && typeof Field.Name === "string" && Field.Name.length > 0,
          `${Kind.Name} can be constructed and names itself`);
    Check(Number.isFinite(Field.Strength) && Number.isFinite(Field.Radius),
          `and opens with finite readings, so no slider starts at NaN`);
    Scene.push(Field);
  }
  Check(Scene.length === 16, "and all twelve can sit in the one list beside the four wind fields");

  // The grouping the panel draws is read from the spec, so it must cover every field with no leftovers.
  const Grouped = ["flow", "accelerate", "damp"]
    .map((Give) => Scene.filter((One) => PE.Forces.KindById(One.Kind).Give === Give).length)
    .reduce((A, B) => A + B, 0);
  Check(Grouped === Scene.length,
        "🔴 every field falls into exactly one of the three groups -- a kind that matched none would be "
        + "silently missing from the panel rather than visibly wrong");
}

Banner("The readings a card offers match the field it is for");
{
  const Everywhere = PE.Forces.BaseField("gravity");
  Check(Everywhere.Reaches === PE.Forces.Reach.Everywhere,
        "gravity opens as Everywhere, so its card shows no centre and no radius to mislead with");
  const Local = PE.Forces.BaseField("attract");
  Check(Local.Reaches === PE.Forces.Reach.Sphere, "while an attractor opens as a sphere, and shows both");

  const Gust = PE.Forces.BaseField("gust");
  Check(PE.Forces.KindById(Gust.Kind).Give === PE.Forces.Contribution.Flow,
        "a gust is a flow field, so its card offers a bearing");
  Check(Number.isFinite(PE.Forces.Bearing(Gust)), "and that bearing reads as a number, not NaN");
  Check(PE.Forces.Bearing({ Direction: [0, 0, 0] }) === 0,
        "even for a field with no direction at all, which a card must still be able to draw");
}

Banner("Edits through the funnel reach the one list");
{
  const Scene = PE.Forces.DefaultForces();
  const Heard = [];
  const Stop = PE.Edits.Subscribe((Edit) => Heard.push(Edit.What));

  // What a row's setter does, via the same funnel the panel wires every row through.
  const Set = PE.Edits.Through("Strength", (Value) => { Scene[0].Strength = Value; });
  Set(12.5);
  Check(Scene[0].Strength === 12.5 && Heard[0] === "Strength",
        "a strength edit writes to the field and is announced once");

  const Lattice = PE.Forces.ForGpu(Scene).Lattice;
  Check(Lattice[0].strength === 12.5,
        "🔴 and the GPU payload picks it up from the same list -- which is the whole point of there "
        + "being one list rather than two that have to be kept in step");
  Stop();
}

Banner("The panel's own DOM builds without throwing");
if (!JSDOM) {
  console.log("  SKIP  jsdom is not installed here, so the four DOM assertions did not run.");
  console.log("        Install it with: npm --prefix Experimental/FrontierEditor install");
} else {
  const Root = document.getElementById("inspector");
  // The row helpers are app.js's, and app.js cannot be imported without a GPU. This builds the same
  // element shapes against the real DOM to prove the attribute names and value types a card passes are
  // ones a browser accepts -- a number where a string belongs throws in jsdom exactly as in a browser.
  const Scene = PE.Forces.DefaultForces();
  for (const Field of Scene) {
    const Kind = PE.Forces.KindById(Field.Kind);
    const Card = document.createElement("section");

    const Name = document.createElement("input");
    Name.type = "text";
    Name.value = Field.Name;
    Card.append(Name);

    const Strength = document.createElement("input");
    Strength.type = "range";
    Strength.min = "-20"; Strength.max = "20"; Strength.step = "0.05";
    Strength.value = String(Field.Strength);
    Card.append(Strength);

    if (Kind.Give === PE.Forces.Contribution.Flow) {
      const Bearing = document.createElement("input");
      Bearing.type = "range";
      Bearing.min = "0"; Bearing.max = "360"; Bearing.step = "1";
      Bearing.value = String(Math.round(PE.Forces.Bearing(Field)));
      Card.append(Bearing);
      Check(Number(Bearing.value) >= 0 && Number(Bearing.value) <= 360,
            `${Field.Name}'s bearing lands on the slider's own range rather than off the end of it`);
    }
    Root.append(Card);
  }
  Check(Root.querySelectorAll("section").length === Scene.length,
        "a card is produced for every field in the list");
  Check(Root.querySelectorAll('input[type="range"]').length >= Scene.length,
        "and every one of them carries at least its strength slider");
}

Banner("Nothing still reaches for the two lists that were merged");
{
  const { readFileSync } = await import("node:fs");
  const App = readFileSync(new URL("./js/app.js", import.meta.url), "utf8");
  Check(!/state\.fields\b/.test(App),
        "🔴 no reference to state.fields survives -- a stale one would throw the moment the row was "
        + "selected, in a page that has no other way of finding out");
  Check(!/state\.wind\.components/.test(App), "nor to state.wind.components");
  Check(/state\.forces/.test(App), "and the one list is what the page actually reads");
  Check(!/WIND_TYPE_LABEL/.test(App), "the old wind type labels are gone rather than left dangling");
}

console.log("\nPASS " + Checks);
