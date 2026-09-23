#!/bin/bash
# Install binaries + user service. Run from the repo root on the host.
set -euo pipefail
cmake -B build -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build -j"$(nproc)"
mkdir -p ~/.local/bin ~/.config/nv-broadcast ~/.config/systemd/user
install -m755 build/nvbcastd build/nvbcast ~/.local/bin/
[ -f build/nvbcast-gui ] && install -m755 build/nvbcast-gui ~/.local/bin/ || echo "(nvbcast-gui skipped: install gtk4 + libadwaita dev files to build it)"
[ -f ~/.config/nv-broadcast/config.toml ] || install -m600 config.example.toml ~/.config/nv-broadcast/config.toml
install -m644 systemd/nv-broadcast.service ~/.config/systemd/user/
systemctl --user daemon-reload
echo "Installed. Next:"
echo "  nvbcast list"
echo "  nvbcast set-input <device-id>"
echo "  systemctl --user enable --now nv-broadcast.service"
