# Shared-storage bin locking (Phase 3.7)

When a project's bins are on storage several systems use, Media Composer
lets one user at a time change each bin: "The user who opens the bin first
controls the lock and obtains write access to the bin"; the others can read
it (Editing Guide p.302). Media Composer does this on Avid NEXIS. Cutthulhu
does it on any shared folder: a NAS through SMB, NFS or AFP (ROADMAP §2,
NEXIS itself is not supported).

Sources: the Media Composer Editing Guide (2025), p.300-304: "Working with
Shared Projects and Bins", "Working with Locks and Shared Bins",
"Refreshing Locked Bins" and "Restrictions and Limitations for Locked Bins".
How Media Composer stores its locks is not documented, and is not copied:
the files below are Cutthulhu's own.

## Shared storage (`model::isSharedLocation`)

"The Bin Lock Status button does not appear if the bin is not on Avid
shared storage" (p.302): locks are kept only for bins on a network file
system.
* Windows: a UNC path (`\\server\share`), or a drive letter that
  `GetDriveTypeW` reports as `DRIVE_REMOTE`.
* macOS and Linux: the file system type (`QStorageInfo`) of the nearest
  folder that exists: `cifs`, `smb3`, `smbfs`, `smb2`, `nfs`, `nfs4`,
  `afpfs`, `webdav`, `davfs`, `fuse.sshfs`, `afs`, `ceph`, `glusterfs`,
  `fuse.glusterfs`, `lustre`.
* `LUMENIS_SHARED_BINS=all` (or `1`) makes every location shared, `none`
  (or `0`) none: for tests, and for storage the check does not recognise
  (BUILDING.md).

## Files (`src/model/bin_lock.*`)

* **The lock**, `<bin>.cthbin.lck`, beside the bin: JSON with `app`,
  `user`, `machine`, `since` (UTC, ISO 8601) and `kind` (`open`, `locked`
  or `protected`). It is made with an exclusive create
  (`QIODevice::NewOnly`: `O_EXCL`, `CREATE_NEW`), which SMB and NFS servers
  carry out atomically, so two systems opening a bin at once cannot both
  get it; the one that fails reads who has it. A lock file that cannot be
  read is held by someone unknown ("?"), never taken for free.
* **Holders** are a user (the User profile's name, p.1138) on a machine
  (its host name, compared without case). A lock left by the same user on
  the same machine (a session that ended without closing the bin) is
  theirs again.
* **Kinds** (p.302): *open* is released when its holder closes the bin;
  *locked* (Lock Project Bin) stays after they close it; *protected*
  (Protect Project Bin) stays and makes the bin read only for everyone,
  its holder too. A stronger kind replaces a weaker one of the same
  holder. The holder is written "ed on EDIT-1", "ed* on EDIT-1" for
  locked and "ed** on EDIT-1" for protected, the asterisks after the user
  name as the guide describes (p.302).
* **The history**, `<bin>.cthbin.history`: a tab-separated line per event,
  appended (UTC time, user, machine, action: `saved`, `locked`,
  `protected`, `unlocked`). The guide's history "shows which computers and
  users have modified the bin and the date and time of the modifications"
  (p.301). The newest 4 MB are read.
* **The project.** "A project folder for each computer that accesses the
  project" keeps each one's own data (p.301): Cutthulhu keeps the bins each
  machine has open in `<project>/<machine>/open-bins.json`, so that users
  do not reopen each other's bins. The bin list (`<project>.cthbins`) is
  shared; a system writes it only when it changes the list itself, never
  just because it opened or closed a bin.

## In the application (`BinContainer`, `BinDocument`, `BinView`)

* **Opening** a shared bin takes its lock, or reads who holds it. A lock
  file that cannot be made (a read-only share) leaves the bin read only.
  Alt (Option)+double-click opens it "without controlling the lock"
  (p.302).
* **The Bin Lock Status button** (p.301-303), at the left of the bin's top
  row:
  * green: this user holds the lock and may change the bin; Alt+click
    saves the bin and releases the lock (p.303);
  * red: someone else holds it, or it is protected (for everyone, p.302);
  * yellow: the holder has saved the bin since it was read; the tab adds
    "(modified)" ("the bin title provides a status in parentheses", p.302).
    A click reads the bin again and turns it red;
  * blue: the holder has released the lock. A click takes it (reading the
    bin again first if it changed) and turns it green (p.303);
  * otherwise a click shows the history, with the current holder first.
* **Watching.** Every two seconds the container reads the lock files of the
  project's bins and of its open shared bins, and the bin list's time: file
  watching does not reach across network file systems. A save is noticed by
  the bin file's modification time.
* **Bin Container** (p.301-302): bins locked by another user are in bold,
  with a tooltip naming the holder (ours: the Sidebar has no column for
  it). Its right-click menu has Lock Project Bin, Protect Project Bin and
  Unlock Project Bin for the selected bins (p.302); the Clip menu has Lock
  Project Bin and Unlock Project Bin for the selected bins, else the active
  one (p.302 note). Only the holder can unlock (p.302); a user who unlocks
  a bin they still have open keeps an ordinary lock on it.
* **Read-only bins** (p.304): items cannot be dropped into one; items
  dragged from one into a writable bin are duplicated there ("a duplicate
  (not a copy)") and stay where they were; a bin someone else holds cannot
  be moved, renamed or deleted; new sequences, imports and duplicates
  cannot go into it. Ours: changes to its items are refused, and the
  Console says so, where Media Composer lets them be made and then refuses
  to save the bin to the same name (p.304); its view (columns, sorting) can
  still change, unsaved.
* **Saving** a bin re-reads its lock first: if it went to someone else
  meanwhile (two systems can both see green for a moment, p.304), the save
  is refused and the button turns red or blue. Each save goes into the
  history.

## Limits

* Two systems changing the bin list (new, renamed, moved bins) within the
  two seconds before each sees the other's change: the last write wins.
* A system that crashes leaves its open lock behind until the same user
  opens the bin again on that machine; another user cannot take it. (The
  guide gives no way to break a lock; removing the `.lck` file by hand
  does.)
* Bins in the Trash, Favorite Bins from other projects and Other Bins are
  not in the Sidebar's lock display, though they lock when they open.

## Checks

* `tst_bin_lock`: lock files (first opener, own lock taken back, kinds,
  release by the holder only, damaged files), the history, and
  `LUMENIS_SHARED_BINS`.
* `tst_bin_locking`: two users' Bin Containers on one project: green and
  red, bold and tooltip, refused changes, saves, renames and deletes; the
  yellow refresh, the blue takeover and the Alt+click release; Lock,
  Protect and Unlock; drops into and out of read-only bins; Alt+double-click;
  the shared bin list and the per-machine open bins; no button on local
  storage.
* By hand, on a real NAS from two systems (BUILDING.md).
