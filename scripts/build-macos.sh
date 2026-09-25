#!/bin/bash
set -e
cd "$(dirname "$0")/.."   # move to repo root

echo "🔨 Building macOS Universal App..."
docker build --build-arg APP_VERSION="$(git describe --tags --match 'v[0-9]*' 2>/dev/null || echo 0.0.0-dev)" -t 3dco-macos-builder -f packaging/docker/Dockerfile.macos .
mkdir -p dist/macos
docker run --rm -v $(pwd)/dist:/dist 3dco-macos-builder
echo "✅ macOS app bundle created in ./dist/macos/"
