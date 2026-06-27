#!/bin/bash
# Bundle all non-system dylibs into the macOS .app bundle.
# Usage: bundle_macos_dylibs.sh <executable> <frameworks_dir>
# Recursively copies and fixes references for all third-party dylibs.

set -euo pipefail

EXECUTABLE="$1"
FRAMEWORKS_DIR="$2"

mkdir -p "$FRAMEWORKS_DIR"

# Return the list of LC_RPATH entries embedded in a binary, one per line.
get_rpaths() {
    otool -l "$1" | awk '
        /cmd LC_RPATH/ { in_rpath = 1; next }
        in_rpath && $1 == "path" { print $2; in_rpath = 0 }
    '
}

# Resolve an @rpath/foo or @loader_path/foo reference against a binary's
# LC_RPATH entries (plus the main executable's dir as a last resort).
# Echoes the resolved absolute path, or nothing if not found.
resolve_rpath_dep() {
    local binary="$1"
    local dep="$2"
    local name="${dep#@rpath/}"
    name="${name#@loader_path/}"
    name="${name#@executable_path/}"

    local loader_dir
    loader_dir="$(dirname "$binary")"
    local exe_dir
    exe_dir="$(dirname "$EXECUTABLE")"

    case "$dep" in
        @loader_path/*)
            local candidate="$loader_dir/${dep#@loader_path/}"
            [ -f "$candidate" ] && { echo "$candidate"; return; }
            ;;
        @executable_path/*)
            local candidate="$exe_dir/${dep#@executable_path/}"
            [ -f "$candidate" ] && { echo "$candidate"; return; }
            ;;
    esac

    local rp
    while IFS= read -r rp; do
        [ -z "$rp" ] && continue
        case "$rp" in
            @loader_path/*) rp="$loader_dir/${rp#@loader_path/}" ;;
            @loader_path)   rp="$loader_dir" ;;
            @executable_path/*) rp="$exe_dir/${rp#@executable_path/}" ;;
            @executable_path)   rp="$exe_dir" ;;
        esac
        if [ -f "$rp/$name" ]; then
            echo "$rp/$name"
            return
        fi
    done < <(get_rpaths "$binary")
}

# Fix all non-system dylib references in a binary.
# Copies missing dylibs to Frameworks/ and rewrites load paths.
fix_dylib_refs() {
    local binary="$1"
    otool -L "$binary" | tail -n +2 | awk '{print $1}' | while read -r dep; do
        case "$dep" in
            /usr/lib/*|/System/*) continue ;;
            @executable_path/*) continue ;;
        esac

        local filename
        filename=$(basename "$dep")
        local dest="$FRAMEWORKS_DIR/$filename"

        local src=""
        case "$dep" in
            @rpath/*|@loader_path/*)
                src="$(resolve_rpath_dep "$binary" "$dep" || true)"
                ;;
            *)
                if [ -f "$dep" ]; then
                    src="$(perl -MCwd -e 'print Cwd::realpath($ARGV[0])' "$dep")"
                fi
                ;;
        esac

        # Copy the dylib if not already bundled
        if [ ! -f "$dest" ]; then
            if [ -z "$src" ] || [ ! -f "$src" ]; then
                echo "WARNING: Cannot resolve $dep (referenced from $binary)" >&2
                continue
            fi
            cp "$src" "$dest"
            chmod u+w "$dest"
            # Set the dylib's own ID to the bundle-relative path
            install_name_tool -id "@executable_path/../Frameworks/$filename" "$dest"
            # Recursively fix this dylib's own dependencies
            fix_dylib_refs "$dest"
        fi

        # Rewrite the reference in the current binary
        install_name_tool -change "$dep" "@executable_path/../Frameworks/$filename" "$binary"
    done
}

# Fix the main executable
fix_dylib_refs "$EXECUTABLE"

# Fix cross-references between bundled dylibs (including @rpath refs)
for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    [ -f "$dylib" ] || continue
    fix_dylib_refs "$dylib"
done

# Re-sign everything install_name_tool touched. Mach-O edits invalidate the
# existing signature and arm64 macOS refuses to load unsigned pages.
# Sign each Mach-O through a temporary copy outside the .app: codesign auto-
# detects bundle layout from the path and rejects "detritus" xattrs that iCloud
# / Finder reapply on parent directories faster than we can strip them. macOS
# 26 changed codesign to allocate a new inode when rewriting the signature, so
# a hardlink no longer reflects back to the original path — copy out, sign in
# the neutral location, then move back over the original.
sign_mach_o() {
    local target="$1"
    local tmp
    tmp="$(mktemp -u "${TMPDIR:-/tmp}/cs.XXXXXX")"
    cp "$target" "$tmp"
    codesign --force --sign - "$tmp"
    mv "$tmp" "$target"
}

for dylib in "$FRAMEWORKS_DIR"/*.dylib; do
    [ -f "$dylib" ] || continue
    sign_mach_o "$dylib"
done
sign_mach_o "$EXECUTABLE"

echo "Bundle dylib fixup complete."