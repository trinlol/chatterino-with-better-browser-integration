#!/usr/bin/env bash

# Bundle relevant Qt and system dependencies into the application bundle.

set -eo pipefail

APP_BUNDLE="Chatterino Better Browser.app"

if [ -d "bin/$APP_BUNDLE" ] && [ ! -d "$APP_BUNDLE" ]; then
    >&2 echo "Moving bin/$APP_BUNDLE down one directory"
    mv "bin/$APP_BUNDLE" "$APP_BUNDLE"
fi

if [ -n "$Qt5_DIR" ]; then
    echo "Using Qt DIR from Qt5_DIR: $Qt5_DIR"
    _QT_DIR="$Qt5_DIR"
elif [ -n "$Qt6_DIR" ]; then
    echo "Using Qt DIR from Qt6_DIR: $Qt6_DIR"
    _QT_DIR="$Qt6_DIR"
fi

if [ -n "$_QT_DIR" ]; then
    export PATH="${_QT_DIR}/bin:$PATH"
else
    echo "No Qt environment variable set, assuming system-installed Qt"
fi

echo "Running MACDEPLOYQT"

_macdeployqt_args=()

if [ -n "$MACOS_CODESIGN_CERTIFICATE" ]; then
    _macdeployqt_args+=("-codesign=$MACOS_CODESIGN_CERTIFICATE")
fi

macdeployqt "$APP_BUNDLE" "${_macdeployqt_args[@]}"

if [ -n "$MACOS_CODESIGN_CERTIFICATE" ]; then
    # Validate that the application bundle was codesigned correctly.
    codesign -v "$APP_BUNDLE"
fi
