# Releasing ScreenSense

Steps for publishing a new version. Everything up to step 4 happens on your
own computer; steps 4 to 7 publish under your accounts.

## 1. Check

```sh
cmake --build build && ctest --test-dir build
scripts/test-gnome-headless.sh --with-daemon
```

Use the build for a few days yourself before releasing.

## 2. Set the version

Change it in three places:

- `CMakeLists.txt`: `project(screensense VERSION x.y.z ...)`
- `gnome-extension/screensense@aravindedakkot.gmail.com/metadata.json`: `version-name`
- `debian/changelog`: add an entry at the top, with the same version

## 3. Build the packages

Each `.deb` must be built on the release it is for, because it links against
that release's OpenCV. The script does this in a clean container:

```sh
scripts/build-deb.sh ubuntu:24.04
scripts/build-deb.sh ubuntu:26.04
scripts/build-deb.sh debian:12
```

Each package lands in `build-deb/<release>/` with a `lintian.txt` report to read.
Copies for the download page, such as `screensense_0.2.0_ubuntu24.04_amd64.deb`,
collect in `build-deb/release/` with a `SHA256SUMS` file.

## 4. Commit and tag

```sh
git commit -am "Release x.y.z"
git tag -a vx.y.z -m "ScreenSense x.y.z"
git push origin main --tags
```

## 5. GitHub release

On GitHub, open Releases, then Draft a new release. Pick the tag, paste the
`debian/changelog` entry as the notes, and attach everything in
`build-deb/release/`.

## 6. Ubuntu PPA (optional, lets people install with apt and get updates)

One-time setup:

1. Create an account at https://launchpad.net.
2. Create an OpenPGP key (`gpg --full-generate-key`) and add it to Launchpad
   under your profile.
3. On your profile page, choose Create a new PPA. Name it `screensense`.
4. Install the tools: `sudo apt install devscripts dput`

For each Ubuntu release you support, build and upload a signed source package.
Each upload needs its own version, so add the release name:

```sh
dch -v x.y.z~noble1 -D noble "Build for Ubuntu 24.04"
debuild -S -sa
dput ppa:YOUR_LAUNCHPAD_NAME/screensense ../screensense_x.y.z~noble1_source.changes
git checkout debian/changelog
```

Repeat with `resolute` for Ubuntu 26.04. Launchpad builds the packages, which
takes about half an hour. Users then install with:

```sh
sudo add-apt-repository ppa:YOUR_LAUNCHPAD_NAME/screensense
sudo apt install screensense
```

## 7. GNOME Extensions website (optional)

```sh
gnome-extensions pack gnome-extension/screensense@aravindedakkot.gmail.com
```

Upload the `.zip` at https://extensions.gnome.org/upload/. A person reviews
each version, which can take days. The description already says the extension
needs the `screensense` service, which reviewers expect to be stated.
