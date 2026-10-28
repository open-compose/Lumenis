# File > Media > Unmount, File > Input > Link, Center Current and Send Current Home (UI completion pass U3b)

Pages are the Editing Guide's.

Code: `media::unmountDrives`, `remountAllDrives`, `unmountedDrives` (`src/media/managed.*`),
`UnmountDialog` (`src/ui/unmount_dialog.*`), `MainWindow::unmountDrives`,
`MainWindow::placeCurrentWindow`; test `tst_media_unmount`.

## Unmount (p.343-344)

* "By default, all media drives connected to your system are listed and can be seen by Media Composer.
  You can remove one or more drives from the list at any time by using the Unmount command."
* "Select File > Media > Unmount. The Unmount Disk or Drives dialog box opens. The list displays all
  drives currently available. Select a drive to unmount. Ctrl+click (Windows) or Command+click (Mac) to
  select additional drives. Click Unmount. The drives are no longer available to Media Composer."
  * Unmounted drives are left out of `media::mediaDrives()`, which every drive menu, the media database
    and the Media Tool use.
* "The Unmount command does not remove drives from the Windows system." Nothing is unmounted from the
  operating system; "If the drive being ejected supports the auto-eject feature, the command ejects the
  disk" is not done (ours: no media is ejected).
* "You can also remount all the drives and return them to the desktop. However, you cannot mount
  selected drives." Ours: the dialog's Mount All button; unmounted drives also come back when the
  application restarts (the list is not saved).

## Input > Link (p.315-317, p.328)

* File > Input > Link opens the Source Browser to link files, as File > Input > Import Media opens it
  to import (p.213). "File > Input > Link to Volume for Export" for AS-02 bundles (p.328) waits for
  AS-02 export (Phase 9).

## Windows > Center Current and Send Current Home (p.601)

* "To restore a resized Timeline window to its default position: Click the Timeline and select Windows >
  Send Current Home. To center a resized Timeline window: Click the Timeline and select Windows > Center
  Current."
* They act on the active panel's floating window (any panel, not only the Timeline). Center Current
  centres it on its screen at its size. Send Current Home gives it its preferred size centred on the main
  window, which is where a panel floats first (ours: the guide does not say what the default position
  is). A docked panel has no window of its own, so nothing moves.
