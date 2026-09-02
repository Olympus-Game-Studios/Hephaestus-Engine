# Localization audit

**Maturity: Level 0 — Missing**

No string tables, locale fallback, font atlas pipeline, or RTL preview. HUI text is authored literals. Lua strings are literals.

**Readiness:** English-only prototypes fine. Shipping in multiple languages requires a new pipeline (tables + HUI text ids + font).

**Necessary now?** No. HUI/settings should use **ids** when those widgets exist so loc can be added without rewriting UI.

**Recommended:** Do not build i18n tools yet. Avoid baking English into C++ UI where Lua/HUI can hold strings.
