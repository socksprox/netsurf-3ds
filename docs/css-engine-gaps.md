# CSS / layout engine gaps (NetSurf 3DS)

This document surveys what the NetSurf stack already supports, what is parsed but
not painted, and what is missing entirely. It is intended to guide incremental
improvements that help real sites (search boxes, forms, navigation chrome) without
chasing full Chrome compatibility.

## Architecture (three layers)

```
  libcss          Parse + cascade CSS → computed styles on DOM nodes
       ↓
  layout (html/)  box tree, dimensions, flex/table algorithms
       ↓
  redraw (html/)  backgrounds, borders, text, form gadgets → plotters → nsfb
```

A rendering bug may live in any layer. Always determine which before fixing.

| Symptom | Likely layer |
|---------|----------------|
| Property ignored in View Source styles | libcss (parse) or not implemented |
| Element missing or wrong size | layout / flex |
| Size correct, wrong appearance | redraw / fonts |
| Works on desktop NetSurf, not 3DS | framebuffer plotters / fonts / scale |

Compare against **desktop SDL NetSurf** (`TARGET=framebuffer`) before assuming a
3DS-only bug.

---

## Tier 1 — Quick wins (small diff, high visibility)

These use **existing libcss properties**; work is mostly in `netsurf/content/handlers/html/redraw*.c`.

| Feature | libcss | Layout | Paint | Effort | Notes |
|---------|--------|--------|-------|--------|-------|
| **opacity** | ✅ parsed | — | ✅ added | Small | Multiply alpha on backgrounds; compositing still limited |
| **outline** | ✅ parsed | — | ✅ added | Small | Focus rings (`outline: 2px solid …`); drawn outside border box |
| **box-sizing** | ✅ | ✅ used | — | Done | Already in layout; default.css uses `border-box` for inputs |

### opacity (implemented)

- Parser: `libcss/src/parse/properties/opacity.c`
- Getter: `css_computed_opacity()`
- Applied when converting `background-color` to plot colours in `redraw.c`
- Limitation: no full stacking-context / knockout alpha compositing; semi-transparent
  overlays may look wrong where multiple layers overlap

### outline (implemented)

- Parser: `outline-color`, `outline-style`, `outline-width` in `properties.gen`
- New: `redraw_outline.c` draws a rectangular outline outside the padding edge
- Limitation: no `outline-offset`; not rounded

### hairline borders at fit-width zoom (fixed)

The 3DS opens pages at "fit width" zoom, so a 1024px desktop layout on the 400px
top screen renders at roughly `scale = 0.39`. Redraw scaled border widths with a
truncating `int` cast, so every 1px border became 0px and was skipped entirely —
Google's search box, table rules and input outlines all disappeared until the user
zoomed past 100%.

`html_redraw_scale_border_width()` in `redraw_style.c` now clamps any non-zero
border to at least one device pixel. Applied in `redraw.c` (block and inline
boxes), `redraw_border.c` (both border painters) and `redraw_outline.c`.

---

## Tier 2 — Medium effort (high impact for modern UI)

| Feature | libcss | Layout | Paint | Effort | Impact |
|---------|--------|--------|-------|--------|--------|
| **border-radius** | ❌ | — | ❌ | Medium | Rounded inputs (Google, buttons); needs libcss + redraw |
| **box-shadow** | ❌ | — | ❌ | Medium–large | Soft shadows on cards/inputs; parse + multi-pass paint |
| **text-overflow: ellipsis** | ❌ | partial | ❌ | Medium | Truncated labels in tight flex rows |
| **overflow: hidden** + radius | partial | partial | ❌ | Medium | Clip children to rounded rect |
| **min()/max()/clamp()** in sizes | ❌ | — | — | Medium | Responsive layouts |
| **CSS grid layout** | parse only | ❌ | — | Large | `display:grid` → `BOX_BLOCK` fallback today |
| **outline-offset** | ❌ | — | — | Small | After outline works |

### border-radius — recommended next step

**Why it matters:** Desktop Google and most form UIs use `border-radius` on `<input>`
and wrapper divs. Without it, search boxes look like flat rectangles or clip oddly.

**What exists today:**

- `libnsfb` has `nsfb_plot_ellipse_fill`, `nsfb_plot_arc` (used by app menu)
- Borders/backgrounds use axis-aligned rectangles in `redraw_border.c` / `redraw.c`
- No `border-*-radius` in `libcss/src/parse/properties/properties.gen`

**Implementation plan:**

1. **libcss** (codegen)
   - Add to `properties.gen`:
     - `border_top_left_radius`, `border_top_right_radius`,
       `border_bottom_left_radius`, `border_bottom_right_radius`
     - Shorthand `border_radius` parser (like `margin`)
   - Add length fields to `select_config.py` → run `make select_generator`
   - Add keyword tokens to `propstrings` if needed
   - Regenerate parse autogen via `gen_parser`

2. **layout** (optional v1)
   - Radius does not affect layout in standard CSS (border-box sizing already handled)
   - May need hit-testing tweaks for rounded inputs later

3. **redraw**
   - Helper: `html_redraw_rounded_background()` — fill rect minus four corner arcs
   - Extend `html_redraw_borders()` or add `html_redraw_rounded_borders()`
   - Use `nsfb_plot_arc` / filled polygons for corners
   - v1: single radius per corner; v2: elliptical radii (`50% / 25%`)

4. **Test**
   - Local HTML: `<input style="border-radius:24px;border:1px solid #ccc">`
   - Compare SDL vs 3DS framebuffer

**Rough size:** ~400–800 lines across libcss + redraw.

### box-shadow

- Not in libcss at all
- Paint: offset blurred rect (or simple hard shadow as v1: 1px offset rectangle)
- Often used together with `border-radius` on inputs

---

## Tier 3 — Parsed but weak / fallback behaviour

| Feature | Status | Site impact |
|---------|--------|-------------|
| **display: flex** | `layout_flex.c` (2022) | Major; many nav bars and search rows; bugs remain |
| **display: grid** | Parsed → `BOX_BLOCK` | Google homepage layout; no real grid algorithm |
| **position: sticky** | Parsed, limited | Sticky headers |
| **background-size/position** | partial | Hero images, sprites |
| **transform** | ❌ | Centered modals, icons |
| **transition/animation** | ❌ | Cosmetic |
| **appearance / -webkit-appearance** | ❌ | Native-looking inputs ignored |
| **::placeholder** | ❌ | Empty search box hint text |
| **:focus-visible** | ❌ | Focus styling |

Flex improvements often fix “search row layout wrong” more than new paint features.

---

## Tier 4 — Large / not realistic on 3DS soon

- Full CSS Grid
- `filter`, `backdrop-filter`
- `clip-path`, masks
- Web fonts `@font-face` beyond basic (variable fonts)
- `calc()` everywhere, container queries
- SVG/CSS blend modes in HTML layout

---

## Debugging checklist (per broken widget)

1. Save page (`wget -p`) → reproduce with `file:///`
2. Try **Request mobile site** (simpler CSS)
3. Desktop NetSurf SDL vs 3DS — same bug?
4. **Box tree dump** (`browser_window_debug_dump`, GTK: Debug box tree)
5. Log: `-v --verbose_filter="(cat:layout && level:DEBUG)"`
6. Minimal HTML isolating one property (e.g. only `border-radius`)

---

## Suggested roadmap

1. ✅ opacity + outline (paint-only, existing CSS)
2. **border-radius** (libcss + rounded redraw) — best ROI for form controls
3. Flex layout bugfixes from saved failing pages (Google, Wikipedia mobile)
4. `text-overflow: ellipsis` + `overflow: hidden`
5. box-shadow (hard shadow v1)
6. Grid layout (only if flex fixes insufficient)

Contributions should add a **minimal HTML test** under `netsurf/test/` or
`test/sites/` for each fix.

## References

- libcss properties: `libcss/src/parse/properties/properties.gen`
- Layout flex: `netsurf/content/handlers/html/layout_flex.c`
- Paint borders: `netsurf/content/handlers/html/redraw_border.c`
- Plot primitives: `libnsfb/include/libnsfb_plot.h`
- NetSurf logging: `netsurf/docs/logging.md`
