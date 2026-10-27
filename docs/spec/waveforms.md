# Audio waveforms in the Timeline (UI completion pass U1a)

Audio Data > Waveform, the Waveform Options, the plot's size, Show Marked
Waveforms, mismatched sample rates and the channel order of surround tracks.
Pages are the Editing Guide's.

Code:
* `src/media/waveform.*`: `media::Waveform`, `WaveformCache`.
* `TimelineView::paintWaveform` in `src/ui/timeline.cpp`.
* The Fast menu items and `Timeline::waveformChannels`.

## The peaks (`media::Waveform`, `WaveformCache`)

* "Audio waveforms in the Timeline display a sample plot of the entire
  amplitude of the track" (p.677). For each channel of a file's audio
  stream, the cache keeps the lowest and highest sample of every 256 at the
  file's rate, as signed bytes. It also keeps a coarser level of every 16 of
  those, for zoomed-out views. Peaks are rounded outwards, so the plot is
  never smaller than the sound.
* **Worked out once, on a thread of its own** (`WaveformCache`). The Timeline
  draws what is ready and draws again when more is.
  * **Ours:** nothing waits for the plot, so there is no redraw to cancel
    with Ctrl+period (p.678).
* **Kept on disk:** "Media Composer saves cached waveforms for projects. This
  allows the waveform to draw faster the next time you open the project. You
  will see a WaveformCache folder in the Avid Projects directory" (p.677).
  * Ours is the `WaveformCache` folder in the project's own folder.
  * Each file (`.wfm`, our own format) records its source file's size and
    modification time. A changed source is worked out again.
* A file that cannot be read is not tried again until the project changes.

## Showing them (p.677-679)

* **Audio Data > Waveform** (the Timeline Fast menu) shows waveforms in every
  audio track. Audio rows are taller while it is on, as for the Volume and
  Pan graphs (ours).
* **Multichannel tracks.** "Waveforms for multichannel tracks in the Timeline
  display waveforms for all channels within a single track, separated by a
  horizontal divider" (p.677).
* **Waveform Options** (Fast menu, p.679):
  * **Source Levels:** "Waveforms reflect the original recording levels."
  * **Output Levels:** "Waveforms reflect changes affecting output volumes,
    such as Clip Gain, Volume, Rendered AudioSuite Effects, and Transitions."
    **Ours:** the clip gain and volume automation (`model::volumeAt`) for
    now; rendered AudioSuite effects and transitions do not change the plot
    yet.
  * **Full Height:** "Visually extends audio peaks to assist with dialogue
    editing and syncing." **Ours:** 48 dB below full scale up to full scale
    spread over the half height.
* **The plot's size.** "To enlarge the size of the waveform plot image without
  enlarging its track, press ... Ctrl+Alt+L. ... To reduce the size of the
  sample plot image without reducing its track, press ... Ctrl+Alt+K" (p.678).
  * These are the commands `timeline.enlarge_waveform` and
    `timeline.reduce_waveform`, also on the Command Palette's Tracks tab
    (ours).
  * Each step doubles or halves the plot, from 1/8 to 16 times (ours).
* **Show Marked Waveforms** (Timeline settings, p.1218; p.677): "draws
  waveforms between an IN point and an OUT point instead of over the entire
  composition."
* **The plot's colour** is black.
  * With Show Mismatched Sample Rates as Different Color (Audio Project
    settings), the plot of clips at the project's rate is black and that of
    other rates white: "the sample plot of these clips (48 kHz) is displayed
    as black, and the sample plot of all other clips with different sample
    rates (32 kHz and 44.1 kHz) is displayed as white" (p.680).
* **Display Channel Order** (Audio settings, p.1148) orders the channels of 5.1
  and 7.1 tracks top to bottom (`Timeline::waveformChannels`). The orders are
  those the Monitor Mix Format menu names (p.689):
  * SMPTE: 5.1 L R C Lfe Ls Rs, 7.1 L R C Lfe Lss Rss Lsr Rsr;
  * Pro Tools: 5.1 L C R Ls Rs Lfe, 7.1 L C R Lss Rss Lsr Rsr Lfe.

## Later

* The Track Control panel's Waveform buttons, Audio Data > Allow Per Track
  Settings (p.677-679, p.602) and Enlarge and Reduce Track (Ctrl+L, Ctrl+K,
  p.678) came with U1b1 (docs/spec/track-control.md).
* Waveforms in the Sequence Map (Timeline settings, p.1219), with the
  Sequence Map (U1d).
* Waveforms of rendered AudioSuite effects and transitions in Output Levels.

## Tests

* `tst_waveform` (media):
  * the peaks of a known sound and the coarse level;
  * the cache file, refused for another key or a changed source;
  * the cache's thread and folder.
* `tst_waveform_ui`:
  * the Fast menu's Waveform and Waveform Options;
  * the plot's size and its limits;
  * Output Levels and Full Height;
  * mismatched rates in white;
  * Show Marked Waveforms;
  * the project's WaveformCache folder;
  * the channel order in both settings.
