# Testing Cutthulhu on your Mac: step by step

Thank you for testing Cutthulhu. This guide covers four things:
* installing the app;
* getting past the macOS security warning, which is expected for a test build;
* making a separate test user, so your own account stays untouched;
* running the session with the checklist.

No technical knowledge is needed. Just follow the steps in order.

## Before you start

You need:
* **A Mac with Apple silicon (M1 or newer).** To check, click the Apple menu (top left) and choose **About This Mac**. It should say **Chip: Apple M1** (or M2, M3, M4 and so on). Cutthulhu does not run on Macs with an Intel processor.
* **macOS 12 Monterey or later.** The same window shows the macOS version.
* **Your Mac's password**, the one you type when you log in.
* **Two files from us:** the Cutthulhu app, a file ending in **.dmg**, and the checklist, a PDF.
* About an hour for the session.

## Part 1: Install Cutthulhu

Do this in your normal account.

1. Save the **.dmg** file to your **Downloads** folder.
2. Double-click it. A window opens showing the **Cutthulhu** icon and an **Applications** folder.
3. Drag the **Cutthulhu** icon onto the **Applications** folder. If your Mac asks for your password, type it and click **OK**.
4. Close the window. In the Finder sidebar, click the eject button next to **Cutthulhu**.

## Part 2: Allow Cutthulhu to open (only once)

Cutthulhu is a test build and is not yet registered with Apple. Because of that, macOS blocks it the first time. This is expected, and it is safe to allow. You only have to do it once, and the permission then works for every user on the Mac.

1. Open **Finder**, click **Applications** in the sidebar and double-click **Cutthulhu**.
2. macOS says that **Cutthulhu** was not opened, or that Apple could not verify it. Click **Done** (or **OK**). **Do not click Move to Trash.**
3. Click the Apple menu and choose **System Settings**. Click **Privacy & Security** in the list on the left.
4. Scroll down to the **Security** section. It says that **Cutthulhu was blocked**. Click **Open Anyway**.
5. Type your password (or use Touch ID) and click **OK**.
6. Double-click **Cutthulhu** in **Applications** again. If macOS asks once more, click **Open Anyway** (or **Open**).
7. The **Select Project** window appears. Cutthulhu works. Quit it for now: choose **Cutthulhu > Quit Cutthulhu**, or press **Command-Q**.

**On macOS 12, 13 or 14**, there is a shorter way. In **Applications**, hold the **Control** key and click **Cutthulhu**, choose **Open**, then click **Open** again.

**If there is no Open Anyway button, or macOS says the app "is damaged"**, use Terminal instead:

1. Press **Command-Space**, type **Terminal**, and press **Return**.
2. Copy this line exactly, paste it into the Terminal window, and press **Return**:

```
xattr -dr com.apple.quarantine /Applications/Cutthulhu.app
```

3. Nothing is printed when it works. Close Terminal, then go back to step 6 above.

## Part 3: Make a test user

A separate user on your Mac gives Cutthulhu a clean start. It also keeps the test away from your own files and settings. Cutthulhu is already installed for every user, so there is nothing more to install.

1. Click the Apple menu and choose **System Settings**, then click **Users & Groups** on the left.
2. Click **Add User…** (on older macOS, click **+**). Type your password if asked.
3. Fill in the new user:
   * **New Account** (or **New User**): choose **Standard**.
   * **Full Name**: **Cutthulhu Test**.
   * **Account Name**: macOS fills this in.
   * **Password**: something simple that you will remember. Type it twice.
4. Click **Create User**.
5. Click the Apple menu and choose **Log Out**. On the login screen, choose **Cutthulhu Test** and type its password.
6. The first time, macOS asks a few setup questions. Choose **Set Up Later**, **Skip**, **Not Now** or **Continue** for each. None of them matter for the test.

## Part 4: The session

1. In the **Cutthulhu Test** user, open **Finder**, click **Applications** and double-click **Cutthulhu**.
2. In the **Select Project** window, click **New Project**. Type a name in **Project name**, for example **Test**. Leave the format as it is and click **OK**. If the project then shows in the list, select it and click **Open**.
3. Open the checklist PDF. It opens in **Preview**. Put Cutthulhu and the checklist side by side.
4. Work through the checklist. Use no help, manuals or web searches: find everything the way you would in Media Composer.
   * Click the **Found** box when you find an entry without help.
   * Under **Where**, write where you looked first, if that was not where you found it.
   * Under **Notes**, write anything that looks or works differently from Media Composer.
   * Entries marked **later** are in their menus but greyed out, because they come in a later version. Please still find them.
5. Save the checklist often: **File > Save** in Preview keeps what you typed.
6. When you are done, quit Cutthulhu and send the saved checklist back to us.

If something goes wrong, for example a crash or a window you cannot close, write down what you did just before. Then carry on, or restart Cutthulhu.

## Part 5: Afterwards (optional)

When the test is over, you can remove everything:

1. Log out of **Cutthulhu Test** and log back in to your own account.
2. **System Settings > Users & Groups**: click the small **i** (info) button next to **Cutthulhu Test** and click **Delete User**. Choose to delete the home folder, then click **Delete User**.
3. To remove the app, drag **Cutthulhu** from **Applications** to the **Trash**.
