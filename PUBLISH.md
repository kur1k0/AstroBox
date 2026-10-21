# Application Store Publish Record

Keep this file in the application project root. Update it when package
metadata, listing copy, assets, privacy choices, or submission status changes.

Portal: <https://dev.cardputer.cc/#/upload>

## Package

- Debian package path: `build/cardputerzero-astro-box_1.0.0-m5stack1_arm64.deb`
- Package: `cardputerzero-astro-box`
- Version: `1.0.0`
- Architecture: `arm64`
- Maintainer: `kur1k0 <fuso@duck.com>`
- APPLaunch desktop path: `usr/share/APPLaunch/applications/cardputerzero-astro-box.desktop` (source: `applications/cardputerzero-astro-box.desktop`)
- Executable path: `usr/share/APPLaunch/bin/M5CardputerZero-AstroBox`

## Source and Ownership

- Public source repository: <https://github.com/kur1k0/AstroBox>
- Expected GitHub uploader/owner: `kur1k0`
- Hide uploader email: `true`

## Listing

- Application name (store title, unique store-wide): `Astro Box`
- One-line summary (maximum 80 characters): `Astro Box lets you create, explore, and share interactive N-body universes.`
- Description: `Create stars and planets, define their mass, color, and initial velocity, then watch gravity shape their paths in real time. Explore built-in orbital systems, pause or accelerate the simulation, follow individual bodies, and share complete universes with other CardputerZero users through QR codes.`
- Categories (1-2 from the fixed enum): `Games`
- License (SPDX id): `MIT`
- Author display name: `kur1k0`
- Share code (4 letters/digits, unique store-wide): `ASTR`
- Permissions (seven booleans): `camera=true` (QR scanning through libcamera); `microphone=false`, `imu=false`, `network=false`, `additional_hardware=false`, `background_service=false`, `external_display=false`
- Locales: none (English listing only)

## Assets

- 256 x 256 source/store icon: `icon-256.png` (downscaled from the `icon.png` 1080 x 1080 master)
- Launcher icon packaged in the `.deb`: `usr/share/APPLaunch/share/images/astro-box.png` (100 x 100, referenced by `Icon=` in the `.desktop`)
- Screenshot 1 (320 x 170): `screenshots/figure-8.png`
- Screenshot 2 (320 x 170): `screenshots/solar-system.png`
- Screenshot 3 (320 x 170): `screenshots/milky-way.png`
- Screenshot 4 (320 x 170): `screenshots/create-body.png`
- Optional screenshot 5 (portal supports up to 6): not used
- Optional screenshot 6 (portal supports up to 6): not used

## Preflight

- [x] `.deb` exists and can be parsed.
- [x] Control fields contain Package, Version, Architecture, and Maintainer.
- [x] Architecture is `arm64`.
- [x] APPLaunch `.desktop`, executable, and PNG icon exist inside the package.
- [x] Package name ownership and update version are valid (new package name; no current owner).
- [x] Listing copy and categories have been reviewed.
- [x] Store metadata passes `store_meta_policy.py`: title/summary/categories/screenshots/icon/license/author/share_code/permissions.
- [x] 1-6 clean 320 x 170 screenshots exist (four provided).
- [x] `.deb` passes `install_path_policy.py` (no disallowed paths, no root system service).
- [ ] No credentials, tokens, or private server details are embedded.
- [ ] Previously published `meta.json` UUID acknowledged (n/a: first release).
- [ ] Real-device demo video recorded and ready to post in the PR (required for a first release).
- [ ] The user explicitly approved external submission.

Last verification: `cardputer-app-publish/scripts/prepublish_check.py --deb build/cardputerzero-astro-box_1.0.0-m5stack1_arm64.deb --app-dir .` passed (store policy scripts fetched and run).

## Submission Result

- Status: `not submitted`
- Submitted at: TODO
- Portal message: TODO
- Actions URL: TODO
- Tracking or pull-request URL: TODO
