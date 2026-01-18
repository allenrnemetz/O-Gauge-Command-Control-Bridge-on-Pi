#!/bin/bash
# Lionel MTH Bridge Updater
# Pull latest code, update dependencies, and restart service

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR"

if [ ! -d "venv" ]; then
  echo "Virtual environment not found. Please run install.sh first."
  exit 1
fi

source venv/bin/activate

echo "🚂 Updating repository..."
git pull --rebase

echo "📦 Updating Python dependencies..."
pip install --upgrade -r requirements.txt

echo "🔄 Restarting service..."
sudo systemctl restart lionel-mth-bridge.service

echo "✅ Update complete."
