#!/bin/bash
# Crea Nebula-Instalador.pkg con el AU y la app independiente (VST3 sólo con NEBULA_INCLUDE_VST3=1).
set -euo pipefail

VERSION="${1:-1.0.0}"
ART="build/NebulaSynth_artefacts/Release"
WORK="build/pkg"
OUT="dist"

rm -rf "$WORK" "$OUT"
mkdir -p "$WORK/vst3" "$WORK/au" "$WORK/app" "$WORK/scripts" "$OUT"

# NEBULA-CORE-050: sin artefactos VST3 nuevos (sólo se incluye si alguien lo compiló a propósito)
HAS_VST3=0
if [ -d "$ART/VST3/Nebula.vst3" ] && [ "${NEBULA_INCLUDE_VST3:-0}" = "1" ]; then cp -R "$ART/VST3/Nebula.vst3" "$WORK/vst3/"; HAS_VST3=1; fi
cp -R "$ART/AU/Nebula.component"    "$WORK/au/"
cp -R "$ART/Standalone/Nebula.app"  "$WORK/app/"

# Script post-instalación: refresca la caché de Audio Units
cat > "$WORK/scripts/postinstall" <<'EOS'
#!/bin/bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
exit 0
EOS
chmod +x "$WORK/scripts/postinstall"

# Evita que el instalador "reubique" el plugin si encuentra otra copia en el disco
make_component () {
    local name="$1" root="$2" dest="$3" extra="${4:-}"
    pkgbuild --analyze --root "$root" "$WORK/$name.plist"
    local i=0
    while plutil -extract "$i" xml1 -o /dev/null "$WORK/$name.plist" 2>/dev/null; do
        plutil -replace "$i.BundleIsRelocatable" -bool NO "$WORK/$name.plist"
        i=$((i + 1))
    done
    pkgbuild --root "$root" \
             --component-plist "$WORK/$name.plist" \
             --identifier "com.remacdent.nebulasynth.$name" \
             --version "$VERSION" \
             --install-location "$dest" \
             $extra \
             "$WORK/$name.pkg"
}

if [ "$HAS_VST3" = "1" ]; then make_component vst3 "$WORK/vst3" "/Library/Audio/Plug-Ins/VST3"; fi
make_component au   "$WORK/au"   "/Library/Audio/Plug-Ins/Components" "--scripts $WORK/scripts"
make_component app  "$WORK/app"  "/Applications"

VST3_LINE=""; VST3_CHOICE=""; VST3_REF=""
if [ "$HAS_VST3" = "1" ]; then
  VST3_LINE='<line choice="vst3"/>'
  VST3_CHOICE='<choice id="vst3" title="Nebula VST3" description="Plugin VST3"><pkg-ref id="com.remacdent.nebulasynth.vst3"/></choice>'
  VST3_REF="<pkg-ref id=\"com.remacdent.nebulasynth.vst3\" version=\"${VERSION}\">vst3.pkg</pkg-ref>"
fi
cat > "$WORK/distribution.xml" <<EOX
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>Nebula ${VERSION}</title>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <choices-outline>
        ${VST3_LINE}
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>
    ${VST3_CHOICE}
    <choice id="au" title="Nebula Audio Unit" description="Plugin AU (Ableton, Logic, GarageBand)">
        <pkg-ref id="com.remacdent.nebulasynth.au"/>
    </choice>
    <choice id="app" title="Nebula (app independiente)" description="Para probar el sinte sin abrir un DAW">
        <pkg-ref id="com.remacdent.nebulasynth.app"/>
    </choice>
    ${VST3_REF}
    <pkg-ref id="com.remacdent.nebulasynth.au"   version="${VERSION}">au.pkg</pkg-ref>
    <pkg-ref id="com.remacdent.nebulasynth.app"  version="${VERSION}">app.pkg</pkg-ref>
</installer-gui-script>
EOX

productbuild --distribution "$WORK/distribution.xml" \
             --package-path "$WORK" \
             "$OUT/Nebula-Instalador-${VERSION}.pkg"

echo "Listo: $OUT/Nebula-Instalador-${VERSION}.pkg"
