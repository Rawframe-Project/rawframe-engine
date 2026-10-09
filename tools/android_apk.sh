#!/usr/bin/env bash
# Makes the Android client's application (D551) with the SDK's own tools and
# no Gradle:
#
#   tools/android_apk.sh <out.apk> <librawframe_client.so> <package> <label>
#
# Maul Window's Java activity (third_party/maul-window/java) compiled by javac
# and d8, hosts/android_client/android/AndroidManifest.xml with the package
# and label filled in, linked by aapt2 as debuggable, the client's library
# under lib/arm64-v8a/, aligned, and signed with a debug key kept beside the
# output. ANDROID_HOME names the SDK; its newest build tools and platform are
# used.
set -euo pipefail
cd "$(dirname "$0")/.."

out="$1"
library="$2"
package="$3"
label="$4"
tools="$(ls -d "$ANDROID_HOME"/build-tools/* | sort -V | tail -1)"
jar="$(ls -d "$ANDROID_HOME"/platforms/android-* | sort -V | tail -1)/android.jar"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

sed -e "s/@PACKAGE@/$package/" -e "s/@LABEL@/$label/" \
    hosts/android_client/android/AndroidManifest.xml >"$work/AndroidManifest.xml"
mkdir -p "$work/classes" "$work/dex" "$work/lib/arm64-v8a"
javac -nowarn --release 11 -classpath "$jar" -d "$work/classes" \
    $(find third_party/maul-window/java -name '*.java')
"$tools/d8" --min-api 29 --lib "$jar" --output "$work/dex" $(find "$work/classes" -name '*.class')
"$tools/aapt2" link -o "$work/linked.apk" -I "$jar" --manifest "$work/AndroidManifest.xml" \
    --min-sdk-version 29 --target-sdk-version 35 --debug-mode
cp "$library" "$work/lib/arm64-v8a/librawframe_client.so"
cp "$work/dex/classes.dex" "$work/"
(cd "$work" && zip -q linked.apk classes.dex lib/arm64-v8a/librawframe_client.so)
"$tools/zipalign" -f -p 4 "$work/linked.apk" "$work/aligned.apk"
key="$(dirname "$out")/debug.keystore"
if [ ! -f "$key" ]; then
    keytool -genkeypair -keystore "$key" -storepass android -keypass android -alias debug -keyalg RSA \
        -validity 10000 -dname "CN=Rawframe development" >/dev/null 2>&1
fi
"$tools/apksigner" sign --ks "$key" --ks-pass pass:android --out "$out" "$work/aligned.apk"
