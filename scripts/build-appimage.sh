#!/bin/bash
set -e
cd "$(dirname "$0")/.."

echo "🔨 Building Linux AppImage (statically linked)..."
docker build --build-arg APP_VERSION="$(git describe --tags --match 'v[0-9]*' 2>/dev/null || echo 0.0.0-dev)" -t 3dco-appimage-builder -f packaging/docker/Dockerfile.appimage .
mkdir -p dist/linux
docker run --rm -v $(pwd)/dist:/dist 3dco-appimage-builder
echo "✅ Linux AppImage created in ./dist/linux/"