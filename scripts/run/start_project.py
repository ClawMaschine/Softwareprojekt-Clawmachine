#!/usr/bin/env python3
"""
Clawmachine – Projekt starten TUI
Startet MQTT-Broker (Docker) und Python-Server mit Live-Log-Ausgabe.
"""

import re
import shutil
import subprocess
import sys
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

from rich import box  # noqa: E402
from rich.console import Console  # noqa: E402
from rich.panel import Panel  # noqa: E402
from rich.rule import Rule  # noqa: E402
from rich.table import Table  # noqa: E402

console = Console()

_SCRIPT_DIR  = Path(__file__).parent
_REPO_ROOT   = _SCRIPT_DIR.parent.parent
_VENV        = _REPO_ROOT / ".venv"
_COMPOSE     = _REPO_ROOT / "docker" / "docker-compose.yml"

# ─── Log-Level-Erkennung ─────────────────────────────────────────────────────
# Erkennt Level aus: uvicorn, FastAPI, paho-mqtt, apt, docker

_LEVEL_PATTERNS = [
    (re.compile(r"\b(CRITICAL|FATAL)\b"),                                            "CRIT ", "bold red"),
    (re.compile(r"\b(ERROR|error|Fehler|failed|FAILED|Exception|Traceback)\b"),      "ERROR", "red"),
    (re.compile(r"\b(WARNING|Warning|WARN|Warnung|DeprecationWarning)\b"),           "WARN ", "yellow"),
    (re.compile(r"\bDEBUG\b"),                                                       "DEBUG", "dim blue"),
    (re.compile(r"\b(Started|Running|Connected|Startup|ready|started|connected)\b"), "INFO ", "green"),
    (re.compile(r"^\s*(INFO|info)\b|INFO:"),                                         "INFO ", "cyan"),
    (re.compile(r"\b(Pulling|pulled|Creating|created|Starting|started|done)\b"),     "INFO ", "cyan"),
]


def _classify(line: str) -> tuple[str, str]:
    for pattern, level, color in _LEVEL_PATTERNS:
        if pattern.search(line):
            return level, color
    return "INFO ", "dim"


def _log(line: str) -> None:
    line = line.rstrip()
    if not line:
        return
    level, color = _classify(line)
    console.print(f"  [{color}][{level}][/{color}] {line}")


# ─── Subprocess-Hilfsfunktionen ───────────────────────────────────────────────

def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def _stream(cmd: list[str], title: str) -> bool:
    """Führt einen Befehl aus und streamt Ausgabe mit Log-Level-Tags."""
    console.print(f"\n  [bold dim]▸ {title}[/]")
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        _log(line)
    process.wait()
    if process.returncode != 0:
        console.print(f"  [red]✗ {title} fehlgeschlagen (exit {process.returncode})[/]")
        return False
    console.print(f"  [green]✓ {title}[/]")
    return True


# ─── Zustandsabfragen ─────────────────────────────────────────────────────────

def _container_running(name: str) -> bool:
    result = _run(["docker", "ps", "-q", "--filter", f"name={name}"])
    return bool(result.stdout.strip())


def _container_exists(name: str) -> bool:
    result = _run(["docker", "ps", "-aq", "--filter", f"name={name}"])
    return bool(result.stdout.strip())


def _docker_compose_services() -> list[dict]:
    """Parst docker compose ps Ausgabe zu einer Liste von Dicts."""
    result = _run(["docker", "compose", "-f", str(_COMPOSE), "ps"])
    lines = result.stdout.strip().splitlines()
    if len(lines) < 2:
        return []
    services = []
    for line in lines[1:]:  # Kopfzeile überspringen
        parts = line.split()
        if not parts:
            continue
        name   = parts[0]
        status = next((p for p in parts if p.lower().startswith(("up", "exit", "creat", "dead", "remov"))), "?")
        ports  = " ".join(p for p in parts if "->" in p or p.startswith("0.0.0.0"))
        services.append({"name": name, "status": status, "ports": ports})
    return services


# ─── TUI-Panels ───────────────────────────────────────────────────────────────

def _show_header() -> None:
    console.print()
    console.print(Panel(
        "[bold cyan]Clawmachine[/] – Projekt starten\n"
        "[dim]Startet MQTT-Broker (Docker) und Python-Server mit Live-Logs.[/dim]",
        title="[bold]Start[/]",
        border_style="cyan",
    ))


def _show_state() -> None:
    t = Table(box=box.ROUNDED, border_style="blue", title="Systemzustand")
    t.add_column("Komponente", style="bold")
    t.add_column("Status")
    t.add_column("Details", style="dim")

    docker_ok = bool(shutil.which("docker"))
    docker_ver = _run(["docker", "--version"]).stdout.strip() if docker_ok else ""
    t.add_row("Docker", "[green]● vorhanden[/]" if docker_ok else "[red]○ fehlt[/]", docker_ver)

    if docker_ok and _COMPOSE.exists():
        services = _docker_compose_services()
        if services:
            for svc in services:
                running = svc["status"].lower().startswith("up")
                t.add_row(
                    f"  {svc['name']}",
                    "[green]● running[/]" if running else f"[yellow]○ {svc['status']}[/]",
                    svc["ports"],
                )
        else:
            t.add_row("  (keine Container)", "[dim]–[/]", "noch nicht gestartet")

    venv_ok = (_VENV / "bin" / "activate").exists()
    t.add_row(".venv", "[green]● vorhanden[/]" if venv_ok else "[red]○ fehlt[/]", str(_VENV) if venv_ok else "setup_server.py ausführen")

    console.print(t)


def _check_prerequisites() -> bool:
    ok = True
    if not shutil.which("docker"):
        console.print("  [red]✗ docker nicht gefunden – bitte setup_server.py ausführen[/]")
        ok = False
    if not _COMPOSE.exists():
        console.print(f"  [red]✗ {_COMPOSE} nicht gefunden[/]")
        ok = False
    if not (_VENV / "bin" / "activate").exists():
        console.print("  [red]✗ .venv fehlt – bitte setup_server.py ausführen[/]")
        ok = False
    return ok


# ─── Start-Schritte ───────────────────────────────────────────────────────────

def _start_broker() -> bool:
    console.print()
    console.print(Rule("[bold]1 · MQTT-Broker (Docker)[/]", style="blue"))

    ok = _stream(
        ["docker", "compose", "-f", str(_COMPOSE), "up", "-d"],
        "MQTT-Broker starten",
    )
    if ok:
        services = _docker_compose_services()
        for svc in services:
            running = svc["status"].lower().startswith("up")
            icon = "[green]●[/]" if running else "[yellow]○[/]"
            console.print(f"  {icon} [bold]{svc['name']}[/]  [dim]{svc['ports']}[/dim]")
    return ok


def _stream_server() -> None:
    console.print()
    console.print(Rule("[bold]2 · Python-Server[/]", style="blue"))
    console.print("  [dim](Strg+C beendet den Server – MQTT-Broker läuft weiter)[/dim]\n")

    python = str(_VENV / "bin" / "python")
    process = subprocess.Popen(
        [python, "-m", "python_server"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=str(_REPO_ROOT),
    )

    try:
        for line in process.stdout:
            _log(line)
        process.wait()
    except KeyboardInterrupt:
        console.print("\n  [yellow]Server wird beendet …[/]")
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
        console.print("  [green]✓ Server gestoppt[/]")


# ─── Hauptprogramm ────────────────────────────────────────────────────────────

def main() -> None:
    _show_header()

    # Zustand & Checks
    console.print()
    console.print(Rule("[bold]1 · Systemzustand[/]", style="blue"))
    if not _check_prerequisites():
        sys.exit(1)
    _show_state()

    # Broker starten
    if not _start_broker():
        console.print("[red]✗ MQTT-Broker konnte nicht gestartet werden.[/]")
        sys.exit(1)

    # Server mit Live-Logs
    _stream_server()

    # Abschluss
    console.print()
    console.print(Panel(
        "[green]✓ Server beendet[/]\n\n"
        "[dim]MQTT-Broker läuft noch im Hintergrund.\n"
        f"Stoppen:  docker compose -f docker/docker-compose.yml down[/dim]",
        border_style="green",
    ))


if __name__ == "__main__":
    main()
