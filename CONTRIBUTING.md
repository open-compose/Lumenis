# Contributing to Cutthulhu

BUILDING.md says how to build and test; docs/ARCHITECTURE.md maps the code; docs/ROADMAP.md has the plan.

## Translations

The interface is written in English; each other language is a Qt Linguist file in `translations/`
(docs/spec/localisation.md). You need Qt's Linguist tools (BUILDING.md, "Translations").

**To translate a language that is already listed** (German, French, Italian, Irish):

1. Open `translations/cutthulhu_<code>.ts` in Qt Linguist (`linguist translations/cutthulhu_de.ts`).
2. Translate texts and mark each one done (Ctrl+Enter). A text left empty shows in English.
   * Keep placeholders as they are: `%1`, `%2`, `%n`, `%L1`.
   * Keep one `&` in a menu or button text: it marks the letter used with Alt, and may move to a letter of your word.
   * The context says where a text is: `commands` (menu items and buttons), `menus` (menu titles), `settings` (the
     Settings windows), `effects` (effect, parameter and choice names), or the window's class name.
   * Use the words editors in your language already know for the same things.
3. Build (`cmake --build --preset ci`) and start with `--language <code>` to see it.
4. Commit the `.ts` file.

**To add a language:**

1. Add a line to `translations/languages.json`: its code (ISO 639-1, as `pt` or `pt_BR`) and its name in itself.
2. Run `python3 scripts/update-translations.py`: it makes `translations/cutthulhu_<code>.ts` with every text.
3. Translate it as above. CMake builds every `.ts` in the folder; the language appears in the Interface setting.

**When you change a text in the code or the JSON resources,** run `python3 scripts/update-translations.py` and commit
the `.ts` files with your change; ctest `translations_current` fails otherwise. Translations of the old text are kept,
marked obsolete, for the translators to reuse.

**To check that a window translates,** start with `--language pseudo`: every translated text is accented and longer
("[Ŝáṽé ~~]"). Text that stays plain English was never sent through `tr()` or `translatedResource()`, and a control
that cuts the longer text is too small for German or French.

**A name the code shows** (a built-in bin column, bin view or workspace) goes in `src/ui/resources/names.json` and is
shown through `translatedResource`; a text kept in `src/model` is marked `QT_TRANSLATE_NOOP(context, ...)`. ctest
`tst_untranslated` fails on a text the user sees that was never translated.
