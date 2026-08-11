#!/usr/bin/env python3
"""
Clawmachine – Projekt-Dashboard
Baut und startet alle Docker-Services und zeigt die Logs jedes Containers
in einem eigenen Tile an. Strg+C stoppt alle Container wieder.
"""

import re
import shutil
import subprocess
import sys
import threading
import time
from collections import deque
from pathlib import Path


# ─── Rich ────────────────────────────────────────────────────────────────────

def _ensure_rich() -> None:
    try:
        import rich  # noqa: F401
    except ImportError:
        print("Installiere 'rich' …")
        result = subprocess.run(["apt-get", "install", "-y", "-q", "python3-rich"], capture_output=True)
        if result.returncode != 0:
            subprocess.run([sys.executable, "-m", "pip", "install", "--quiet", "rich"], check=True)


_ensure_rich()

from rich.console import Console  # noqa: E402
from rich.layout import Layout  # noqa: E402
from rich.live import Live  # noqa: E402
from rich.panel import Panel  # noqa: E402
from rich.text import Text  # noqa: E402

console = Console()

_SCRIPT_DIR = Path(__file__).parent
_REPO_ROOT = _SCRIPT_DIR.parent.parent
_COMPOSE = _REPO_ROOT / "docker" / "docker-compose.yml"
_TILE_HISTORY = 200
_TILE_COLUMNS = 2

# ─── Log-Level-Einfärbung ─────────────────────────────────────────────────────

_LEVEL_PATTERNS = [
    (re.compile(r"\b(CRITICAL|FATAL)\b"), "bold red"),
    (re.compile(r"\b(ERROR|error|Fehler|failed|FAILED|Exception|Traceback)\b"), "red"),
    (re.compile(r"\b(WARNING|Warning|WARN|Warnung|DeprecationWarning)\b"), "yellow"),
    (re.compile(r"\bDEBUG\b"), "dim blue"),
    (re.compile(r"\b(Started|Running|Connected|Startup|ready|started|connected)\b"), "green"),
]


def _line_style(line: str) -> str:
    for pattern, style in _LEVEL_PATTERNS:
        if pattern.search(line):
            return style
    return ""


# ─── Docker-Hilfsfunktionen ────────────────────────────────────────────────────

def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def _compose_services() -> list[str]:
    result = _run(["docker", "compose", "-f", str(_COMPOSE), "config", "--services"])
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


# ─── Log-Tiles ─────────────────────────────────────────────────────────────────

class LogTile:
    def __init__(self, service: str):
        self.service = service
        self.lines: deque[str] = deque(maxlen=_TILE_HISTORY)
        self._process = None
        self._thread = None

    def start(self) -> None:
        self._process = subprocess.Popen(
            ["docker", "compose", "-f", str(_COMPOSE), "logs", "-f", "--tail", "30", self.service],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        self._thread = threading.Thread(target=self._read, daemon=True)
        self._thread.start()

    def _read(self) -> None:
        for line in self._process.stdout:
            self.lines.append(line.rstrip())

    def stop(self) -> None:
        if self._process is not None:
            self._process.terminate()

    def render(self, height: int) -> Panel:
        visible = list(self.lines)[-max(height, 1):]
        text = Text()
        for line in visible:
            text.append(line + "\n", style=_line_style(line))
        return Panel(text, title=f"[bold]{self.service}[/]", border_style="blue")


def _build_layout(service_names: list[str]) -> Layout:
    layout = Layout()
    rows = [service_names[i:i + _TILE_COLUMNS] for i in range(0, len(service_names), _TILE_COLUMNS)]
    row_layouts = []
    for row in rows:
        row_layout = Layout()
        row_layout.split_row(*[Layout(name=name) for name in row])
        row_layouts.append(row_layout)
    layout.split_column(*row_layouts)
    return layout


# ─── Hauptprogramm ────────────────────────────────────────────────────────────

def _check_prerequisites() -> bool:
    if not shutil.which("docker"):
        console.print("\n  [red]✗ docker nicht gefunden – bitte setup_server.py ausführen[/]")
        return False
    if not _COMPOSE.exists():
        console.print(f"\n  [red]✗ {_COMPOSE} nicht gefunden[/]")
        return False
    return True


def _build_and_start() -> bool:
    console.print("\n  [bold dim]▸ Images bauen[/]")
    if subprocess.run(["docker", "compose", "-f", str(_COMPOSE), "build"]).returncode != 0:
        console.print("  [red]✗ Build fehlgeschlagen[/]")
        return False

    console.print("  [bold dim]▸ Services starten[/]")
    if subprocess.run(["docker", "compose", "-f", str(_COMPOSE), "up", "-d"]).returncode != 0:
        console.print("  [red]✗ Start fehlgeschlagen[/]")
        return False

    return True


def main() -> None:
    console.print()
    console.print(Panel(
        "[bold cyan]Clawmachine[/] – Projekt-Dashboard\n"
        "[dim]Baut + startet alle Docker-Services und zeigt jeden Container in einem eigenen Tile.\n"
        "Strg+C stoppt alle Container wieder.[/dim]",
        title="[bold]Dashboard[/]",
        border_style="cyan",
    ))

    if not _check_prerequisites():
        sys.exit(1)

    if not _build_and_start():
        sys.exit(1)

    services = _compose_services()
    if not services:
        console.print("  [red]✗ Keine Services in der docker-compose.yml gefunden[/]")
        sys.exit(1)

    tiles = {name: LogTile(name) for name in services}
    for tile in tiles.values():
        tile.start()

    layout = _build_layout(services)
    console.print("\n  [dim]Dashboard startet … (Strg+C stoppt alle Container)[/dim]")
    time.sleep(1)

    try:
        with Live(layout, console=console, refresh_per_second=4, screen=True):
            while True:
                for name, tile in tiles.items():
                    tile_height = layout[name].size or 20
                    layout[name].update(tile.render(tile_height - 2))
                time.sleep(0.25)
    except KeyboardInterrupt:
        pass
    finally:
        for tile in tiles.values():
            tile.stop()
        console.print("\n  [yellow]Stoppe alle Container …[/]")
        subprocess.run(["docker", "compose", "-f", str(_COMPOSE), "down"])
        console.print("  [green]✓ Alle Container gestoppt[/]")


if __name__ == "__main__":
    main()
