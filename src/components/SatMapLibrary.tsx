import { useEffect, useMemo, useRef, useState } from "react";
import { Check, ChevronLeft, ChevronRight, Search, X } from "lucide-react";
import {
  SATMAP_LIBRARY,
  SATMAP_FAMILIES,
  filterSatmaps,
  type BuiltinSatmapId,
  type SatmapFamily,
} from "../engine/satmaps/catalog";
import { paletteGradient } from "../engine/satmaps/satmap";
import type { SatmapId } from "../engine/types";
export const SATMAP_PAGE_SIZE = 12;

export function SatMapLibrary({
  selected,
  onSelect,
}: {
  selected: SatmapId;
  onSelect: (id: BuiltinSatmapId) => void;
}) {
  const [query, setQuery] = useState("");
  const [family, setFamily] = useState<SatmapFamily | "all">("all");
  const [origin, setOrigin] = useState<"all" | "satellite" | "authored">("all");
  const [page, setPage] = useState(() =>
    Math.floor(
      Math.max(
        0,
        SATMAP_LIBRARY.findIndex((a) => a.id === selected),
      ) / SATMAP_PAGE_SIZE,
    ),
  );
  const results = useMemo(
    () => filterSatmaps(query, family, origin),
    [query, family, origin],
  );
  const pages = Math.max(1, Math.ceil(results.length / SATMAP_PAGE_SIZE));
  const currentPage = Math.min(page, pages - 1);
  const visible = results.slice(
    currentPage * SATMAP_PAGE_SIZE,
    (currentPage + 1) * SATMAP_PAGE_SIZE,
  );
  const previous = useRef(selected);
  // Restore a newly loaded project's selection even when its card is on page 9.
  useEffect(() => {
    if (previous.current !== selected) {
      previous.current = selected;
      const index = results.findIndex((a) => a.id === selected);
      if (index >= 0) setPage(Math.floor(index / SATMAP_PAGE_SIZE));
    }
  }, [selected, results]);
  const reset = () => {
    setQuery("");
    setFamily("all");
    setOrigin("all");
    setPage(0);
  };
  const reveal = () => {
    reset();
    setPage(
      Math.floor(
        Math.max(
          0,
          SATMAP_LIBRARY.findIndex((a) => a.id === selected),
        ) / SATMAP_PAGE_SIZE,
      ),
    );
  };
  return (
    <div className="satmap-browser">
      <p className="satmap-library-note">
        4 satellite-derived + 96 authored CLUTs. Only the selected map is
        uploaded to the terrain renderer.
      </p>
      <div className="satmap-search">
        <Search size={14} aria-hidden="true" />
        <input
          type="search"
          aria-label="Search SatMaps"
          placeholder="Search names, colors, environments…"
          value={query}
          onChange={(e) => {
            setQuery(e.target.value);
            setPage(0);
          }}
        />
        {query && (
          <button
            aria-label="Clear SatMap search"
            onClick={() => {
              setQuery("");
              setPage(0);
            }}
          >
            <X size={13} />
          </button>
        )}
      </div>
      <div className="satmap-filters">
        <label>
          Environment
          <select
            aria-label="SatMap environment"
            value={family}
            onChange={(e) => {
              setFamily(e.target.value as typeof family);
              setPage(0);
            }}
          >
            <option value="all">
              All environments · {SATMAP_LIBRARY.length}
            </option>
            {SATMAP_FAMILIES.map((f) => (
              <option key={f.id} value={f.id}>
                {f.name} ·{" "}
                {SATMAP_LIBRARY.filter((a) => a.family === f.id).length}
              </option>
            ))}
          </select>
        </label>
        <label>
          Provenance
          <select
            aria-label="SatMap provenance"
            value={origin}
            onChange={(e) => {
              setOrigin(e.target.value as typeof origin);
              setPage(0);
            }}
          >
            <option value="all">All maps</option>
            <option value="satellite">Satellite-derived</option>
            <option value="authored">Authored / fantasy</option>
          </select>
        </label>
      </div>
      <div className="satmap-results-line">
        <span role="status" aria-label="SatMap search results">
          {results.length
            ? `${currentPage * SATMAP_PAGE_SIZE + 1}–${Math.min(results.length, (currentPage + 1) * SATMAP_PAGE_SIZE)} of ${results.length} maps`
            : "No matching maps"}
        </span>
        {selected !== "custom" && !visible.some((a) => a.id === selected) && (
          <button onClick={reveal}>Show selected</button>
        )}
      </div>
      <div
        className="satmap-library"
        role="group"
        aria-label="Satellite palette library"
      >
        {visible.map((item) => (
          <button
            key={item.id}
            data-satmap-id={item.id}
            className={`satmap-card ${selected === item.id ? "selected" : ""}`}
            aria-pressed={selected === item.id}
            title={`${item.name} · ${item.note}`}
            onClick={() => onSelect(item.id)}
          >
            <div
              className={`satmap-thumbnail ${item.thumbnail ? "" : "satmap-color-preview"}`}
            >
              {item.thumbnail ? (
                <img
                  src={`${import.meta.env.BASE_URL}${item.thumbnail}`}
                  alt=""
                />
              ) : (
                <span
                  className="satmap-card-colors"
                  style={{ background: paletteGradient(item.palette) }}
                  aria-hidden="true"
                />
              )}
              <span className="satmap-category">{item.category}</span>
              <span className="satmap-card-index">
                {String(SATMAP_LIBRARY.indexOf(item) + 1).padStart(3, "0")}
              </span>
              {selected === item.id && (
                <i>
                  <Check size={11} />
                </i>
              )}
            </div>
            <strong>{item.name}</strong>
            <small>
              {item.origin === "satellite"
                ? "Satellite · " + item.region
                : item.origin === "fantasy"
                  ? "Fantasy · authored"
                  : "Terrain-inspired · authored"}
            </small>
          </button>
        ))}
      </div>
      {!results.length && (
        <div className="satmap-empty">
          <p>Try another name, environment or provenance filter.</p>
          <button onClick={reset}>Reset library filters</button>
        </div>
      )}
      {results.length > SATMAP_PAGE_SIZE && (
        <nav className="satmap-pagination" aria-label="SatMap pages">
          <button
            aria-label="Previous SatMap page"
            disabled={currentPage === 0}
            onClick={() => setPage(currentPage - 1)}
          >
            <ChevronLeft size={14} /> Previous
          </button>
          <label className="satmap-page-label">
            Page
            <select
              aria-label="SatMap page"
              value={currentPage}
              onChange={(e) => setPage(Number(e.target.value))}
            >
              {Array.from({ length: pages }, (_, i) => (
                <option key={i} value={i}>
                  {i + 1} / {pages}
                </option>
              ))}
            </select>
          </label>
          <button
            aria-label="Next SatMap page"
            disabled={currentPage === pages - 1}
            onClick={() => setPage(currentPage + 1)}
          >
            Next <ChevronRight size={14} />
          </button>
        </nav>
      )}
    </div>
  );
}
