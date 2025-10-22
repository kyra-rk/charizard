#!/usr/bin/env bash
# dev-start.sh
# Small helper to run the server with a temporary developer ADMIN_API_KEY in this shell session.

set -euo pipefail

# Change this value for your local dev environment; do NOT use this in production.
ADMIN_KEY="changeme_admin_key_please_replace"
export ADMIN_API_KEY="$ADMIN_KEY"

echo "Starting server with ADMIN_API_KEY=$ADMIN_KEY"
make run
