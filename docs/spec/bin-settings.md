# The last Bin settings (UI completion pass U3h)

Pages are the Editing Guide's.

Code: `BinAppearance::frameBorders`, `frameIcons`, `localMediaIcons`, `typeColor` and `mediaTint`
(`src/ui/bin_view.*`, drawn by the Frame view's delegate and the icon column);
`media::MediaDatabase::hasMixedResolution`; `WorkspaceManager::setExtras` (`src/ui/workspace.*`) with
MainWindow's single floating Bin Containers. Test: `tst_bin_settings`.

## Frame View (p.1154)

* "Show Border Colors: Use color based on object type displays a colored border around the following:
  Blue - Precomputes and source side motion effects; Green - Master clips; Dark Green - Subclips and Group
  clips; Red - Sequences; Purple - Media files in the Media Tool." Ours: motion and rendered effects blue,
  master and audio clips green, subclips and groups dark green, sequences red; other objects have no
  border. The Media Tool has no Frame view, so purple is not used.
* "Show Border Colors: Use clip color, assigns the same colors you assigned to clips in Text View to items
  in Frame View": the clip colour (p.264); none without one.
* "Show icons displays the applicable bin item icon": ours, in the corner of a frame with a picture, and in
  the middle of a frame without one. Off, frames without a picture are plain black.

## Show local media icons (p.1154)

* "Clip icons will appear blue to indicate the media is local or pink to indicate mixed resolution." The
  clip's icon in the Text view. Ours: local means every one of the clip's media files is there and none is
  on shared storage (`model::isSharedLocation`, docs/spec/bin-locking.md); mixed resolution means the media
  database holds video media of the clip's MobID at more than one resolution, which takes precedence.

## Save single floating Bin Containers in Workspaces (p.1153)

* "Select this option if you want a floating Bin Container that is not tabbed or paneled with other tools
  or Bin Containers to be saved into a workspace": a floating Bin Container holding one bin is kept with
  the workspace's layout (saved and last known), with its bin and place, and floats again when the layout
  is applied. Floating Bin Containers with several bins, and bins not in the layout, are left as they are
  (ours). Off, workspaces keep no Bin Containers.
