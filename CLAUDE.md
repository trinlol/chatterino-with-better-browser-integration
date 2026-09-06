## Project Discovery

- Default branch: `master`
- Docs: `docs/`; extension user guide: `chatterino-extension/README.md`
- Coding standards: `CONTRIBUTING.md`, `.clang-format`, `.clang-tidy`, `.prettierrc`
- Layout: `src/` (C++ desktop client and native bridge); `tests/src/` (desktop tests); `chatterino-extension/` (MV3 extension); `chatterino-extension/tests/` (extension tests)

### Chatterino Better Browser desktop client

- Root: `.`
- Language: C++23
- Frameworks: Qt 6 Widgets, CMake 3.22+, GoogleTest, Lua/Sol2 plugins
- Dependency systems: Conan 2, vcpkg manifest, Git submodules
- Install dependencies: `git submodule update --init --recursive`
- Test: configure with `-DBUILD_TESTS=On`, build, then run `ctest --output-on-failure`
- Build: `cmake --build . --config Release`

### Chatterino Better Browser MV3 extension

- Root: `chatterino-extension/`
- Language: JavaScript, HTML, CSS
- Frameworks: Chrome WebExtensions Manifest V3, Node test runner
- Test: `npm test`
- Package: `npm run package:extension`
- Dev: load `chatterino-extension/` unpacked in Chrome or Edge

### Releasing to GitHub

- **Version bump is mandatory on every release push.** Any push that ships an update to users (GitHub release with prebuilt binaries, or a commit that changes application/extension behavior) must bump the version in ALL FOUR files together, keeping them in lockstep:
  1. `CMakeLists.txt` (`project(chatterino VERSION X.Y.Z ...)` — sets the exe's version resource)
  2. `package.json` (`version`)
  3. `release-contract.json` (`applicationVersion` AND `extensionVersion`)
  4. `chatterino-extension/manifest.json` (`version`)
- Re-run `npm run test:e2e:contract` after bumping; it validates the release contract.
- Commit the bump as `chore(release): bump version to X.Y.Z` on `master` before creating the GitHub release.
- Never rebuild/publish a prebuilt package whose embedded version was not bumped — an unverifiable binary is a release blocker.
- Release evidence contract (gates, smoke matrix, artifact collection): `docs/testing/browser-native-release-gate.md`
