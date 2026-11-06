# Export to Device: XDCAM and P2 (Phase 7.2b)

File > Output > Export to Device > XDCAM and > P2 (Editing Guide p.883-884,
p.901, Export Settings: P2 and XDCAM p.1187).

## What is exported (`MainWindow::exportToDevice`)

* "If you have a sequence loaded in the Record monitor, the sequence exports
  when you select Export to Device. You can also right-click the clip or
  sequence in a bin" (p.883): the Record monitor's sequence, else the active
  bin's selected master clips, subclips and sequences, each as one clip.
* The clip is written as XDCAM MXF OP1a (7.2a, docs/spec/movie-export.md "MXF
  OP1a") in the Video Format chosen, with the audio Bit Depth chosen, through
  the Export setting's code with the dialog's values in place of the
  setting's (`exportOverride_`, as Send To does), then handed to the device's
  `deliver()`.
* Sound: the stereo mix, tracks 1 and 2 ("the system mixes down to audio
  tracks 1 and 2 during export", p.884). Ours: always so; up to 8 tracks for
  XDCAM-50 on the PDW HD1500 later. "If the target XDCAM disk you are
  exporting to already has other clips on it, you are only allowed to export
  a clip with the same number of audio tracks" (p.884): a disk whose first
  clip has another channel count is refused, said in the Console.

## The dialog (`ExportDeviceDialog`)

XDCAM Export Settings and P2 Export Settings (p.1187): Use Marks, Use Selected
Tracks (from the Export setting), Target XDCAM Disk or P2 Device, Video Format
(the device's formats this build can write), Bit Depth (16 or 24). "If you do
not connect to a P2 device or camera, the options are not available" (p.901):
without a disk or card everything is dimmed, OK too.

## Devices (`ext::ExportDeviceProvider`, lumenis/ext/export_device.h)

A provider of category ExportDevice says its kind ("xdcam" or "p2"), lists its
targets (with the audio channels of the clips already there), its video
formats and bit depths, and delivers a written OP1a file under the device's
own name.

* **XDCAM disks as folders** (`media::XdcamDiskProvider`, registered by
  `media::registerProviders`): mounted volumes whose root has a Clip folder
  beside General, Sub or Edit (the disk layout, ours by observation), and the
  folders of `media::setXdcamDiskFolders` or `LUMENIS_XDCAM_DISKS`; with
  `LUMENIS_NO_SYSTEM_DRIVES=1` only those. Formats XDCAM-50, -35, -25 and -17
  (p.884). A clip goes into Clip as the next CNNNN.MXF ("All exported clips are
  given a new sequential name of Cxxxx.mxf, for example, C0019.mxf"; on a real
  disk the deck names them, here we do). The disk's own metadata files (Sony's)
  are not written.
* **P2**: no built-in device; the P2 clip metadata (CONTENTS/CLIP XML) is
  Panasonic's and not published, so a P2 card comes from a module
  (docs/EXTENSIONS.md).

## Not yet

SD XDCAM (IMX) formats, 8 audio tracks on XDCAM-50, Aspect Ratio (p.1174),
naming the clip on the disk (firmware 1.5, p.883), a deck reached over FTP.

## Tests

`tst_movie_export_ui`: P2 without a device (dimmed, nothing exported); XDCAM onto
a folder disk twice (C0001.MXF, C0002.MXF, XDCAM-35 read back); a disk whose
clip has four audio tracks refused.
