import assert from "node:assert/strict";
import fs from "node:fs";
const Port = process.argv[2] || 9264,
  Site = process.argv[3] || "http://127.0.0.1:8104";
let Targets = await (await fetch(`http://127.0.0.1:${Port}/json`)).json(),
  Target = Targets.find((T) => T.type === "page" && T.url.startsWith(Site));
if (!Target)
  Target = await (
    await fetch(
      `http://127.0.0.1:${Port}/json/new?${encodeURIComponent(Site)}`,
      { method: "PUT" },
    )
  ).json();
const Socket = new WebSocket(Target.webSocketDebuggerUrl);
await new Promise((R) => Socket.addEventListener("open", R, { once: true }));
let Id = 0;
const Pending = new Map(),
  Errors = [],
  Checks = [];
Socket.addEventListener("message", (E) => {
  const M = JSON.parse(E.data);
  if (M.id) {
    const P = Pending.get(M.id);
    Pending.delete(M.id);
    if (P) M.error ? P[1](M.error) : P[0](M.result);
  }
  if (M.method === "Runtime.exceptionThrown") Errors.push(M.params);
  if (M.method === "Runtime.consoleAPICalled" && M.params.type === "error")
    Errors.push(M.params.args);
});
const Call = (Method, Params = {}) =>
  new Promise((Resolve, Reject) => {
    Pending.set(++Id, [Resolve, Reject]);
    Socket.send(JSON.stringify({ id: Id, method: Method, params: Params }));
  });
const Evaluate = async (Expression) => {
  const R = await Call("Runtime.evaluate", {
    expression: Expression,
    returnByValue: true,
    awaitPromise: true,
  });
  if (R.exceptionDetails) throw Error(JSON.stringify(R.exceptionDetails));
  return R.result.value;
};
const Wait = (Ms) => Evaluate(`new Promise(R=>setTimeout(R,${Ms}))`);
const Click = async (Selector) => {
  await Evaluate(`document.querySelector(${JSON.stringify(Selector)}).click()`);
  await Wait(180);
};
const Tab = async (N) => {
  await Evaluate(
    `document.querySelectorAll('.step-navigation button')[${N}].click()`,
  );
  await Wait(500);
};
const Shot = async (Name) => {
  const R = await Call("Page.captureScreenshot", { format: "png" });
  fs.writeFileSync(
    `/tmp/import-dialogue-${Name}.png`,
    Buffer.from(R.data, "base64"),
  );
};
try {
  await Call("Page.enable");
  await Call("Runtime.enable");
  await Call("Emulation.setDeviceMetricsOverride", {
    width: 1440,
    height: 1000,
    deviceScaleFactor: 1,
    mobile: false,
  });
  await Call("Page.navigate", { url: Site });
  await Evaluate(
    `new Promise((Resolve,Reject)=>{let N=0;const Check=()=>{if(document.querySelector('.geometry-render canvas'))Resolve();else if(++N>150)Reject('Preview timeout');else setTimeout(Check,100);};Check();})`,
  );
  await Wait(1000);
  for (let I = 0; I < 7; I++) {
    await Tab(I);
    const Stats = await Evaluate(
      `(()=>{const D=document.querySelector('.import-dialog'),P=document.querySelectorAll('.inspector-page')[${I}];return {Width:D.clientWidth,Scroll:D.scrollWidth,PageWidth:P.clientWidth,PageScroll:P.scrollWidth,Title:P.querySelector('h2').textContent,Invalid:P.textContent.includes('NaN'),Borders:[...P.querySelectorAll('.import-section')].map(S=>getComputedStyle(S).borderWidth)};})()`,
    );
    assert(!Stats.Invalid);
    assert(Stats.Scroll <= Stats.Width + 1);
    assert(Stats.PageScroll <= Stats.PageWidth + 1);
    assert(Stats.Borders.every((B) => B === "0px"));
    Checks.push(Stats);
    await Shot("page-" + I);
  }

  const Set = async (Label, Value, Type = "input") => {
    await Evaluate(
      `(()=>{const E=document.querySelector(${JSON.stringify(Type + '[aria-label="' + Label + '"]')});Object.getOwnPropertyDescriptor(${Type === "select" ? "HTMLSelectElement" : "HTMLInputElement"}.prototype,'value').set.call(E,${JSON.stringify(Value)});E.dispatchEvent(new Event('input',{bubbles:true}));E.dispatchEvent(new Event('change',{bubbles:true}));})()`,
    );
    await Wait(180);
  };
  const Button = async (Text) => {
    await Evaluate(
      `[...document.querySelectorAll('.import-dialog button')].find(B=>B.textContent.trim()===${JSON.stringify(Text)}).click()`,
    );
    await Wait(200);
  };
  const Poll = (Expression) =>
    Evaluate(
      `new Promise((Resolve,Reject)=>{let N=0;function Check(){if(${Expression})Resolve(true);else if(++N>160)Reject('Condition timeout: '+${JSON.stringify(Expression)});else setTimeout(Check,100);}Check();})`,
    );
  const Inject = async (Name, Text, Type = "") => {
    await Evaluate(
      `(()=>{const F=new File([${JSON.stringify(Text)}],${JSON.stringify(Name)},{type:${JSON.stringify(Type)}}),D=new DataTransfer();D.items.add(F);const E=document.querySelector('input[aria-label="Choose source files"]');E.files=D.files;E.dispatchEvent(new Event('change',{bubbles:true}));})()`,
    );
    await Wait(600);
  };
  await Tab(0);
  await Set("Import name", "Housing_Review");
  assert.equal(
    await Evaluate(`document.querySelector('.source-ident h2').textContent`),
    "Housing_Review",
  );
  await Tab(1);
  const Before = await Evaluate(
    `document.querySelector('.preview-statistics').textContent`,
  );
  await Set("Scale X", 1.5);
  assert.notEqual(
    await Evaluate(`document.querySelector('.preview-statistics').textContent`),
    Before,
  );
  assert.equal(
    await Evaluate(
      `document.querySelector('input[aria-label="Scale Y"]').value`,
    ),
    "1.5",
  );
  await Set("Normals", "Flat", "select");
  await Set("Normals", "Source", "select");
  await Set("Pivot placement", "Custom", "select");
  assert(
    await Evaluate(`!!document.querySelector('input[aria-label="Pivot X"]')`),
  );
  await Set("Pivot placement", "Bottom", "select");
  await Set("Rotation Z", 25);
  await Shot("geometry");
  Checks.push(
    "XYZ scaling, linked axes, normals, rotation and pivot edit the geometry preview",
  );
  await Tab(2);
  const UvBefore = await Evaluate(
    `document.querySelector('.uv-preview canvas').toDataURL()`,
  );
  await Set("UV scale", 2);
  assert.notEqual(
    await Evaluate(`document.querySelector('.uv-preview canvas').toDataURL()`),
    UvBefore,
  );
  await Set("UV scale", 1);
  Checks.push("UV transforms redraw actual UV0 coordinates");
  await Tab(3);
  await Click(".bake-button");
  await Poll(`document.querySelectorAll('.lod-table>button').length===4`);
  const Levels = await Evaluate(
    `[...document.querySelectorAll('.lod-table>button')].map(B=>({Triangles:+B.querySelector('b').textContent.replaceAll(',',''),Clusters:+B.children[2].textContent}))`,
  );
  assert(Levels[3].Triangles < Levels[0].Triangles);
  assert(Levels.every((L) => L.Clusters === Math.ceil(L.Triangles / 128)));
  await Click(".lod-table>button:nth-of-type(3)");
  await Shot("baked-clusters");
  await Set("Cluster capacity", "64", "select");
  assert(!(await Evaluate(`!!document.querySelector('.lod-table')`)));
  await Click(".bake-button");
  await Poll(`document.querySelectorAll('.lod-table>button').length===4`);
  Checks.push(
    "real cluster baking reduces geometry, sizes groups, selects LODs and invalidates stale settings",
  );
  await Tab(4);
  assert.equal(
    await Evaluate(
      `document.querySelectorAll('.material-slot-list>button').length`,
    ),
    3,
  );
  await Set("Material name", "Titanium · reviewed");
  await Set("Base colour", "#5c8caa");
  await Set("Metallic", 0.62);
  await Evaluate(
    `document.querySelectorAll('.material-slot-list>button')[1].click()`,
  );
  assert(
    await Evaluate(
      `document.querySelectorAll('.material-slot-list>button')[1].getAttribute('aria-pressed')==='true'`,
    ),
  );
  await Evaluate(
    `document.querySelectorAll('.material-slot-list>button')[0].click()`,
  );
  await Click('[aria-label="Choose Base colour texture"]');
  await Evaluate(
    `(()=>{const C=document.createElement('canvas');C.width=C.height=8;C.getContext('2d').fillRect(0,0,4,8);return new Promise(R=>C.toBlob(B=>{const D=new DataTransfer();D.items.add(new File([B],'ReviewTexture.png',{type:'image/png'}));const E=document.querySelector('input[aria-label="Choose material texture"]');E.files=D.files;E.dispatchEvent(new Event('change',{bubbles:true}));R();}));})()`,
  );
  await Poll(`document.querySelector('.texture-inputs img')?.naturalWidth===8`);
  await Click('[aria-label="Clear Base colour texture"]');
  assert(!(await Evaluate(`!!document.querySelector('.texture-inputs img')`)));
  await Shot("materials");
  Checks.push(
    "three real material slots, selection/isolation and live PBR property edits",
  );
  await Tab(5);
  await Click(".sdf-bake-button");
  await Button("Cancel");
  assert(
    !(await Evaluate(`!!document.querySelector('.distance-preview canvas')`)),
  );
  await Click(".sdf-bake-button");
  await Poll(`document.querySelector('.distance-preview canvas')`);
  assert.equal(
    await Evaluate(
      `document.querySelector('.distance-preview canvas').dataset.resolution`,
    ),
    "24",
  );
  const FieldBefore = await Evaluate(
    `document.querySelector('.distance-preview canvas').toDataURL()`,
  );
  await Set("Voxel section", 7);
  assert.notEqual(
    await Evaluate(
      `document.querySelector('.distance-preview canvas').toDataURL()`,
    ),
    FieldBefore,
  );
  await Set("Section plane", "XZ", "select");
  assert.equal(
    await Evaluate(
      `document.querySelector('.distance-preview canvas').dataset.plane`,
    ),
    "XZ",
  );
  await Shot("distance-field");
  await Set("Volume padding", 0.18);
  assert(
    !(await Evaluate(`!!document.querySelector('.distance-preview canvas')`)),
  );
  await Click(".sdf-bake-button");
  await Poll(`document.querySelector('.distance-preview canvas')`);
  await Evaluate(
    `(()=>{window.ExportBlob=null;window.CreateURL=URL.createObjectURL;URL.createObjectURL=B=>{window.ExportBlob=B;return window.CreateURL.call(URL,B);};window.AnchorClick=HTMLAnchorElement.prototype.click;HTMLAnchorElement.prototype.click=function(){};})()`,
  );
  await Button("Export distance samples");
  const Exported = await Evaluate(
    `window.ExportBlob.text().then(T=>{const V=JSON.parse(T);return {N:V.Resolution,Count:V.Values.length,Units:V.Units,Layout:V.Layout};})`,
  );
  assert.equal(Exported.N, 24);
  assert.equal(Exported.Count, 24 ** 3);
  assert.equal(Exported.Units, "metres");
  assert(Exported.Layout.includes("X fastest"));
  await Evaluate(
    `URL.createObjectURL=window.CreateURL;HTMLAnchorElement.prototype.click=window.AnchorClick;`,
  );
  Checks.push(
    "distance field bake/cancel, actual slice/plane updates, float sample export and stale-bake invalidation",
  );
  await Tab(6);
  await Button("Capture current view");
  assert(
    (await Evaluate(`document.querySelector('.custom-icon').src`)).startsWith(
      "data:image/png",
    ),
  );
  await Set("Author / studio", "Frontier review");
  await Set("Tags", "mechanical, tested");
  await Button("Add property");
  await Set("Property 1 key", "revision");
  await Set("Property 1 value", "3");
  await Shot("metadata");
  const Extents = await Evaluate(
    `document.querySelector('.preview-statistics').textContent`,
  );
  await Click(".import-confirm");
  await Poll(`document.querySelector('.completion-overlay')`);
  const Record = await Evaluate(
    `new Promise((Resolve,Reject)=>{const R=indexedDB.open('Frontier.ImportDialogue',1);R.onsuccess=()=>{const D=R.result,Q=D.transaction('Imports','readonly').objectStore('Imports').getAll();Q.onsuccess=()=>{const V=Q.result.filter(R=>R.Name==='Housing_Review').sort((A,B)=>B.Created.localeCompare(A.Created))[0];Resolve({Name:V.Name,Author:V.Manifest.Settings.Author,Scale:V.Manifest.Settings.Scale,Custom:V.Manifest.Settings.Custom,Levels:V.Levels.length,Source:V.SourceGeometry.Positions.length,Geometry:V.Geometry.Positions.length,Materials:V.Manifest.Settings.Materials,Distance:V.DistanceField?{N:V.DistanceField.Resolution,Count:V.DistanceField.Values.length,Typed:V.DistanceField.Values instanceof Float32Array}:null});D.close();};};R.onerror=()=>Reject(R.error);})`,
  );
  assert.equal(Record.Author, "Frontier review");
  assert.equal(Record.Levels, 4);
  assert.equal(Record.Materials[0].Name, "Titanium · reviewed");
  assert.equal(Record.Materials[0].Colour, "#5c8caa");
  assert.equal(Record.Materials[0].Metalness, 0.62);
  assert.deepEqual(Record.Distance, { N: 24, Count: 24 ** 3, Typed: true });
  assert.equal(Record.Custom[0].Value, "3");
  await Button("Back to library");
  assert(!(await Evaluate(`!!document.querySelector('.dialog-backdrop')`)));
  await Evaluate(
    `[...document.querySelectorAll('.library-tile')].find(B=>B.querySelector('span')?.textContent==='Housing_Review').click()`,
  );
  await Poll(
    `document.querySelector('.source-ident h2')?.textContent==='Housing_Review'`,
  );
  await Wait(400);
  assert.equal(
    await Evaluate(`document.querySelector('.preview-statistics').textContent`),
    Extents,
  );
  Checks.push(
    "metadata, icon capture, original geometry and baked levels save in IndexedDB and reopen without double transforms",
  );
  await Tab(4);
  assert.equal(
    await Evaluate(
      `document.querySelector('input[aria-label="Material name"]').value`,
    ),
    "Titanium · reviewed",
  );
  await Tab(5);
  assert(
    await Evaluate(`!!document.querySelector('.distance-preview canvas')`),
  );
  Checks.push(
    "material recipes plus float32 distance volume save/reopen in IndexedDB",
  );
  await Tab(0);
  await Evaluate(
    `document.querySelectorAll('.sample-switcher button')[1].click()`,
  );
  await Poll(`document.querySelector('.kind-image img')?.naturalWidth===512`);
  assert(
    await Evaluate(
      `[...document.querySelectorAll('.step-navigation button')].slice(1,6).every(B=>B.disabled)`,
    ),
  );
  await Click('[aria-label="Next import page"]');
  assert(
    await Evaluate(
      `document.querySelectorAll('.step-navigation button')[6].getAttribute('aria-selected')==='true'`,
    ),
  );
  Checks.push(
    "image import has a real preview and skips inapplicable geometry pages",
  );
  await Tab(0);
  await Evaluate(
    `document.querySelectorAll('.sample-switcher button')[2].click()`,
  );
  await Poll(
    `document.querySelector('.audio-wave small')?.textContent.includes('channel(s)')`,
  );
  assert(await Evaluate(`!!document.querySelector('audio[controls]')`));
  await Shot("audio");
  Checks.push("audio specimen decodes a real waveform with playback controls");
  await Inject("Readme.txt", "<script>window.injected=true</script>");
  assert.equal(
    await Evaluate(`document.querySelector('.document-specimen').textContent`),
    "<script>window.injected=true</script>",
  );
  assert(!(await Evaluate(`!!window.injected`)));
  await Inject("Capture.custom", "Arbitrary bytes");
  assert(await Evaluate(`!!document.querySelector('.binary-specimen')`));
  Checks.push(
    "arbitrary file types retain source bytes and text previews cannot execute scripts",
  );
  await Inject(
    "Triangle.obj",
    "v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nf 1/1 2/2 3/3\n",
  );
  await Poll(`document.querySelector('.geometry-render canvas')`);
  assert.equal(
    await Evaluate(
      `document.querySelector('.preview-statistics strong').textContent`,
    ),
    "1",
  );
  await Inject("Broken.obj", "v 0 0 0");
  assert(
    await Evaluate(
      `document.querySelector('[role="alert"]').textContent.includes('No triangle geometry')`,
    ),
  );
  assert.equal(
    await Evaluate(`document.querySelector('.source-ident h2').textContent`),
    "Triangle",
  );
  await Click(".keep-source");
  await Poll(`document.querySelector('.binary-specimen')`);
  assert.equal(
    await Evaluate(`document.querySelector('.source-ident h2').textContent`),
    "Broken",
  );
  Checks.push(
    "actual OBJ import reads geometry and malformed sources preserve the previous valid import",
  );
  await Tab(0);
  await Evaluate(
    `document.querySelectorAll('.sample-switcher button')[0].click()`,
  );
  await Poll(
    `document.querySelector('.source-ident h2')?.textContent==='Turbine_Housing'`,
  );
  await Wait(400);
  await Call("Emulation.setDeviceMetricsOverride", {
    width: 390,
    height: 844,
    deviceScaleFactor: 1,
    mobile: true,
  });
  await Wait(400);
  assert(
    await Evaluate(
      `document.documentElement.scrollWidth<=innerWidth&&document.querySelector('.import-dialog').scrollWidth<=document.querySelector('.import-dialog').clientWidth+1`,
    ),
  );
  assert(
    await Evaluate(
      `(()=>{const B=document.querySelector('.import-confirm').getBoundingClientRect();return B.bottom<=innerHeight&&B.right<=innerWidth;})()`,
    ),
  );
  await Shot("mobile");
  for (const N of [4, 5, 6]) {
    await Tab(N);
    assert(
      await Evaluate(
        `(()=>{const B=document.querySelectorAll('.step-navigation button')[${N}].getBoundingClientRect(),R=document.querySelector('.step-navigation').getBoundingClientRect(),P=document.querySelectorAll('.inspector-page')[${N}];return B.left>=R.left-1&&B.right<=R.right+1&&P.scrollWidth<=P.clientWidth+1;})()`,
      ),
    );
    await Shot("mobile-page-" + N);
  }

  Checks.push(
    "390px layout keeps the preview, sliding inspector and import action accessible",
  );
  await Call("Emulation.setDeviceMetricsOverride", {
    width: 1440,
    height: 1000,
    deviceScaleFactor: 1,
    mobile: false,
  });
  await Tab(0);
  await Shot("final");
  assert.equal(Errors.length, 0);
  console.log(JSON.stringify({ Passed: true, Checks, Errors }, null, 2));
} finally {
  Socket.close();
}
