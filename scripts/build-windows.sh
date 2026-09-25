#!/bin/bash
set -e
cd "$(dirname "$0")/.."   # move to repo root

echo "🔨 Building Windows executable..."
docker build --build-arg APP_VERSION="$(git describe --tags --match 'v[0-9]*' 2>/dev/null || echo 0.0.0-dev)" -t 3dco-windows-builder -f packaging/docker/Dockerfile.windows .
mkdir -p dist/windows
docker run --rm -v $(pwd)/dist:/dist 3dco-windows-builder
echo "✅ Windows executable created in ./dist/windows/"
