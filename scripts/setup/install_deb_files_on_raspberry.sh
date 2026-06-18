#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(cd "$script_directory/../.." && pwd)"
deb_files_directory="$repository_root_directory/data/deb_files"


mapfile -t deb_package_paths < <(find "$deb_files_directory" -maxdepth 1 -type f -name "*.deb" | sort)

if [[ ${#deb_package_paths[@]} -eq 0 ]]; then
  echo "No .deb files found in: $deb_files_directory" >&2
  exit 1
fi

echo "Installing ${#deb_package_paths[@]} package(s) from $deb_files_directory"

if [[ "$EUID" -eq 0 ]]; then
  apt-get update
  apt-get install -y "${deb_package_paths[@]}"
else
  sudo apt-get update
  sudo apt-get install -y "${deb_package_paths[@]}"
fi

echo "Installation finished."
