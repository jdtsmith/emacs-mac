#!/opt/local/bin/bash
set -euo pipefail

APP="$HOME/Applications/emacs-mac/Emacs.app"
FRAMEWORKS="$APP/Contents/Frameworks"
MACHOS=("$APP/Contents/MacOS/Emacs")

mkdir -p "$FRAMEWORKS"

# Collect all executables in MacOS dir (emacs, ctags, etc.)
while IFS= read -r f; do
    MACHOS+=("$f")
done < <(find "$APP/Contents/MacOS/libexec" -type f -perm +111 2>/dev/null || true)

# Track already-processed dylibs to avoid loops
declare -A PROCESSED

# Libs to skip bundling — expect these on the target system (e.g. via MacPorts).
# libgccjit needs the full gcc toolchain (driver, cc1, as, ld) at runtime, so it
# must come from the target's own gcc14 install.
declare -A SKIP_BUNDLE=(
    [libgccjit.0.dylib]=1
    [libisl.23.dylib]=1
    [libmpc.3.dylib]=1
    [libmpfr.6.dylib]=1
)

# Try @executable_path/../Frameworks/ first; fall back to @loader_path/ if
# install_name_tool fails (insufficient header padding for the longer path).
change_ref() {
    local binary="$1" old_ref="$2" libname="$3"
    local long="@executable_path/../Frameworks/$libname"
    local short="@loader_path/$libname"

    if install_name_tool -change "$old_ref" "$long" "$binary" 2>/dev/null; then
        return 0
    fi
    # Longer path didn't fit — use shorter @loader_path
    if install_name_tool -change "$old_ref" "$short" "$binary" 2>/dev/null; then
        return 0
    fi
    echo "  WARNING: could not rewrite $old_ref in $(basename "$binary")"
    return 1
}

set_id() {
    local lib="$1" libname="$2"
    install_name_tool -id "@executable_path/../Frameworks/$libname" "$lib" 2>/dev/null \
        || install_name_tool -id "@loader_path/$libname" "$lib" 2>/dev/null \
        || true
}

copy_deps() {
    local binary="$1"

    # Collect non-system, non-already-rewritten deps
    local all_deps
    all_deps=$(otool -L "$binary" | awk 'NR>1 {print $1}' \
        | grep -v /usr/lib | grep -v /System \
        | grep -v '@executable_path' | grep -v '@loader_path' || true)

    for dep in $all_deps; do
        local libname
        libname=$(basename "$dep")
        local dest="$FRAMEWORKS/$libname"

        if [[ -n "${PROCESSED[$libname]:-}" ]]; then
            change_ref "$binary" "$dep" "$libname"
            continue
        fi

        # Skip libs that must come from the target system
        if [[ -n "${SKIP_BUNDLE[$libname]:-}" ]]; then
            PROCESSED[$libname]=1
            echo "Skipping $libname (expected on target system)"
            continue
        fi

        # Resolve the actual file path
        local resolved=""
        if [[ "$dep" == @rpath/* ]]; then
            for rpath in /opt/local/lib /opt/local/lib/gcc14; do
                if [[ -f "$rpath/$libname" ]]; then
                    resolved="$rpath/$libname"
                    break
                fi
            done
            if [[ -z "$resolved" ]]; then
                echo "WARNING: Cannot resolve $dep, skipping"
                continue
            fi
        elif [[ -f "$dep" ]]; then
            resolved="$dep"
        else
            echo "WARNING: $dep not found, skipping"
            continue
        fi

        echo "Bundling $libname"
        cp -f "$resolved" "$dest"
        chmod 644 "$dest"
        PROCESSED[$libname]=1

        set_id "$dest" "$libname"
        change_ref "$binary" "$dep" "$libname"

        # Recurse into the copied dylib's own dependencies
        copy_deps "$dest"
    done
}

# --- Phase 1: copy all deps and rewrite references in the calling binary ---
for macho in "${MACHOS[@]}"; do
    echo "=== Processing $(basename "$macho") ==="
    copy_deps "$macho"
done

# --- Phase 2: fix cross-references between bundled dylibs ---
echo ""
echo "=== Fixing cross-references ==="
for lib in "$FRAMEWORKS"/*.dylib; do
    [[ -f "$lib" ]] || continue
    deps=$(otool -L "$lib" | awk 'NR>1 {print $1}' \
        | grep -v /usr/lib | grep -v /System \
        | grep -v '@executable_path' | grep -v '@loader_path' || true)
    for dep in $deps; do
        depname=$(basename "$dep")
        if [[ -f "$FRAMEWORKS/$depname" ]]; then
            change_ref "$lib" "$dep" "$depname"
        fi
    done
done

# --- Phase 3: verify everything ---
echo ""
echo "=== Verifying ==="
fail=0
for f in "$FRAMEWORKS"/*.dylib "${MACHOS[@]}"; do
    [[ -f "$f" ]] || continue
    while IFS= read -r ref; do
        [[ -z "$ref" ]] && continue
        refname=$(basename "$ref")
        # Allow @rpath refs for libs we intentionally skipped
        if [[ "$ref" == @rpath/* && -n "${SKIP_BUNDLE[$refname]:-}" ]]; then
            continue
        fi
        echo "UNFIXED $(basename "$f"): $ref"
        fail=1
    done < <(otool -L "$f" | awk 'NR>1 {print $1}' \
        | grep -E '/opt/local|@rpath' || true)
done
if (( fail == 0 )); then
    echo "All dependencies bundled successfully!"
fi

# --- Phase 4: ad-hoc sign ---
echo ""
echo "=== Signing ==="
# Sign dylibs
for lib in "$FRAMEWORKS"/*.dylib; do
    codesign --force --sign - "$lib" 2>/dev/null
done
# Sign helper executables
find "$APP/Contents/MacOS/libexec" -type f -perm +111 -exec codesign --force --sign - {} \; 2>/dev/null || true
# Sign the app bundle (hide libexec to avoid codesign sub-bundle detection bug)
if [[ -d "$APP/Contents/MacOS/libexec" ]]; then
    mv "$APP/Contents/MacOS/libexec" "$APP/Contents/_libexec_tmp"
    codesign --force --deep --sign - "$APP"
    mv "$APP/Contents/_libexec_tmp" "$APP/Contents/MacOS/libexec"
else
    codesign --force --deep --sign - "$APP"
fi

echo ""
du -sh "$APP"
echo "Done. On the target Mac run: xattr -cr '$APP'"
