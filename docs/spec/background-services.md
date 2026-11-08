# Background services and Dynamic Media Folders (8.4)

Editing Guide p.371-377. 8.4a (done): the background services, the Background Queue following them and the Timeline's
progress indicator. 8.4b: Dynamic Media Folders and their profiles, watched for new files. 8.4c: the Dynamic Media
Folders window, the Profile Editor and Acquire.

## The background services (p.376-377, 8.4a)

"Media Composer installs a Background Transcode service, a Background Render service, and a Dynamic Media Folder
service. These services are off by default." Ours: the services are the Background Queue's state
(`BackgroundQueue::ServiceState`), in the application; there is no separate service process and no taskbar or menu bar
icon (the guide's "Avid Background Services Manager"). Jobs (background Consolidate/Transcode, renders, transcription,
later Dynamic Media Folders) start only while the services are Active; a job already running finishes.

* Tools > Background Services (`BackgroundServicesDialog`, `MainWindow::backgroundServices`): the state ("Active,
  Inactive or Paused"), Start ("Immediately starts the background services if they are currently stopped"), Stop, Pause
  ("for the specified amount of time"; ours: in minutes, 10 to start with), Always Start Avid Background Services at
  Launch, Always Stop Avid Background Services upon Exit and Enable Pausing of Background Service. Start, Stop and
  Pause act at once; OK keeps the three options in the user's Interface setting (keys outside its schema).
* Ours: Always Start ... at Launch is on to start with, so that the background work earlier steps queue runs as before;
  the guide's services are off by default.
* Pausing (p.377): explicit, or "automatically performed during critical UI operations such as playback" (ours:
  while the Composer plays); "If the pause time period ends and you are in the middle of a play operation, the pause
  time period is extended" (ours: checked again each second). Unchecking Enable Pausing and clicking OK clears the pause.
* Always Stop ... upon Exit: the services stop as the main window closes, so that no waiting job starts then (ours: they
  end with the application anyway).

## The progress indicator (p.375-376, 8.4a)

`BackgroundIndicator` in the Timeline's toolbar (ours: before the Quick Find field), drawn by us after the guide's table:
spinning while background processing is in progress; a solid green dot when Dynamic Media Folder assets wait to be
acquired (8.4c); grey when the services are stopped; yellow when paused; not spinning and no dot when idle; a yellow
triangle after an error. Its right-click menu: Dynamic Media Folders (8.4c) and Generate Error Report in Console, which
writes each failed job since the last report to the Console and clears the triangle ("The yellow triangle will remain
until you generate an error report").

## Tests

* `tst_background_services`: jobs waiting while stopped and paused, a timed pause ending, the pause extended during play,
  Enable Pausing off, the error report; the indicator's states; Tools > Background Services in the application with its
  options kept and applied for the next user's settings.
