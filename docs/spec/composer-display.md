# The Composer settings' Window tab (UI completion pass U2e3)

Data Display at Top, Tick Marks in Position Bars and the middle Composer button panel (Composer
settings, Window tab, p.1158-1159). Pages are the Editing Guide's.

Code:
* `Composer::Display` (`twoRows`, `flowData`, `tickMarks`, `middlePanel`), `showsSecondRow`,
  `setSecondTracking`, `middlePanel`, the "middle" key of `buttonLayout` (`src/ui/composer.*`); read
  from the settings by `MainWindow::applySettings`.
* Test: `tst_composer_display`.

## Data Display at Top (p.380, p.384-385, p.1158)

* "Off ... turns off the information display above the monitors"; "Always Display One Row of Data
  ... one row of tracking, duration, and clip or sequence title information"; both were built in 1.5a.
* "Always Display Two Rows of Data — ... a second row of information above the first row. The second
  row includes Fast Menu icons, duration information, and, when applicable, ganging and multicamera
  icons" (p.1158).
  * "If you select the option for two information rows above the monitors ... you can display two
    different types of tracking information for the footage in each monitor. For example, you can
    display both running timecode and IN to OUT durations" (p.385). Each monitor's second row has its
    own tracking display with the same Tracking Information menu; it starts at None like the first
    ("By default, the tracking information area displays no data until you select a tracking
    format", p.385).
  * Ours: the second row's layout. Over the Source monitor: the Fast Menu, the gang icon (shown while
    the monitors are ganged), then the tracking display above the first row's. Over the Record
    monitor: the tracking display, then the Fast Menu at the right. The Fast Menu opens the monitor's
    right-click menu; its picture is the bin's Fast Menu picture. Multicamera icons wait for
    MultiCamera (Phase 7).
* "Flow Data Dynamically — ... adjusts the display of information above the monitors as you resize
  them" (p.1158); "the data above the monitors displays in either one or two rows, depending on the
  size of the Composer window" (p.380).
  * Ours: two rows while the Composer is at least 420 pixels high (`Composer::kFlowHeight`), one row
    below that.

## Tick Marks in Position Bars (p.1159)

* "When this option is selected, Media Composer switches the display of tick marks (duration
  indicators) that appear incrementally along the position bars ... When this option is deselected,
  the tick marks are invisible." Off by default (ours, 1.5a).
* Ours: a tick every 1, 2, 5, 10 or 30 seconds, or 1, 5, 10, 30 or 60 minutes of the loaded item:
  the first of these that leaves at least 6 pixels between ticks. Before U2e3 the bars always drew
  24 evenly spaced ticks; they now draw none unless the option is on.

## The middle Composer button panel (p.1159)

* "When this option is selected, an additional 3 or 6 button panel is displayed between the Source
  and Record monitors, depending on the number of button rows enabled for the Composer window. It is
  activated by default for the 'Media Composer Classic' user profile."
  * Three buttons per button row (Button Display at Bottom), level with the rows, between the
    monitors. Hidden with the button rows in Trim mode.
  * Ours: the buttons: Lift, Extract and Trim Mode; Replace, Fit to Fill and Copy to Clipboard. They
    are user-selectable like the monitors' buttons (p.96-98) and saved with the Composer settings'
    button layout under "middle".
  * The "Media Composer Classic" user profile does not exist here; the option is off by default.
