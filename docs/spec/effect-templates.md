# Effect templates (ROADMAP 6.6b1)

FX Guide p.37-38, p.52-55, p.182; Editing Guide p.474. Code: `src/model/effect_template.*`
(`EffectTemplate`, `segmentTemplate`, `transitionTemplate`, `effectTemplateItem`, `effectTemplateOf`,
`adaptedEffect`, `applyTemplate`, `applyTemplateGroup`, `promotedTemplateName`), `effectTemplateMime()` in
`bin_dock.*`, the bins' drop in `bin_view.cpp` and `BinView::promoteEffects`, `Timeline::droppedTemplate`,
`dropTemplate` and `applyTemplateToSelection`, `EffectEditor::shownTemplate`, `templateMime` and
`applyTemplateGroup`, the Effect Palette's `setTemplateBins` (filled by `MainWindow::refreshTemplateBins`), and
`MotionEffectEditor::timewarpTemplateMime`. Tests: `tst_effect_template`, `tst_effect_template_ui`, scenario
`29-effect-template.json`.

* "Media Composer lets you save an effect with its parameter settings as a template. You can then apply the
  template to other effects or video clips at any time" (p.52). Ours: an Effect item in a bin whose
  composition holds the template under "videoEffect": the effect with its parameters and keyframes, whether
  it was a transition (then its duration and alignment), the effect's length and the project's edit rate when
  saved, and with Alt its source media. The Audio EQ and AudioSuite templates (5.3, 5.5c2) are Effect items
  too, under keys of their own.
* Saving: "Drag the effect icon from the Effect Editor to a bin. A new effect template appears in the bin,
  containing the parameter setting information for the effect" (p.53); it is named after the effect (ours)
  and renamed as any item is ("click the template name below the icon and type a new name"). "Press and hold
  the Alt key ... while you drag the effect icon" saves "a segment effect with its source media" (p.53): the
  segment itself, clip and effect (and nest), without its transition and renders. Keyframe-only title
  templates wait for titles (6.8).
* The Motion Effect Editor's "Timewarp Effect icon: Drag the icon to a bin to create a Timewarp Effect template
  from the current settings in the Speed graph and Position graph" (p.182). Ours: the effect icon left of the
  graph display buttons, on while a Timewarp is shown.
* Applying: "You can apply an effect template with all its parameters directly to segments or transitions as
  you would any other effect" (p.53): dragged from the bin or the Effect Palette to the Timeline, or
  double-clicked in the Effect Palette for the selection. A segment template goes on the segment (Alt nests
  it, as for palette effects, 6.6a); a transition template on the cut nearest the drop with its duration and
  alignment, through the Insufficient Source dialog when it does not fit; a template with its source media
  is edited in over the track from the drop (an overwrite; double-clicked: at the position, on the top
  selected video track). Video tracks only.
* "To apply a specific parameter from an effect template ... drag it to the specific parameter group in the
  Effect Editor ... Media Composer applies the effect template only to the effect parameter group you
  selected" (p.54): the group's values and keyframes become the template's (its defaults where it has none).
  A group the template's effect does not have is refused (ours).
* Mixed rates: "When you apply a template, Media Composer adjusts keyframes if necessary to account for
  differences in frame rate" (Editing Guide p.474). Elastic keyframes are fractions of the effect and need
  nothing; Fixed keyframes, counted in frames (p.124), are scaled by the two rates.
* The Effect Palette: "If you save any effect templates to a bin, and have that bin open, those templates
  also display in the Effect Palette" (p.37); "Click the bin name below the effect category list to view the
  effect templates in the bin" (p.38). Ours: the open bins with templates listed below the categories,
  hidden while there are none; a bin's templates drag and double-click like effects.
* Promote Effects (p.54-55): "Right-click the template in the bin, select Promote Effects, and then click OK
  in the Promote Effects dialog box. Media Composer creates a copy of the template ... named, for example,
  MyColorEffect.Promoted.01". Our templates hold only advanced keyframes, so the promoted copy's effect is
  the same; the command is in the bin's shortcut menu while templates are selected.
* Not yet: the Quick Transitions bin's templates in the Quick Transition dialog's Add menu (FX Guide p.69)
  waits for that dialog; templates are not written to AAF or `.avb` (the export keeps bins' clips and
  sequences).
