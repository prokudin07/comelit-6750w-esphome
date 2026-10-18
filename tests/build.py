#!/usr/bin/env python3
"""Compile the shipped example against the local component with dummy Wi-Fi."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
example = sys.argv[1] if len(sys.argv) > 1 else "bus-monitor.yaml"
source = (root / "examples" / example).read_text()
source = source.replace(
    "source: github://prokudin07/comelit-6750w-esphome@main",
    "source:\n      type: local\n      path: " + str(root / "components"),
)
with tempfile.TemporaryDirectory(prefix="comelit-build-") as tmp:
    tmp = Path(tmp)
    (tmp / "receiver.yaml").write_text(source)
    (tmp / "secrets.yaml").write_text("wifi_ssid: test-network\nwifi_password: test-password\n")
    subprocess.run(["esphome", "compile", str(tmp / "receiver.yaml")], check=True)
