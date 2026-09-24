#!/usr/bin/env bash
# Replaces third_party/msquic and third_party/openssl with the sources of one
# exact MsQuic revision and the OpenSSL revision that MsQuic revision pins as
# its submodule, taken from upstream's archives and pruned to what a static
# library build reads.
#
#   tools/update_quic.sh <msquic revision> <openssl revision>
set -euo pipefail
cd "$(dirname "$0")/.."

msquic_revision="$1"
openssl_revision="$2"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fetch() {
    curl -fsSL "https://codeload.github.com/$1/tar.gz/$2" | tar -xz -C "$work"
}

fetch microsoft/msquic "$msquic_revision"
fetch openssl/openssl "$openssl_revision"
msquic_source="$(echo "$work"/msquic-*)"
openssl_source="$(echo "$work"/openssl-*)"

# MsQuic: the build files, the library, and its notices. Tests, tools,
# samples, bindings, and submodules are not read by a library build.
rm -rf third_party/msquic
mkdir -p third_party/msquic/src
cp -r "$msquic_source"/{cmake,CMakeLists.txt,version.json,LICENSE,THIRD-PARTY-NOTICES} third_party/msquic/
cp -r "$msquic_source"/src/{bin,core,generated,inc,manifest,platform} third_party/msquic/src/
# Windows profile-guided optimisation data: binary, and read by nothing here.
rm -rf third_party/msquic/src/bin/win*/pgo_*

# OpenSSL: Configure reads every build.info, so documentation and fuzzing
# keep theirs; the rest of those directories, the tests, and the demos go.
rm -rf third_party/openssl
cp -r "$openssl_source" third_party/openssl
rm -rf third_party/openssl/{test,demos}
# The command-line programs' sample keys and certificates: not built, and
# private keys have no place in this repository even as examples.
rm -f third_party/openssl/apps/*.pem
find third_party/openssl/doc third_party/openssl/fuzz -type f ! -name build.info -delete
find third_party/openssl -type d -empty -delete

echo "third_party/msquic is now $msquic_revision and third_party/openssl is $openssl_revision;"
echo "update the table in third_party/README.md"
