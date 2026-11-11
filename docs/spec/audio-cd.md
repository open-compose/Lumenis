# Audio CDs on import (9.2d)

Editing Guide p.215 "Adjusting Gain Before Importing Audio Files", p.1198 (the Import setting's Audio tab), p.176
"Capturing Audio from a Music CD". Code: `src/ext/include/lumenis/ext/audio_cd.h` (the provider hook),
`src/media/audio_cd.*` (".cda" files, discs, WAV, the Windows provider), `MainWindow::readCdTracks` in
`src/ui/main_window_graphics.cpp`, gain in `MainWindow::importClipMedia`.

## Tracks of an audio CD

"You import audio from a CD" (p.215) through Import, as any sound file. What the operating system shows of the disc
decides how (CLAUDE.md "Licensed technology": the OS first, else a provider hook):

* **macOS** mounts an audio CD as a volume of AIFF files (cddafs): they import as they are.
* **Linux** desktops show its tracks as WAV files through gvfs (`cdda://`): they import as they are.
* **Windows** shows ".cda" files of 44 bytes that hold no sound, only the track's place on the disc (a RIFF "CDDA" file:
  track number, volume serial, first sector, length in sectors). An audio CD provider (`ext::AudioCdProvider`) reads
  the track off the disc into a WAV file (44.1 kHz, 16-bit stereo), which is imported; the clip is named after the
  ".cda" file and its Source File and Source Path name it. The built-in provider on Windows reads CD-DA sectors through
  the system's CD-ROM driver (IOCTL_CDROM_RAW_READ). Elsewhere, or for a drive the system cannot read, a module can add
  one; without any, the track is left out with a Console line.

Not built: "Capturing Audio from a Music CD" (p.176) through the Capture tool's CD Player audio input, which needs
capture (not part of our plan) and "Avid input/output hardware".

## Apply attenuation/gain effect on import (p.215, p.1198)

* The Import setting's Audio tab: **Apply attenuation/gain effect on import** with the gain ("Type a decibel level from
  12 to -96") and **CD only** ("Select the CD only option if you only want to apply attenuation/gain to all of the music
  files on a CD").
* The gain becomes the clip's own gain, as Clip > Audio > Apply Gain sets it (`model::applyClipGain`, 5.7b1): "If you
  later apply gain from the Clip menu to a clip that you adjusted the gain before import, Media Composer ignores the
  pre-import gain".
* On a CD (`media::isOnDisc`, ours): a ".cda" file, a file on a volume of a disc's file system (cddafs, ISO 9660, UDF,
  CDFS) or a gvfs audio CD track.
* The setting is kept with each clip (Batch Import, docs/spec/batch-import.md).

## Tests

`tst_audio_cd`: ".cda" files written and read, which files are on a disc, WAV files FFmpeg reads; a ".cda" track read by
a provider and imported as a clip named after it; the gain with and without CD only.
