/*====================================================================================================================================
                                                       CATALOGUEMENU.JS
====================================================================================================================================*/
// 🧩 The searchable spawn menu, filtered to ports compatible with a released link when one is in flight

import { ComposeGlyph } from './GlyphOutlines.js';
import { NodeCatalogue } from '../Graph/CatalogueSpecifications.js';
import { EscapeMarkup } from './EntryRasterization.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const MenuWidth  = 256;   // [px] - Fixed menu width, used to keep it inside the window
const MenuHeight = 400;   // [px] - Worst-case menu height for the same clamp

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

let MenuElement    = null;
let ScreenElement  = null;
let SearchTerm     = '';
let FoldMemory     = { input: true, generator: true, math: true };
let LinkContext    = null;
let SpawnCallback  = null;

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 With a link in flight, only nodes that can terminate it are offered: releasing an outbound link
//    needs a candidate with a compatible inbound port, and vice versa.
function FilterByLinkContext(Categories)
{
    if (!LinkContext) return Categories;
    const { PortDirection, PortClassification } = LinkContext;

    return Categories.map((Category) =>
    {
        const Admissible = Category.Items.filter((Item) =>
        {
            if (PortDirection === 'source')
            {
                if (!Item.InboundPorts || Item.InboundPorts.length === 0) return false;
                if (PortClassification === 'any') return true;
                return Item.InboundPorts.includes('any') || Item.InboundPorts.includes(PortClassification);
            }
            if (!Item.OutboundPort) return false;
            if (PortClassification === 'any' || Item.OutboundPort === 'any') return true;
            return Item.OutboundPort === PortClassification;
        });
        return Admissible.length > 0 ? { ...Category, Items: Admissible } : null;
    }).filter(Boolean);
}

function FilterBySearchTerm(Categories)
{
    const Needle = SearchTerm.trim().toLowerCase();
    if (!Needle) return Categories;

    return Categories.map((Category) =>
    {
        if (Category.Naming.toLowerCase().includes(Needle)) return Category;
        const Matching = Category.Items.filter((Item) =>
            Item.Naming.toLowerCase().includes(Needle) || Item.Summary.toLowerCase().includes(Needle));
        return Matching.length > 0 ? { ...Category, Items: Matching } : null;
    }).filter(Boolean);
}

function ComposeRoster()
{
    const Visible = FilterBySearchTerm(FilterByLinkContext(NodeCatalogue));

    if (Visible.length === 0) return `<div class="MenuVacant">No nodes found</div>`;

    // 📝 An active search forces every surviving fold open so matches are never hidden.
    const SearchActive = SearchTerm.trim().length > 0;

    return Visible.map((Category) =>
    {
        const Unfolded = SearchActive || FoldMemory[Category.Category];
        const Chevron  = ComposeGlyph(Unfolded ? 'ChevronDown' : 'Chevron', 14);

        const Items = Unfolded ? Category.Items.map((Item) =>
            `<button class="CatalogueItem" data-item="${Item.Token}" data-category="${Category.Category}">`
          +   `<span>${EscapeMarkup(Item.Naming)}</span>`
          +   `<span class="ItemAdd">${ComposeGlyph('Plus', 14)}</span>`
          + `</button>`).join('') : '';

        return `<div class="CategoryFold">`
             +   `<button class="CategoryPill" data-fold="${Category.Category}">`
             +     `<div class="CategoryNaming">${ComposeGlyph(Category.Glyph, 16, 1.5)}`
             +     `<span>${EscapeMarkup(Category.Naming)}</span></div>`
             +     `<div class="FoldChevron">${Chevron}</div>`
             +   `</button>`
             +   (Unfolded ? `<div class="CatalogueItems">${Items}</div>` : '')
             + `</div>`;
    }).join('');
}

function RefreshRoster()
{
    if (!MenuElement) return;
    const Roster = MenuElement.querySelector('#MenuRoster');
    if (Roster) Roster.innerHTML = ComposeRoster();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

export function ConcealCatalogueMenu()
{
    if (MenuElement)   { MenuElement.remove();   MenuElement = null; }
    if (ScreenElement) { ScreenElement.remove(); ScreenElement = null; }
    SearchTerm  = '';
    LinkContext = null;
}

export function CatalogueMenuRevealedCondition()
{
    return MenuElement !== null;
}

// Reveal the spawn menu at a screen coordinate. LinkSpecification, when supplied, filters the roster and
// is handed back to the spawn callback so the caller can integrate the pending link.
export function RevealCatalogueMenu(ScreenX, ScreenY, LinkSpecification, OnSpawn)
{
    ConcealCatalogueMenu();

    LinkContext   = LinkSpecification || null;
    SpawnCallback = OnSpawn;

    ScreenElement = document.createElement('div');
    ScreenElement.className = 'MenuScreen';
    ScreenElement.addEventListener('pointerdown', ConcealCatalogueMenu);
    ScreenElement.addEventListener('contextmenu', (PointerRelease) =>
    {
        PointerRelease.preventDefault();
        ConcealCatalogueMenu();
    });
    document.body.appendChild(ScreenElement);

    MenuElement = document.createElement('div');
    MenuElement.className = 'CatalogueMenu';
    MenuElement.style.left = `${Math.max(8, Math.min(ScreenX, window.innerWidth  - MenuWidth  - 20))}px`;
    MenuElement.style.top  = `${Math.max(8, Math.min(ScreenY, window.innerHeight - MenuHeight - 20))}px`;
    MenuElement.innerHTML =
        `<div class="MenuSearchRegion">`
      +   `<div class="MenuSearchField">`
      +     `<span style="color:#9ca3af;display:flex">${ComposeGlyph('Magnifier', 14)}</span>`
      +     `<input type="text" placeholder="Search nodes..." id="MenuSearchInput">`
      +   `</div>`
      + `</div>`
      + `<div class="MenuRoster CustomScroll" id="MenuRoster">${ComposeRoster()}</div>`;

    MenuElement.addEventListener('pointerdown', (PointerPress) => PointerPress.stopPropagation());
    MenuElement.addEventListener('contextmenu', (PointerPress) => PointerPress.preventDefault());

    const SearchInput = MenuElement.querySelector('#MenuSearchInput');
    SearchInput.addEventListener('input', () =>
    {
        SearchTerm = SearchInput.value;
        RefreshRoster();
    });

    MenuElement.addEventListener('click', (PointerRelease) =>
    {
        const FoldPill = PointerRelease.target.closest('[data-fold]');
        if (FoldPill)
        {
            const CategoryKey = FoldPill.dataset.fold;
            FoldMemory[CategoryKey] = !FoldMemory[CategoryKey];
            RefreshRoster();
            return;
        }

        const ItemAction = PointerRelease.target.closest('[data-item]');
        if (ItemAction && SpawnCallback)
        {
            const CategoryKey = ItemAction.dataset.category;
            const Category = NodeCatalogue.find((Candidate) => Candidate.Category === CategoryKey);
            const Item = Category && Category.Items.find((Candidate) => Candidate.Token === ItemAction.dataset.item);
            const PendingContext = LinkContext;
            const SpawnScreenX = parseFloat(MenuElement.style.left);
            const SpawnScreenY = parseFloat(MenuElement.style.top);
            ConcealCatalogueMenu();
            if (Item) SpawnCallback(Item, CategoryKey, SpawnScreenX, SpawnScreenY, PendingContext);
        }
    });

    document.body.appendChild(MenuElement);
    SearchInput.focus();
}
