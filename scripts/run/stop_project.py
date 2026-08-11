#!/usr/bin/env python3
"""
Clawmachine – Projekt stoppen TUI
Stoppt alle Docker-Services (MQTT-Broker, CaptiveDNS, Frontend, Server).
"""

import shutil
import subprocess
import sys
from pathlib import Path


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
from rich.panel import Panel  # noqa: E402
from rich.rule import Rule  # noqa: E402

console = Console()

_SCRIPT_DIR = Path(__file__).parent
_REPO_ROOT = _SCRIPT_DIR.parent.parent
_COMPOSE = _REPO_ROOT / "docker" / "docker-compose.yml"


def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def _stream(cmd: list[str], title: str) -> bool:
    console.print(f"\n  [bold dim]▸ {title}[/]")
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        line = line.rstrip()
        if line:
            console.print(f"  {line}")
    process.wait()
    if process.returncode != 0:
        console.print(f"  [red]✗ {title} fehlgeschlagen (exit {process.returncode})[/]")
        return False
    console.print(f"  [green]✓ {title}[/]")
    return True


def main() -> None:
    console.print()
    console.print(Panel(
        "[bold cyan]Clawmachine[/] – Projekt stoppen\n"
        "[dim]Stoppt alle Docker-Services (MQTT-Broker, CaptiveDNS, Frontend, Server).[/dim]",
        title="[bold]Stop[/]",
        border_style="cyan",
    ))

    if not shutil.which("docker"):
        console.print("\n  [red]✗ docker nicht gefunden[/]")
        sys.exit(1)
    if not _COMPOSE.exists():
        console.print(f"\n  [red]✗ {_COMPOSE} nicht gefunden[/]")
        sys.exit(1)

    console.print()
    console.print(Rule("[bold]Docker Services stoppen[/]", style="blue"))
    if not _stream(["docker", "compose", "-f", str(_COMPOSE), "down"], "Services stoppen"):
        sys.exit(1)

    console.print()
    console.print(Panel(
        "[green]✓ Clawmachine gestoppt[/]",
        border_style="green",
    ))


if __name__ == "__main__":
    main()
