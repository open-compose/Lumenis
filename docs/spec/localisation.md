# Localisation (10.4)

Ours: the guides describe only the English interface. The owner's languages are English (the default and the source),
German, French, Italian and Irish; adding another must stay easy for us and for users (a `.ts` file and a line in a
list).

## How a text reaches the screen

* **The code's texts** are in `tr()` (or `QCoreApplication::translate`), in English. Qt Linguist's `lupdate` finds them.
* **The JSON resources' texts** are translated where they are shown, never where they are read, because many are also
  identifiers (a setting's kind, a stored effect name, a Command Palette tab): `translatedResource(context, text)` in
  `src/ui/translation.h`, with the contexts
  * `commands`: commands.json's `text` (`CommandRegistry`, so menus, buttons, the Command Palette and Find Command);
  * `menus`: menus.json's `title` (`menu_builder.cpp`);
  * `settings`: settings.json's setting names, tabs, labels, suffixes, special texts, reset buttons and choices
    (`settings_window.cpp`, `SettingKind::title`);
  * `commandPalette`: its tab names;
  * `binColumns`, `binViews`, `workspaces`: the built-in bin column headings, bin views and workspaces, listed in
    `src/ui/resources/names.json` (the bin, Choose Columns, Find, the Source Browser, the workspace bar and menu);
    `tst_untranslated` checks the list against `model::binHeadings`, `builtInBinViews`, `SourceMediaModel::columns`
    and `WorkspaceManager::builtinNames`;
  * texts kept in the model (the calculator's formats, the tracking formats) are marked `QT_TRANSLATE_NOOP(context,
    ...)` where they are written, so lupdate finds them, and shown through `translatedResource` in the `calculator`
    and `tracking` contexts;
  * `effects`: effect, category and parameter names and their choices, through `model::shownName`
    (`src/model/shown_names.cpp`), after its renames (docs/spec/trademarks.md). The texts are extracted as renamed, so
    a translator never sees, and never brings back, a name we renamed.
* User data (bin, clip, sequence and track names, markers, titles) is never translated.

## The files

* `translations/languages.json`: the languages, `{"code", "name"}` with the name in its own language.
* `translations/cutthulhu_<code>.ts`: one Qt Linguist file a language, every text with its translation (empty until
  translated; an untranslated text shows in English).
* `scripts/update-translations.py` runs `lupdate` over `src/` and `app/` and over the JSON resources' texts (written
  for it as `QT_TRANSLATE_NOOP` lines in a temporary file) into every `.ts`, keeping translations; a text that is gone
  is marked obsolete with its translation kept. `--check` compares the messages each file would have with those it has
  (not the bytes: Qt versions write the same file differently); ctest `translations_current` runs it, so a new text is
  not forgotten.
* CMake (`src/ui/CMakeLists.txt`) compiles each `.ts` with `lrelease` into `:/lumenis/translations/cutthulhu_<code>.qm`
  when Qt's Linguist tools are found (`find_package(Qt6 COMPONENTS LinguistTools)`); without them the build is English
  only and says so, and `LUMENIS_REQUIRE_TRANSLATIONS=ON` (release builds) makes them required.

## Choosing the language

* The Interface setting's **Language (when Cutthulhu starts again)** (General tab; ours), its choices English, the
  languages built in and the pseudo-locale (`"choicesFrom": "languages"`). Changing it says once that it takes effect
  at the next start; the choice is also kept outside the user's settings (`QSettings`, `interface/language`), since the
  language is needed before a user is chosen.
* At start-up (`app/main.cpp`): `--language <code>`, else `LUMENIS_LANGUAGE`, else the saved choice, else English.
  Tooling modes (`--screenshot`, `--dump-menus`, `--smoke-test`, `--soak` ...) start in English unless `--language`
  is given. `installLanguage` installs our translation and Qt's own (`qtbase_<code>.qm`, the standard buttons and
  dialogs, where the Qt installation has it) and sets the default locale (numbers and dates in the language's way).
* **The pseudo-locale** (`--language pseudo`): every translated text accented, about 40% longer and in brackets
  ("[Ŝáṽé ~~]"), placeholders, mnemonics, tags and entities kept. Text that stays plain was never translated; a
  control too small for a longer text shows cut. It needs no file (`PseudoTranslator`).

## Adding a language

CONTRIBUTING.md, "Translations": a line in `translations/languages.json`, `python3 scripts/update-translations.py`,
then the new `.ts` translated in Qt Linguist (or any XLIFF/TS tool) and committed.

## Parts

* 10.4a, the infrastructure above (`src/ui/translation.*`, test `tst_translation`, ctest `translations_current`), is done.
* 10.4b, the check, is done: `tst_untranslated` starts in the pseudo-locale, opens the main window with a project and
  every window of the Tools menu (answering any question they ask first), and reads what the widgets show: labels,
  buttons, group boxes, placeholders, tabs, combo box items, headings, menus, window titles and tooltips. A text with
  a plain word of three letters or more left once the bracketed parts are taken out was never translated. Names that
  are the same in every language pass: the product, units, and widgets that show names (the List Tool's output
  formats, the Source Browser's folder crumbs, input devices, drive paths: `kNameWidgets`). Its first run found 72
  texts; they are translated. It found a bug too: the Composer's button tooltips showed command ids, the button
  mapper having no command registry. Windows outside the Tools menu (dialogs, the Settings windows) are checked by
  the pseudo-locale's screenshots (10.4c).
* 10.4c, pseudo-localised screenshots, is done: every CI job's Screenshots step also takes `--screenshot shots/pseudo
  --language pseudo`, and ui-consistency compares them with Linux's like the others (the text-free images within
  0.5%; BUILDING.md "CI screenshots"). They show what the check above cannot: text cut by a control too small for
  it (the bin tab's "Untitled", a Timeline setting wider than its window), for the translators and for 10.4d. The
  Timeline settings' screenshot is now 800 pixels tall: at 580 its radio groups were squashed out of sight.
* 10.4d, the translations, is in two parts. 10.4d1: German, French and Italian drafted by Claude (the owner's
  decision, 2026-11-13) with the words editors in each language already use, every text of each file translated and
  left "unfinished" in Qt Linguist until a native speaker has reviewed it (lrelease builds unfinished texts in, so the
  drafts show). Each language's terms, and why, are in `translations/glossary_<code>.md`; a reviewer starts there.
  Checked by a script: placeholders, tags, Alt-key letters, tabs, newlines and spacing as in the English. 10.4d2,
  Irish, the same way (its terms from téarma.ie, Irish software translations and TG4/RTÉ, coined ones marked in its
  glossary; three plural forms, n = 1, n = 2 and the rest), is done. Irish's longer "Transcription On" showed a layout
  fault: the Sidebar's stretched Name column was squeezed to nothing. The Sidebar now sizes its columns itself, the
  names first with at least 60% of its width (`SidebarTree::fitColumns` in `bin_container.cpp`, test
  `tst_untranslated longerTextsLeaveBinNamesRoom`).
  Qt's own texts (the standard buttons) come from Qt's translations (`qtbase_<code>.qm`), which a Linux build has only
  with the distribution's Qt translations package (`qt6-translations-l10n` on Ubuntu).
