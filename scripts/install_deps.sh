#!/bin/bash

# Material3UI Centralized Dependency Installer
# Usage: ./install_deps.sh <category> [confirm_flag]

CATEGORY=$1
CONFIRM_FLAG=$2 # e.g., --noconfirm

DEP_JSON="data/dependencies.json"

if [ ! -f "$DEP_JSON" ]; then
    echo "Error: $DEP_JSON not found!"
    exit 1
fi

# Ensure jq is installed (it's the core of this system)
if ! command -v jq >/dev/null 2>&1; then
    echo "Installing jq for dependency management..."
    sudo pacman -S --needed $CONFIRM_FLAG jq
fi

# Get package list for the category
PACKAGES=$(jq -r ".$CATEGORY[].name" "$DEP_JSON")

if [ -z "$PACKAGES" ] || [ "$PACKAGES" == "null" ]; then
    echo "No packages found for category: $CATEGORY"
    exit 0
fi

echo "Installing $CATEGORY dependencies..."
yay -S --needed $CONFIRM_FLAG $PACKAGES
