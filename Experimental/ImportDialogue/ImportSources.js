import * as THREE from "three";
import { OBJLoader } from "three/examples/jsm/loaders/OBJLoader.js";
import { STLLoader } from "three/examples/jsm/loaders/STLLoader.js";
import { PLYLoader } from "three/examples/jsm/loaders/PLYLoader.js";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { FBXLoader } from "three/examples/jsm/loaders/FBXLoader.js";
export const SampleEntries = [
  {
    Id: "housing",
    Name: "Turbine_Housing.obj",
    Kind: "Geometry",
    Icon: "asset-file-obj",
    Note: "Built-in mechanical specimen",
  },
  {
    Id: "surface",
    Name: "Brushed_Titanium.png",
    Kind: "Image",
    Icon: "asset-file-png",
    Note: "Built-in surface specimen",
  },
  {
    Id: "audio",
    Name: "Cooling_Fan.wav",
    Kind: "Audio",
    Icon: "asset-file-wav",
    Note: "Built-in acoustic specimen",
  },
  {
    Id: "document",
    Name: "Surface_Response.json",
    Kind: "Document",
    Icon: "asset-file-json",
    Note: "Built-in structured text",
  },
];
export function FileKind(Name) {
  const Extension = Name.split(".").pop().toLowerCase();
  if (["obj", "stl", "ply", "gltf", "glb", "fbx"].includes(Extension))
    return "Geometry";
  if (
    [
      "png",
      "jpg",
      "jpeg",
      "webp",
      "gif",
      "avif",
      "bmp",
      "svg",
      "hdr",
      "exr",
      "tif",
      "tiff",
    ].includes(Extension)
  )
    return "Image";
  if (["wav", "mp3", "ogg", "flac", "aac", "m4a", "opus"].includes(Extension))
    return "Audio";
  if (["mp4", "webm", "mov", "mkv"].includes(Extension)) return "Video";
  if (["ttf", "otf", "woff", "woff2"].includes(Extension)) return "Font";
  if (
    [
      "json",
      "txt",
      "md",
      "csv",
      "xml",
      "slang",
      "glsl",
      "hlsl",
      "py",
      "js",
      "mat",
      "material",
      "toml",
      "yaml",
    ].includes(Extension)
  )
    return "Document";
  return "Binary";
}
export function FileIcon(Name) {
  const E = Name.split(".").pop().toLowerCase();
  return globalThis.NativeAssets?.Icons?.["asset-file-" + E]
    ? "asset-file-" + E
    : {
        Geometry: "asset-file-gltf",
        Image: "asset-file-png",
        Audio: "asset-file-wav",
        Video: "slate-video-player",
        Font: "asset-file-ttf",
        Document: "slate-document",
        Binary: "package",
      }[FileKind(Name)];
}
function TextureRecord(Texture) {
  if (!Texture) return null;
  let Image = null;
  try {
    const Source = Texture.image;
    if (Source?.width && Source?.height && typeof document !== "undefined") {
      const Canvas = document.createElement("canvas"),
        Ratio = Math.min(1, 1024 / Math.max(Source.width, Source.height));
      Canvas.width = Math.max(1, Math.round(Source.width * Ratio));
      Canvas.height = Math.max(1, Math.round(Source.height * Ratio));
      Canvas.getContext("2d").drawImage(
        Source,
        0,
        0,
        Canvas.width,
        Canvas.height,
      );
      Image = Canvas.toDataURL("image/png");
    }
  } catch {}
  return {
    Name: Texture.name || "Embedded texture",
    Image,
    FlipY: Texture.flipY,
    WrapS: Texture.wrapS,
    WrapT: Texture.wrapT,
    Repeat: Texture.repeat.toArray(),
    Offset: Texture.offset.toArray(),
    Rotation: Texture.rotation,
    Centre: Texture.center.toArray(),
    Channel: Texture.channel,
    Status: Image ? "Decoded preview (max 1024 px)" : "Source reference only",
  };
}
function MaterialRecord(Material, Id) {
  return {
    Name: Material?.name || `Surface ${String(Id + 1).padStart(2, "0")}`,
    Colour: "#" + (Material?.color?.getHexString() || "9da8a6"),
    Roughness: Material?.roughness ?? 0.5,
    Metalness: Material?.metalness ?? 0,
    Opacity: Material?.opacity ?? 1,
    Emissive: "#" + (Material?.emissive?.getHexString() || "000000"),
    EmissiveIntensity: Material?.emissiveIntensity ?? 1,
    DoubleSided: Material?.side === THREE.DoubleSide,
    BaseMap: TextureRecord(Material?.map),
    NormalMap: TextureRecord(Material?.normalMap),
    RoughnessMap: TextureRecord(Material?.roughnessMap),
    MetalnessMap: TextureRecord(Material?.metalnessMap),
  };
}
export function FlattenGeometry(Group, PackUv = false) {
  const Positions = [],
    Normals = [],
    Uvs = [],
    MaterialIds = [],
    Materials = [],
    MaterialLookup = new Map();
  const MaterialId = (Material) => {
    if (!MaterialLookup.has(Material)) {
      MaterialLookup.set(Material, Materials.length);
      Materials.push(MaterialRecord(Material, Materials.length));
    }
    return MaterialLookup.get(Material);
  };
  let HasUv = true,
    Parts = 0,
    PartCount = 0;
  Group.traverse((P) => {
    if (P.isMesh) PartCount++;
  });
  const Columns = Math.ceil(Math.sqrt(PartCount));
  Group.updateMatrixWorld(true);
  Group.traverse((Part) => {
    if (!Part.isMesh) return;
    let Geometry = Part.geometry.clone();
    if (Geometry.index) {
      const Indexed = Geometry;
      Geometry = Indexed.toNonIndexed();
      Indexed.dispose();
    }
    Geometry.applyMatrix4(Part.matrixWorld);
    if (!Geometry.attributes.normal) Geometry.computeVertexNormals();
    const P = Geometry.attributes.position,
      N = Geometry.attributes.normal,
      U = Geometry.attributes.uv;
    if (!P) return;
    if (P.count % 3 !== 0) throw new Error("Incomplete triangle data.");
    if (Positions.length / 3 + P.count > 750000)
      throw new Error("This browser demo accepts up to 250,000 triangles.");
    for (let I = 0; I < P.count; I += 3) {
      const Slot =
          Geometry.groups.find((G) => I >= G.start && I < G.start + G.count)
            ?.materialIndex || 0,
        Material = Array.isArray(Part.material)
          ? Part.material[Slot]
          : Part.material;
      MaterialIds.push(MaterialId(Material));
    }
    for (let I = 0; I < P.count; I++) {
      const Values = [P.getX(I), P.getY(I), P.getZ(I)];
      if (Values.some((V) => !Number.isFinite(V)))
        throw new Error("Non-finite source coordinates.");
      Positions.push(...Values);
      Normals.push(N.getX(I), N.getY(I), N.getZ(I));
      let Uv = [U?.getX(I) || 0, U?.getY(I) || 0];
      if (PackUv && U) {
        const Groups = Geometry.groups,
          GroupId = Groups.findIndex(
            (G) => I >= G.start && I < G.start + G.count,
          ),
          Count = Groups.length,
          Grid = Math.max(1, Math.ceil(Math.sqrt(Count)));
        if (Count > 1) {
          Uv = Uv.map(
            (V, A) =>
              (V * 0.88 +
                0.06 +
                (A === 0
                  ? Math.max(0, GroupId) % Grid
                  : Math.floor(Math.max(0, GroupId) / Grid))) /
              Grid,
          );
        }
        Uv = Uv.map(
          (V, A) =>
            (V * 0.88 +
              0.06 +
              (A === 0 ? Parts % Columns : Math.floor(Parts / Columns))) /
            Columns,
        );
      }
      Uvs.push(...Uv);
    }
    HasUv &&= !!U;
    Parts++;
    Geometry.dispose();
  });
  if (!Positions.length) throw new Error("No triangle geometry found.");
  return {
    Positions: new Float32Array(Positions),
    Normals: new Float32Array(Normals),
    Uvs: HasUv ? new Float32Array(Uvs) : null,
    Parts,
    Materials,
    MaterialIds: new Uint16Array(MaterialIds),
  };
}
export function HousingGeometry() {
  const Group = new THREE.Group(),
    Material = new THREE.MeshStandardMaterial({
      name: "Cast titanium",
      color: "#9ca9a3",
      metalness: 0.72,
      roughness: 0.38,
    }),
    Collar = new THREE.MeshStandardMaterial({
      name: "Brushed collars",
      color: "#b6b9af",
      metalness: 0.9,
      roughness: 0.23,
    }),
    Fastener = new THREE.MeshStandardMaterial({
      name: "Oxidised fasteners",
      color: "#43534f",
      metalness: 0.82,
      roughness: 0.46,
    });
  const Add = (
    Geometry,
    Position = [0, 0, 0],
    Rotation = [0, 0, 0],
    Surface = Material,
  ) => {
    const Part = new THREE.Mesh(Geometry, Surface);
    Part.position.set(...Position);
    Part.rotation.set(...Rotation);
    Group.add(Part);
  };
  // A machined, open-bore housing, not a generic primitive. Lathed profiles retain
  // their own UV charts; the collars, ribs and fasteners are separate surfaces.
  const Profile = [
    [0.88, -0.92],
    [1.08, -0.92],
    [1.13, -0.86],
    [1.13, -0.72],
    [1.02, -0.67],
    [1.02, 0.63],
    [1.13, 0.7],
    [1.13, 0.84],
    [1.06, 0.92],
    [0.88, 0.92],
    [0.84, 0.85],
    [0.84, -0.85],
    [0.88, -0.92],
  ].map((P) => new THREE.Vector2(...P));
  Add(new THREE.LatheGeometry(Profile, 96));
  for (const Y of [-0.77, 0.77]) {
    Add(
      new THREE.TorusGeometry(1.1, 0.085, 12, 96),
      [0, Y, 0],
      [Math.PI / 2, 0, 0],
      Collar,
    );
    for (let I = 0; I < 12; I++) {
      const A = (I / 12) * Math.PI * 2;
      Add(
        new THREE.CylinderGeometry(0.065, 0.065, 0.1, 6),
        [Math.cos(A) * 1.06, Y + (Y > 0 ? 0.12 : -0.12), Math.sin(A) * 1.06],
        [0, 0, 0],
        Fastener,
      );
    }
  }
  for (let I = 0; I < 18; I++) {
    const A = (I / 18) * Math.PI * 2;
    Add(
      new THREE.BoxGeometry(0.06, 1.18, 0.13),
      [Math.cos(A) * 1.055, 0, Math.sin(A) * 1.055],
      [0, -A + Math.PI / 2, 0],
    );
  }
  Add(new THREE.BoxGeometry(1.35, 0.16, 0.7), [0, -1.05, 0]);
  const Result = FlattenGeometry(Group, true);
  Group.traverse((P) => P.geometry?.dispose());
  Material.dispose();
  Collar.dispose();
  Fastener.dispose();
  return Result;
}
export function SurfaceSample() {
  const C = document.createElement("canvas");
  C.width = C.height = 512;
  const X = C.getContext("2d"),
    Image = X.createImageData(512, 512);
  for (let Y = 0; Y < 512; Y++)
    for (let Z = 0; Z < 512; Z++) {
      const I = (Y * 512 + Z) * 4,
        N = Math.sin(Y * 87.13 + Z * 0.14) * Math.cos(Y * 1.91) * 14,
        T = 95 + N + Math.sin(Z / 130) * 24;
      Image.data[I] = T * 0.85;
      Image.data[I + 1] = T * 0.97;
      Image.data[I + 2] = T;
      Image.data[I + 3] = 255;
    }
  X.putImageData(Image, 0, 0);
  return C.toDataURL("image/png");
}
export function AudioSample() {
  const Rate = 24000,
    Duration = 3,
    Count = Rate * Duration,
    Buffer = new ArrayBuffer(44 + Count * 2),
    View = new DataView(Buffer);
  const Text = (O, S) =>
    [...S].forEach((C, I) => View.setUint8(O + I, C.charCodeAt(0)));
  Text(0, "RIFF");
  View.setUint32(4, 36 + Count * 2, true);
  Text(8, "WAVEfmt ");
  View.setUint32(16, 16, true);
  View.setUint16(20, 1, true);
  View.setUint16(22, 1, true);
  View.setUint32(24, Rate, true);
  View.setUint32(28, Rate * 2, true);
  View.setUint16(32, 2, true);
  View.setUint16(34, 16, true);
  Text(36, "data");
  View.setUint32(40, Count * 2, true);
  for (let I = 0; I < Count; I++) {
    const T = I / Rate,
      E = Math.min(1, T * 4, (Duration - T) * 4);
    View.setInt16(
      44 + I * 2,
      E *
        (Math.sin(T * Math.PI * 2 * 90) * 0.2 +
          Math.sin(T * Math.PI * 2 * 360) * 0.035) *
        (1 + 0.12 * Math.sin(T * 13)) *
        32767,
      true,
    );
  }
  return new File([Buffer], "Cooling_Fan.wav", { type: "audio/wav" });
}
export async function ReadSource(Files, SourceOnly = false) {
  const Primary =
    Files.find(
      (F) =>
        !["bin", "mtl", "png", "jpg", "jpeg"].includes(
          F.name.split(".").pop().toLowerCase(),
        ),
    ) || Files[0];
  if (!Primary) throw new Error("Choose a file first.");
  if (Files.reduce((S, F) => S + F.size, 0) > 128 * 1024 * 1024)
    throw new Error("Choose a batch smaller than 128 MB for this demo.");
  const Kind = SourceOnly ? "Binary" : FileKind(Primary.name),
    Result = {
      Name: Primary.name,
      Kind,
      Icon: FileIcon(Primary.name),
      Bytes: Primary.size,
      Files,
      Url: null,
      Geometry: null,
      Notice: "Original source is kept unchanged.",
    };
  if (Kind === "Geometry") {
    const Urls = new Map(),
      Manager = new THREE.LoadingManager();
    Manager.setURLModifier((Url) => {
      if (Url.startsWith("data:") || Url.startsWith("blob:")) return Url;
      const Key = decodeURIComponent(Url.split("/").pop());
      const Local = Files.find((F) => F.name === Key);
      if (!Local)
        throw new Error(
          "Missing companion file: " +
            Key +
            ". Choose the model and its dependencies together.",
        );
      if (!Urls.has(Key)) Urls.set(Key, URL.createObjectURL(Local));
      return Urls.get(Key);
    });
    try {
      const E = Primary.name.split(".").pop().toLowerCase(),
        Buffer = await Primary.arrayBuffer();
      let Group;
      if (E === "obj")
        Group = new OBJLoader(Manager).parse(new TextDecoder().decode(Buffer));
      else if (E === "stl" || E === "ply") {
        const Geometry =
          E === "stl"
            ? new STLLoader().parse(Buffer)
            : new PLYLoader().parse(Buffer);
        if (E === "ply" && !Geometry.index) {
          Geometry.dispose();
          throw new Error(
            "Point-only PLY needs a native point importer. Keep the source instead.",
          );
        }
        Group = new THREE.Mesh(Geometry);
      } else if (E === "fbx") Group = new FBXLoader(Manager).parse(Buffer, "");
      else
        Group = (
          await new GLTFLoader(Manager).parseAsync(
            E === "glb" ? Buffer : new TextDecoder().decode(Buffer),
            "",
          )
        ).scene;
      Result.Geometry = FlattenGeometry(Group);
      Result.Notice =
        "Geometry, UVs and material slots decoded locally. PBR previews use supported source properties; rigs, animation and full shader graphs remain in the original file.";
      Group.traverse((P) => {
        P.geometry?.dispose();
        for (const M of Array.isArray(P.material)
          ? P.material
          : P.material
            ? [P.material]
            : []) {
          for (const V of Object.values(M)) if (V?.isTexture) V.dispose();
          M.dispose();
        }
      });
    } finally {
      Urls.forEach((U) => URL.revokeObjectURL(U));
    }
  } else if (Kind === "Document") {
    Result.Text = (await Primary.text()).slice(0, 200000);
    Result.Notice =
      Primary.size > 200000
        ? "Text preview truncated to 200,000 characters; original is retained."
        : "UTF-8 text preview. Original source is retained.";
  } else if (["Image", "Audio", "Video", "Font"].includes(Kind))
    Result.Url = URL.createObjectURL(Primary);
  else {
    Result.Hex = Array.from(
      new Uint8Array(await Primary.slice(0, 128).arrayBuffer()),
    )
      .map((V) => V.toString(16).padStart(2, "0"))
      .join(" ");
    Result.Notice =
      "No browser decoder for this format. Source, metadata and icon can still be imported.";
  }
  return Result;
}
export async function BuiltInSource(Id) {
  if (Id === "housing")
    return {
      Name: "Turbine_Housing.obj",
      Kind: "Geometry",
      Icon: "asset-file-obj",
      Bytes: 0,
      Files: [],
      Geometry: HousingGeometry(),
      Notice:
        "Built-in procedural specimen. Geometry and UVs are real; there is no external OBJ source file.",
    };
  if (Id === "surface")
    return ReadSource([
      new File(
        [await (await fetch(SurfaceSample())).blob()],
        "Brushed_Titanium.png",
        { type: "image/png" },
      ),
    ]);
  if (Id === "audio") return ReadSource([AudioSample()]);
  return ReadSource([
    new File(
      [
        JSON.stringify(
          {
            name: "Brushed titanium",
            roughness: 0.32,
            metalness: 1,
            normalSpace: "tangent",
            revision: 3,
          },
          null,
          2,
        ),
      ],
      "Surface_Response.json",
      { type: "application/json" },
    ),
  ]);
}
