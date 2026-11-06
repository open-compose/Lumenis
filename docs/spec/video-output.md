# The Video Output tool and audio output (Phase 7.3b)

Tools > Video Output Tool (Editing Guide p.910-917, Video Output Tool Settings
p.1225-1227) and the Audio Project setting's Output tab (p.919-924, p.1150).

## Hardware (`ext::VideoOutputProvider`, lumenis/ext/video_output.h)

A provider of category VideoIoDevice lists its devices (their Sync Lock sources,
SD and HD outputs that can be calibrated, HDMI) and opens one as a
`VideoOutputSession` that takes the sync source, the output signal, each
calibration level and pictures to show. "The Video Output tool is not available
in software-only versions of Media Composer" (p.1225): without a device the tool
says so. AJA and Blackmagic devices come as modules (7.3c).

## The tool (`VideoOutputTool`, `MainWindow::openVideoOutput`)

* **Options** tab: Sync Lock (the device's sources), HDMI Color Space and HDMI
  Format (dimmed without HDMI), Crossconvert, Downconvert, Component Format, VBI
  and the Test Patterns menu.
* **SD Cal** tab: the Output menu (Component, Composite, S Video) and the
  sliders Black, Y Gain, Hue, Sat, RY Gain, BY Gain, H Phase, SC Phase and System
  Phase; **HD Cal**: HD Component YPbPr or RGB, HD Black, HD Y Gain, Pr Gain and
  Pb Gain (p.914-917). Ours: every slider runs from -100 to 100 around its
  factory level 0; the guide gives targets on external scopes, not slider units.
* Each slider's **preset button**: "When the Video Output tool opens the first
  time ... all preset buttons are lit (green), with the factory presets loaded";
  clicking the slider of a lit preset dims it and shows the manual level;
  clicking an unlit preset lights it and loads the factory level (p.913); clicking
  a lit preset shows the manual level again (ours, so that one can "switch the
  preset buttons between the levels you set manually and the factory preset
  levels").
* **Test patterns** (`media::testPattern`): SMPTE_Bars, ColorBars (75%) and
  ColorBars_100, drawn at 1920 x 1080 and sent to the output ("Bars are displayed
  on the Client monitor", p.915). Our 75% bars keep their white at 100%; the
  SMPTE bars are drawn in RGB after SMPTE EG 1.
* The values are the project's **Video Output** setting ("Output settings are
  Project settings", p.915); Settings > Save As makes a version of that name and
  makes it active (`MainWindow::storeVideoOutput`). Opening the setting in the
  Settings window opens the tool.

## Audio output

* **Output Gain** on the Audio Project setting's Output tab, "the volume of
  global audio output" (p.1150), added to the Audio Mixer's Master fader in
  playback and applied to the Digital Cut's sound ("These adjustments affect
  levels for all output tracks to both the speakers and record devices", p.920).
* The calibration tone (p.919-920) is 5.7c1's (docs/spec/tone.md).

## Not yet

Direct Out, Stereo Mix Tracks and 16-channel output (hardware channels),
Generate LTC On Playback and LTC out during preroll (an LTC OUT connector, 7.3c),
recording bars and tone directly to a deck (p.925), the Test_Patterns files to
import (p.222).

## Tests

`tst_video_output`: the test patterns' colours; not available without hardware;
with a fake device the sync source, output and calibration reaching it; the preset
buttons; a test pattern shown; the values in the project's setting and Save As;
Output Gain.
