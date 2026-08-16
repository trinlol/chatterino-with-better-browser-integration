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
