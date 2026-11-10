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

## Dynamic Play Forward and Reverse (U2e4; p.401-402, p.1219)

* "You can use the Dynamic Play Forward and Dynamic Play Reverse buttons to adjust play speeds at
  smaller increments than with the usual J and L keys. These buttons are in the Play tab of the Command
  Palette" (p.401): commands `play.dynamic_play_forward` and `play.dynamic_play_reverse`,
  `Composer::playDynamic`.
* "The Dynamic Play Acceleration default is 3 steps per keystroke or button press. A total of 12 steps
  will double the play speed" (p.402): the speed is 2^(steps/12) times normal.
* "The Dynamic Play Forward and Reverse commands start play at normal speed (1x speed). If you press
  Alt ... while you tap the Dynamic Play keys, start play starts at a lower speed. The Slow Start Speed
  setting is the number of keystrokes it will take to get to 1x speed" (p.402): with Alt, play starts
  Slow start speed x Acceleration steps below normal. "If you change the acceleration to a higher
  number, your start play speed will get slower" follows.
* The two Timeline settings (Display tab, p.1219) are read by `MainWindow::applySettings` into
  `Composer::setDynamicPlay`.
* Ours: pressing the other direction starts again at normal speed, as J and L do; the speed stops
  rising at 8 times normal (the top J and L speed, p.401); sound plays only at whole multiples of
  normal speed, and between them play is silent, as slow play is; the Play button shows the rate.

## Mouse Jog and Mouse Shuttle (8.7d2; p.402-403, p.675)

* "Press the N key to activate mouse control for jogging. Press the semicolon (;) key to activate mouse control for
  shuttling", or the Mouse Jog and Mouse Shuttle buttons on the Play tab (p.402-403): `play.mouse_jog` and
  `play.mouse_shuttle` through `Composer::startMouseControl` on the active monitor. While on, an application event
  filter (`MouseControlFilter` in composer.cpp) feeds the mouse's movement to `Composer::mouseControlMoved`.
* "Move the mouse to the right to jog or play forward or to the left to jog or play backward" (p.403). Jog: a frame for
  every `kJogPixels` (4, ours), so digital audio scrub follows each step. Shuttle: `kShuttleFpsPerPixel` (1, ours)
  frames a second for each pixel moved since shuttling started or paused; "playback rates using the mouse do not occur
  at fixed increments. They can vary all the way from 1 to 300 fps" (p.675). Whole multiples of normal speed play with
  sound (smooth audio scrub up to three times, as J-K-L); the speeds between play on the wall clock without sound, as
  Dynamic Play.
* "To pause shuttling with the mouse: Click the mouse button. To quit jogging or shuttling with the mouse: Double-click
  the mouse button or press the space bar" (p.403); "or the Escape key" (p.675). Clicks while mouse control is on go to
  it, not to the window under the mouse.
* "if you are shuttling with the mouse and you press the L key, the playback speeds up to the next normal play rate"
  (p.403): `Composer::shuttleFaster`, 1, 2, 3, 5 or 8 times normal; the mouse goes on from there.
* The Frame view's represented frame by jog and shuttle (p.251) is the Frame view's own (8.7d3, `BinFrameView`).

Test: `tst_composer_display mouseJogAndShuttle`.

## Display During Segment Drag (U2e4; p.617-618, p.1219)

* "Position Bar: When this option is selected, Media Composer shows the frame at the blue position bar
  when dragging a segment" (p.1219).
  * Ours: the Record monitor shows the sequence's frame where the dragged segments' head would go.
* "Show Four-Frame Display: ... shows the head and tail of incoming or outgoing frames of video when you
  drag a segment" (p.1219). "The Source and Record monitors change to a four-frame monitor display. The
  two outer frames update while you drag the segment ... A centered numeric offset counter appears below
  the frame monitors. The offset counter tracks the number of frames ... that you move" (p.617). "The
  two outer frames indicate ending and beginning frames of clips before and after the segment. The two
  inner frames represent the start and end frames of the segment" (p.618). "When you drag segments with
  only the Record monitor displayed, the interface changes to a two-frame display. Only the outer two
  frames in the four-frame display appear in the Record monitor" (p.617).
  * `Timeline::showSegmentDrag` and `Composer::setDragDisplay`. The outer frames are the frame before
    where the head would go and the frame after where the tail would end, in the sequence as it is
    (ours: before the move is made). The counter counts frames; feet+frames for 24p and 25p projects
    wait for film (Phase 9).
* "None: When selected, nothing is displayed in the Composer Window when dragging a segment in the
  Timeline" (p.1219): the monitors stay as they are.
