# Packaging and releases

## Actions and Releases

- A push to `main` or a pull request stores the platform packages in **Actions → run → Artifacts**. A manual run defaults to `none` and also produces Artifacts only. GitHub automatically expires new Artifacts after seven days. This setting applies to workflow artifacts and leaves Git history and Release assets independent of that expiration. Artifacts uploaded before this change retain their original expiry.
- Publish the 1.0.0 alpha series as GitHub pre-releases. Set `OSTF_STUDIO_ALPHA_NUMBER` in `CMakeLists.txt` to the next unused number (1, then 2, then 3). Include the matching notes under `docs/releases/`, verify the pull request on both platforms and merge into `main`.
- For each authorized publication, open **Actions → OSTF Studio cross-platform build → Run workflow**, select `main`, choose `prerelease` and enter the matching tag, starting with `v1.0.0-public-alpha.1` for **OSTF Studio Public Alpha 1**. The earlier experimental Release entries and downloads were withdrawn. Their three Git tags identify the original source revisions. The workflow creates a fresh tag on the exact commit it built. Select `stable` and a plain numeric tag only when a stable release is explicitly requested.
- After both platform jobs succeed, the Release job verifies the four binary packages and their corresponding-source archive, claims a new tag, stages a draft, checks the byte count and SHA-256 of each uploaded asset, and publishes the assets and release notes together.
- Re-run failed jobs for the original release run to reuse the exact artifacts, tag and commit. A draft for that commit can resume missing uploads. An existing published release, another tag target or a conflicting uploaded asset stops publication. Keep the chosen alpha number throughout retries.
- Before publication, check the current release and tag inventories, review the source delta since the last published tag, refresh the bilingual illustrated guides and CHANGELOG, and verify installers, project round trips and the documented limitations. After publication, fetch each asset through its release URL and compare its SHA-256 to GitHub’s asset metadata. Ordinary development builds keep the current alpha number.

The CMake project version is the numeric product version. The Git tag identifies the exact released version, including a suffix for pre-releases. Use the GitHub Actions run number (`GITHUB_RUN_NUMBER`) and run attempt (`GITHUB_RUN_ATTEMPT`) to identify CI builds and reruns. Use the GitHub Release publication date as the release date and retain the generated release notes with the assets.

| Package | File |
|---|---|
| Windows x64 installer | `OSTF_Studio_<version>_Windows_x64_Setup.exe` |
| Windows x64 portable app | `OSTF_Studio_<version>_Windows_x64_Portable.zip` |
| macOS arm64 disk image | `OSTF_Studio_<version>_macOS_arm64.dmg` |
| macOS Intel disk image | `OSTF_Studio_<version>_macOS_x86_64.dmg` |
| Matching source and build inputs | `OSTF_Studio_<version>_Corresponding_Source.tar.gz` |

The release guide checks run with `python tests/illustrated_guides.py` and `bash tests/release-version.sh`. The latter checks the alpha sequence against published release metadata and ignores drafts when calculating the next number.

## Windows install and upgrade

Setup updates the current user's application in its previous directory using the existing Inno Setup `AppId`. Select the desired categories or individual VST3s on the components page. An administrator-approved helper installs the selection in `%CommonProgramFiles%\VST3\OSTF Studio`. Its uninstall entry is **OSTF Studio VST3 Plug-ins**. To add plug-ins later, rerun Setup. Previously installed plug-ins are retained when unchecked. Save and close a DAW before updating a changed binary it has loaded. Interactive Setup offers to continue with the application alone if elevation or shared installation fails.

For unattended Setup, use `/TYPE=full`, `/TYPE=apponly`, or `/TYPE=custom /COMPONENTS="studio,composition\Ose1"` for Studio with Scale Editor. Existing scripts can continue using `/TASKS=sharedvst3` or `/MERGETASKS=!sharedvst3`. Both installers write logs to the installing account's temporary directory.

After a successful shared install, Setup checks the class IDs of previous per-user bundles and moves recognized copies to `%LOCALAPPDATA%\OSTF Studio\Previous VST3`. User settings, score files and third-party plug-ins remain outside the installers' inventories. The Portable ZIP keeps the plug-ins beside the application. Windows may need the Microsoft Visual C++ x64 Redistributable for binaries built with MSVC.

To package the Windows Release build, use Python 3 and Inno Setup 6. Run the command below from the repository root. Use `-Python` and `-InnoCompiler` to select alternate executables.

```powershell
./packaging/windows/Package.ps1 -AppVersion 0.1.0-preview -BinaryVersion 0.1.0
```

Review [Inno Setup licensing](https://jrsoftware.org/inno-setup/downloads) before commercial use of the packaging workflow.

To build the Windows installer for trying the workstation VST3, pass `-ForDAW` to `Package.ps1`. The output is `OSTF_Studio_VST3_Windows_x64_Setup.exe`. Both Windows installers start with the application and every plug-in selected, including on a repeat interactive run. Users may choose the `daw` preset or pass `/TYPE=daw` for just the workstation plug-in and the demo devices. The plug-in list comes from the production session in the embedded Paper Lantern Replies project. The shared installer retains the same component IDs and VST3 locations for upgrades.

The Windows alpha enables ASIO under the GPLv3 SDK option and also supports WASAPI. Drivers are supplied separately by the hardware manufacturer. The macOS build uses Core Audio. Keep the license texts, upstream notices and the corresponding source available alongside every release. [Distribution licenses](../licenses/NOTICE.md)

Windows packages currently have no Authenticode signing configured or verified. Validate the installer and portable executable on Windows independently of compilation and archive verification.

## macOS signing and notarization

Run **Install OSTF Studio and VST3.pkg** in the DMG. The customization page lets the user choose which VST3s to install in `/Library/Audio/Plug-Ins/VST3`. The application goes in `/Applications`. Dragging the app to Applications is also available. To build a native DMG, use `python3 packaging/macos_package.py <numeric-version> --arch arm64` or `--arch x86_64` on the matching Mac runner. The packaging workflow checks the selection of Scale Editor with its required Studio application and then installs all plug-ins and verifies the installed native binaries with lipo.

Both installers read the effect and instrument catalogues under `src/plugins`. Set a plug-in's optional `installerGroup` to `effects`, `instruments`, `composition`, `learning`, `utilities`, or `games` when its user-facing category differs from its catalogue. Categories appear when they contain at least one plug-in. Set `requiresStudio` to `true` for plug-ins that require a Studio project host. Scale Editor uses this flag. Select the workstation VST3 as well for a nested DAW installation. Host integration, a shared code library or an optional drag target does not require installing the application.

The current DMG contains a preview `.app` with no Developer ID signature or Apple notarization. For signed distribution:

1. Obtain a Developer ID Application certificate and import it into a protected build keychain. Store the signing identity and notarization credentials outside the source tree.
2. Sign the app and any nested code from the inside out with Developer ID, a secure timestamp and the hardened runtime. Apply only the entitlements the app needs, then verify the signed bundle with `codesign`.
3. Build the DMG from the signed app and sign the DMG. Submit it using `xcrun notarytool submit <dmg> --keychain-profile <profile> --wait`. Review the notarization log if the submission fails.
4. After acceptance, run `xcrun stapler staple <dmg>` and `xcrun stapler validate <dmg>`. Check the downloaded image with Gatekeeper on a Mac and launch the app from Applications before publishing.

[Apple notarization documentation](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
