#!/bin/sh
set -eu

usage() {
  printf 'Usage: %s [--build-only]\n' "$0"
  printf '  --build-only  Build and test without installing or launching Resolve.\n'
}

build_only=0
case "${1:-}" in
  '') ;;
  --build-only) build_only=1 ;;
  -h|--help) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then
  usage >&2
  exit 2
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
build_dir="$project_dir/build"
resolve_app='/Applications/DaVinci Resolve/DaVinci Resolve.app'
plugin_dir="/Library/OFX/Plugins"
bundle_name='LoFiFxCameraMatch.ofx.bundle'
source_bundle="$build_dir/$bundle_name"
installed_bundle="$plugin_dir/$bundle_name"

if [ "$build_only" -eq 0 ] && [ ! -d "$resolve_app" ]; then
  printf 'DaVinci Resolve is not installed at %s\n' "$resolve_app" >&2
  exit 1
fi

printf '\n==> Configuring Camera Match\n'
# An optional local SDK checkout must be the pinned OpenFX revision in CMakeLists.txt.
# Clear any previous override when the variable is absent so ordinary builds fetch it.
if [ -n "${OPENFX_SOURCE_DIR:-}" ]; then
  sdk_revision=$(git -C "$OPENFX_SOURCE_DIR" rev-parse HEAD 2>/dev/null || true)
  if [ "$sdk_revision" != 'ab779510b2655b4d11a7e01e5c521f9aa8c88976' ]; then
    printf 'OPENFX_SOURCE_DIR must point to the pinned OpenFX 1.5.1 revision.\n' >&2
    exit 1
  fi
  cmake -S "$project_dir" -B "$build_dir" \
    -DFETCHCONTENT_SOURCE_DIR_OPENFX="$OPENFX_SOURCE_DIR"
else
  cmake -S "$project_dir" -B "$build_dir" \
    -UFETCHCONTENT_SOURCE_DIR_OPENFX
fi

printf '\n==> Building Camera Match\n'
cmake --build "$build_dir" --parallel

printf '\n==> Running automated checks\n'
ctest --test-dir "$build_dir" --output-on-failure

if [ "$build_only" -eq 1 ]; then
  printf '\nBuild complete: %s\n' "$source_bundle"
  exit 0
fi

if pgrep -x Resolve >/dev/null 2>&1; then
  printf '\nDaVinci Resolve is running. Save your project and quit Resolve.\n'
  printf 'Waiting for Resolve to close (press Ctrl-C to cancel)...\n'
  while pgrep -x Resolve >/dev/null 2>&1; do
    sleep 2
  done
fi

if [ -d "$installed_bundle" ] && [ -w "$installed_bundle/Contents" ] &&
   [ -w "$installed_bundle/Contents/MacOS" ]; then
  printf '\n==> Updating writable system OFX bundle\n'
  ditto "$source_bundle" "$installed_bundle"
else
  sudo mkdir -p "$plugin_dir"
  staging_dir=$(sudo mktemp -d "$plugin_dir/.camera-match.XXXXXX")
  new_bundle="$staging_dir/$bundle_name"
  previous_bundle="$staging_dir/previous.bundle"
  cleanup() {
    if [ -d "$previous_bundle" ] && [ ! -e "$installed_bundle" ]; then
      sudo mv "$previous_bundle" "$installed_bundle"
    fi
    sudo rm -rf "$staging_dir"
  }
  trap cleanup 0
  trap 'exit 130' INT
  trap 'exit 143' TERM
  printf '\n==> Installing system OFX bundle (administrator password required)\n'
  sudo ditto "$source_bundle" "$new_bundle"
  if [ -e "$installed_bundle" ]; then
    sudo mv "$installed_bundle" "$previous_bundle"
  fi
  sudo mv "$new_bundle" "$installed_bundle"
fi
legacy_bundle="$HOME/Library/OFX/Plugins/$bundle_name"
if [ -d "$legacy_bundle" ]; then
  rm -rf "$legacy_bundle"
fi

cache_file="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCacheV2.xml"
if [ -f "$cache_file" ]; then
  mv "$cache_file" "$cache_file.camera-match-$(date +%Y%m%d%H%M%S).bak"
fi

printf '\n==> Launching DaVinci Resolve\n'
open -a "$resolve_app"
printf 'Installed: %s\n' "$installed_bundle"
