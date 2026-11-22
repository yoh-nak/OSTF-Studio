# Packaging and releases

## Actions and Releases

- A push to `main` or a pull request stores the platform packages in **Actions → run → Artifacts**. A manual run defaults to `none` and also produces Artifacts only. GitHub automatically expires new Artifacts after seven days. This setting applies to workflow artifacts and leaves Git history and Release assets independent of that expiration. Artifacts uploaded before this change retain their original expiry.
- For an authorized release, set the numeric version in `CMakeLists.txt` and merge it into `main`. Open **Actions → OSTF Studio cross-platform build → Run workflow**, select `main`, and choose `stable` with a tag such as `v0.1.0`, or `prerelease` with a tag such as `v0.1.0-preview.1`. The workflow creates the tag on the exact commit it built. Pushing a tag by itself does not publish a Release.
- After both platform jobs succeed, the Release job verifies the three files, claims a new tag, and publishes the assets and GitHub-generated release notes together. Existing tags, Releases, and assets are preserved. A duplicate tag or Release blocks publication.
- If an upload fails after the tag is created, the tag and any unpublished draft are retained for inspection. Use a new approved version tag for a fresh automated attempt. To use a new stable version tag, first update the numeric version in `CMakeLists.txt`.

The CMake project version is the numeric product version. The Git tag identifies the exact released version, including a suffix for pre-releases. Use the GitHub Actions run number (`GITHUB_RUN_NUMBER`) and run attempt (`GITHUB_RUN_ATTEMPT`) to identify CI builds and reruns. Use the GitHub Release publication date as the release date and retain the generated release notes with the assets.

| Package | File |
|---|---|
| Windows x64 installer | `OSTF_Studio_Windows_x64_Setup.exe` |
| Windows x64 portable app | `OSTF_Studio_Windows_x64_Portable.zip` |
| macOS arm64 disk image | `OSTF_Studio_macOS_arm64.dmg` |

## Windows install and upgrade

Setup installs for the current user. It uses the same Inno Setup `AppId` for every version and looks up the previous installation directory to update in place. The installer tracks its own executable and Start menu shortcut. User settings, `.ostf` and `.ostfp` files, and third-party plugins are outside that inventory and survive setup and uninstall. The Portable ZIP contains the app executable and can be extracted to a user-selected directory. Windows may need the Microsoft Visual C++ x64 Redistributable for binaries built with MSVC.

To build the packages on Windows after the Release GUI build, install Inno Setup 6 and run this from the repository root. Use `-InnoCompiler` to give the script an alternate path to `ISCC.exe`.

```powershell
./packaging/windows/Package.ps1 -AppVersion 0.1.0-preview -BinaryVersion 0.1.0
```

Review [Inno Setup licensing](https://jrsoftware.org/inno-setup/downloads) before commercial use of the packaging workflow.

Review the [Steinberg ASIO license options](https://www.steinberg.net/developers/) and the bundled JUCE ASIO notices before distributing Windows builds with the ASIO backend. Set `-DOSTF_STUDIO_ENABLE_ASIO=OFF` for builds that exclude it. Device drivers are installed separately by the user.

Windows packages currently have no Authenticode signing configured or verified. Validate the installer and portable executable on Windows independently of compilation and archive verification.

## macOS signing and notarization

The current DMG contains a preview `.app` with no Developer ID signature or Apple notarization. For signed distribution:

1. Obtain a Developer ID Application certificate and import it into a protected build keychain. Store the signing identity and notarization credentials outside the source tree.
2. Sign the app and any nested code from the inside out with Developer ID, a secure timestamp and the hardened runtime. Apply only the entitlements the app needs, then verify the signed bundle with `codesign`.
3. Build the DMG from the signed app and sign the DMG. Submit it using `xcrun notarytool submit <dmg> --keychain-profile <profile> --wait`. Review the notarization log if the submission fails.
4. After acceptance, run `xcrun stapler staple <dmg>` and `xcrun stapler validate <dmg>`. Check the downloaded image with Gatekeeper on a Mac and launch the app from Applications before publishing.

[Apple notarization documentation](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution)
