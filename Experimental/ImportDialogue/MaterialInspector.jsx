import React, { useRef, useEffect } from "react";
export default function MaterialInspector({
  Geometry,
  Settings,
  Change,
  Controls,
  Notify,
}) {
  const { Section, Scalar, Toggle, Field, Note, Readout, G } = Controls,
    Input = useRef(null),
    Target = useRef("BaseMap"),
    Slots = Settings.Materials || [],
    Selected = Math.min(
      Settings.MaterialSelection || 0,
      Math.max(0, Slots.length - 1),
    ),
    Surface = Slots[Selected];
  const Epoch = useRef(0);
  useEffect(() => {
    Epoch.current++;
  }, [Geometry]);
  const Update = (Key, Value) =>
    Change(
      "Materials",
      Slots.map((S, I) => (I === Selected ? { ...S, [Key]: Value } : S)),
    );
  const Upload = (File) => {
    if (!File) return;
    if (
      !["image/png", "image/jpeg", "image/webp", "image/svg+xml"].includes(
        File.type,
      ) ||
      File.size > 8 * 1024 * 1024
    ) {
      Notify("Choose a PNG, JPEG, WebP or SVG texture smaller than 8 MB.");
      return;
    }
    const Ticket = Epoch.current;
    const Slot = Selected,
      Map = Target.current,
      Reader = new FileReader();
    Reader.onload = () => {
      const Image = new window.Image();
      Image.onload = () => {
        if (Ticket !== Epoch.current) return;
        Change(
          "Materials",
          Slots.map((S, I) =>
            I === Slot
              ? {
                  ...S,
                  [Map]: {
                    Name: File.name,
                    Image: Reader.result,
                    Status: "Local replacement",
                  },
                }
              : S,
          ),
        );
        Notify(File.name + " assigned to " + Map + ".");
      };
      Image.onerror = () => Notify("This texture could not be decoded.");
      Image.src = Reader.result;
    };
    Reader.onerror = () => Notify("This texture could not be read.");
    Reader.readAsDataURL(File);
  };
  if (!Surface)
    return (
      <Note Children="This source has no decoded material slots. The original source remains unchanged." />
    );
  const Count = (Id) =>
    Array.from(Geometry?.MaterialIds || []).filter((I) => I === Id).length;
  return (
    <>
      <Section
        Title="Material slots"
        Tag={Slots.length + " SURFACES"}
        Children={
          <>
            <div className="material-slot-list">
              {Slots.map((S, I) => (
                <button
                  key={I}
                  aria-pressed={Selected === I}
                  onClick={() => {
                    Change("MaterialSelection", I);
                    Change("HighlightMaterial", true);
                  }}
                >
                  <span
                    className="surface-swatch"
                    style={{
                      "--surface-colour": S.Colour,
                      "--surface-roughness": S.Roughness,
                    }}
                  />
                  <span>
                    <strong>{S.Name}</strong>
                    <small>
                      {Count(I).toLocaleString()} triangles · slot {I}
                    </small>
                  </span>
                  <G Name={Selected === I ? "CircleCheck" : "ChevronRight"} />
                </button>
              ))}
            </div>
            <div className="material-toggle">
              <Toggle
                Name="Isolate selected material"
                Symbol="visible"
                On={Settings.HighlightMaterial}
                Change={(V) => Change("HighlightMaterial", V)}
              />
            </div>
          </>
        }
      />
      <Section
        Title="Surface response"
        Tag="LIVE PBR PREVIEW"
        Children={
          <>
            <Field
              Label="Material name"
              Children={
                <input
                  aria-label="Material name"
                  value={Surface.Name}
                  onChange={(E) => Update("Name", E.target.value)}
                />
              }
            />
            <div className="material-colours">
              {[
                ["Colour", "Base colour"],
                ["Emissive", "Emission colour"],
              ].map(([Key, Label]) => (
                <Field
                  key={Key}
                  Label={Label}
                  Children={
                    <div className="colour-editor">
                      <input
                        type="color"
                        aria-label={Label}
                        value={Surface[Key]}
                        onChange={(E) => Update(Key, E.target.value)}
                      />
                      <span>{Surface[Key].toUpperCase()}</span>
                    </div>
                  }
                />
              ))}
            </div>
            <Scalar
              Label="Metallic"
              Value={Surface.Metalness}
              Min={0}
              Max={1}
              Step={0.01}
              Change={(V) => Update("Metalness", V)}
            />
            <Scalar
              Label="Roughness"
              Value={Surface.Roughness}
              Min={0.02}
              Max={1}
              Step={0.01}
              Change={(V) => Update("Roughness", V)}
            />
            <Scalar
              Label="Opacity"
              Value={Surface.Opacity}
              Min={0}
              Max={1}
              Step={0.01}
              Change={(V) => Update("Opacity", V)}
            />
            <Scalar
              Label="Emission intensity"
              Value={Surface.EmissiveIntensity}
              Min={0}
              Max={10}
              Step={0.05}
              Unit="×"
              Change={(V) => Update("EmissiveIntensity", V)}
            />
            <div className="material-toggle">
              <Toggle
                Name="Double-sided surface"
                Symbol="split"
                On={Surface.DoubleSided}
                Change={(V) => Update("DoubleSided", V)}
              />
            </div>
          </>
        }
      />
      <Section
        Title="Texture inputs"
        Tag="UV0"
        Children={
          <>
            <input
              ref={Input}
              hidden
              type="file"
              aria-label="Choose material texture"
              accept="image/png,image/jpeg,image/webp,image/svg+xml"
              onChange={(E) => {
                Upload(E.target.files[0]);
                E.target.value = "";
              }}
            />
            <div className="texture-inputs">
              {[
                ["BaseMap", "Base colour", "sRGB"],
                ["NormalMap", "Normal", "Linear"],
                ["RoughnessMap", "Roughness · G", "Linear"],
                ["MetalnessMap", "Metallic · B", "Linear"],
              ].map(([Key, Label, Space]) => (
                <div key={Key}>
                  {Surface[Key]?.Image ? (
                    <img src={Surface[Key].Image} alt={Label + " texture"} />
                  ) : (
                    <span className="texture-empty">
                      <G Name="LayersSlabs" />
                    </span>
                  )}
                  <div>
                    <strong>{Label}</strong>
                    <small>
                      {Surface[Key]?.Name || "No texture"} · {Space}
                    </small>
                  </div>
                  <button
                    title={"Choose " + Label + " texture"}
                    aria-label={"Choose " + Label + " texture"}
                    disabled={!Geometry?.Uvs}
                    onClick={() => {
                      Target.current = Key;
                      Input.current.click();
                    }}
                  >
                    <G Name="FolderOpen" Size={14} />
                  </button>
                  {Surface[Key] && (
                    <button
                      title={"Clear " + Label + " texture"}
                      aria-label={"Clear " + Label + " texture"}
                      onClick={() => Update(Key, null)}
                    >
                      <G Name="CloseCross" Size={13} />
                    </button>
                  )}
                </div>
              ))}
            </div>
            <Note
              Children={
                !Geometry?.Uvs
                  ? "Texture replacement needs UV0 coordinates. Constant PBR properties remain editable."
                  : "Imported decoded texture previews are capped at 1024 px; full source files remain unchanged. glTF metallic/roughness channels use B/G respectively. Only UV0 is retained; native shader graphs are not executed."
              }
            />
            <button
              className="text-button"
              onClick={() =>
                Change(
                  "Materials",
                  Slots.map((S, I) =>
                    I === Selected
                      ? structuredClone(Geometry.Materials?.[I] || S)
                      : S,
                  ),
                )
              }
            >
              <G Name="RotateCounterClockwise" />
              Restore source material
            </button>
          </>
        }
      />
    </>
  );
}
