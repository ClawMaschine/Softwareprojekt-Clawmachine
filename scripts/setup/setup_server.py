#!/usr/bin/env python3
"""
Clawmachine – Server-Setup TUI
Installiert Docker, Python-Abhängigkeiten und richtet den WLAN-Hotspot ein.
Idempotent – sicher mehrfach ausführbar.
"""

import argparse
import os
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
from rich.prompt import Confirm  # noqa: E402
from rich.rule import Rule  # noqa: E402
from rich.table import Table  # noqa: E402

console = Console()

_SCRIPT_DIR = Path(__file__).parent
_REPO_ROOT = _SCRIPT_DIR.parent.parent
_VENV = _REPO_ROOT / ".venv"

# ─── Log-Level-Erkennung ─────────────────────────────────────────────────────

_LEVEL_PATTERNS = [
    (re.compile(r"\b(CRITICAL|FATAL)\b"),                               "CRIT ", "bold red"),
    (re.compile(r"\b(ERROR|error|Fehler|failed|FAILED)\b"),             "ERROR", "red"),
    (re.compile(r"\b(WARNING|Warning|WARN|Warnung)\b"),                 "WARN ", "yellow"),
    (re.compile(r"\b(DEBUG)\b"),                                        "DEBUG", "dim"),
    (re.compile(r"\b(INFO|info|Hinweis|note|Note)\b"),                  "INFO ", "cyan"),
    (re.compile(r"\b(done|Done|OK|Success|success|bereits|already)\b"), "OK   ", "green"),
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


def _bash(script: str) -> subprocess.CompletedProcess:
    return subprocess.run(["bash", "-c", script], capture_output=True, text=True)


def _stream(cmd: list[str] | str, title: str, shell: bool = False) -> bool:
    """Führt einen Befehl aus und streamt die Ausgabe mit Log-Level-Tags."""
    console.print(f"\n  [bold dim]▸ {title}[/]")
    if shell:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, shell=True)
    else:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    for line in process.stdout:
        _log(line)
    process.wait()
    if process.returncode != 0:
        console.print(f"  [red]✗ Fehlgeschlagen (exit {process.returncode})[/]")
        return False
    console.print(f"  [green]✓ {title}[/]")
    return True


# ─── Zustandsabfragen ─────────────────────────────────────────────────────────

def _docker_version() -> str:
    return _run(["docker", "--version"]).stdout.strip()


def _check_state() -> dict:
    docker_ok = bool(shutil.which("docker"))
    return {
        "docker":            docker_ok,
        "docker_compose":    _run(["docker", "compose", "version"]).returncode == 0 if docker_ok else False,
        "docker_version":    _docker_version() if docker_ok else "",
        "python3":           bool(shutil.which("python3")),
        "pip":               bool(shutil.which("pip3")),
        "venv":              (_VENV / "bin" / "activate").exists(),
        "mosquitto_clients": bool(shutil.which("mosquitto_pub")),
        "python3_rich":      _run([sys.executable, "-c", "import rich"]).returncode == 0,
    }


# ─── TUI-Panels ───────────────────────────────────────────────────────────────

def _show_header() -> None:
    console.print()
    console.print(Panel(
        "[bold cyan]Clawmachine[/] – Server Setup\n"
        "[dim]Installiert Docker, Python-Abhängigkeiten und richtet den WLAN-Hotspot ein.[/dim]",
        title="[bold]Server Setup[/]",
        border_style="cyan",
    ))


def _show_state(state: dict) -> None:
    def dot(ok: bool, ok_t: str = "vorhanden", no_t: str = "fehlt") -> str:
        return f"[green]● {ok_t}[/]" if ok else f"[red]○ {no_t}[/]"

    t = Table(box=box.ROUNDED, border_style="blue", title="Aktueller Systemzustand")
    t.add_column("Komponente", style="bold")
    t.add_column("Status")
    t.add_column("Details", style="dim")
    t.add_row("docker",           dot(state["docker"]),            state["docker_version"])
    t.add_row("docker compose",   dot(state["docker_compose"]),    "")
    t.add_row("python3",          dot(state["python3"]),           "")
    t.add_row(".venv",            dot(state["venv"]),              str(_VENV) if state["venv"] else "")
    t.add_row("mosquitto-clients",dot(state["mosquitto_clients"]), "")
    console.print(t)


def _show_planned(state: dict) -> None:
    rows: list[tuple[str, str]] = []
    if not state["docker"]:
        rows.append(("[blue]📦[/]", "Docker + Docker Compose installieren (offizielle apt-Quelle)"))
    else:
        rows.append(("[green]✓ [/]", f"Docker bereits installiert ({state['docker_version']})"))
    if not state["mosquitto_clients"]:
        rows.append(("[blue]📦[/]", "python3-pip, python3-venv, mosquitto-clients installieren"))
    if not state["venv"]:
        rows.append(("[green]➕[/]", ".venv erstellen + Python-Abhängigkeiten installieren"))
    else:
        rows.append(("[yellow]✏ [/]", "Python-Abhängigkeiten aktualisieren"))
    rows.append(("[cyan]▶ [/]", "WLAN-Hotspot einrichten (setup_hotspot.py)"))
    mosquitto_passwords_path = _REPO_ROOT / "docker" / "mosquitto" / "config" / "passwords"
    if not mosquitto_passwords_path.exists():
        rows.append(("[green]➕[/]", "Mosquitto-Passwortdatei erzeugen (aus config.ini)"))

    t = Table(box=box.ROUNDED, border_style="yellow", title="Geplante Schritte")
    t.add_column("", width=3)
    t.add_column("Schritt")
    for icon, desc in rows:
        t.add_row(icon, desc)
    console.print(t)


# ─── Installationsschritte ────────────────────────────────────────────────────

def _install_docker() -> None:
    console.print()
    console.print(Rule("[bold]Docker Installation[/]", style="blue"))

    _stream(["apt-get", "install", "-y", "ca-certificates", "curl"], "ca-certificates + curl")
    _stream(["install", "-m", "0755", "-d", "/etc/apt/keyrings"], "Erstelle /etc/apt/keyrings")

    console.print("\n  [bold dim]▸ Docker GPG-Key herunterladen[/]")
    r = subprocess.run(
        ["curl", "-fsSL", "https://download.docker.com/linux/ubuntu/gpg",
         "-o", "/etc/apt/keyrings/docker.asc"],
        capture_output=True,
    )
    subprocess.run(["chmod", "a+r", "/etc/apt/keyrings/docker.asc"])
    console.print("  [green]✓ GPG-Key[/]" if r.returncode == 0 else "  [red]✗ GPG-Key Download fehlgeschlagen[/]")

    console.print("\n  [bold dim]▸ Docker apt-Quelle einrichten[/]")
    codename = _bash(". /etc/os-release && echo ${UBUNTU_CODENAME:-$VERSION_CODENAME}").stdout.strip()
    arch     = _bash("dpkg --print-architecture").stdout.strip()
    Path("/etc/apt/sources.list.d/docker.sources").write_text(
        f"Types: deb\n"
        f"URIs: https://download.docker.com/linux/ubuntu\n"
        f"Suites: {codename}\n"
        f"Components: stable\n"
        f"Architectures: {arch}\n"
        f"Signed-By: /etc/apt/keyrings/docker.asc\n"
    )
    console.print("  [green]✓ apt-Quelle[/]")

    _stream(["apt-get", "update"], "apt-get update")
    _stream(
        ["apt-get", "install", "-y",
         "docker-ce", "docker-ce-cli", "containerd.io",
         "docker-buildx-plugin", "docker-compose-plugin"],
        "Docker CE + Compose installieren",
    )


def _add_to_docker_group() -> None:
    user = os.environ.get("SUDO_USER") or os.environ.get("USER", "")
    if not user or user == "root":
        console.print("\n  [yellow]⚠ Kein Benutzer erkannt – docker-Gruppe manuell hinzufügen.[/]")
        return
    result = subprocess.run(["usermod", "-aG", "docker", user], capture_output=True)
    if result.returncode == 0:
        console.print(f"\n  [green]✓ {user} zur docker-Gruppe hinzugefügt[/]")
        console.print(f"  [yellow]⚠ Neu anmelden, damit die Gruppe aktiv wird.[/]")
    else:
        console.print(f"\n  [yellow]⚠ usermod fehlgeschlagen (Gruppe ggf. schon gesetzt)[/]")


def _install_python_deps() -> None:
    console.print()
    console.print(Rule("[bold]Python-Abhängigkeiten[/]", style="blue"))
    script = _SCRIPT_DIR / "install_python_dependencies.sh"
    _stream(["bash", str(script)], "Python-Dependencies")


def _setup_mosquitto_password() -> None:
    # mosquitto.conf verlangt password_file (allow_anonymous false) — ohne
    # diese Datei kommt der Broker-Container gar nicht erst hoch (Restart-
    # Schleife, "Unable to open pwfile"). setup_server.sh hat das schon immer
    # gemacht, hier fehlte der Schritt bisher komplett.
    mosquitto_config_directory = _REPO_ROOT / "docker" / "mosquitto" / "config"
    mosquitto_passwords_path = mosquitto_config_directory / "passwords"

    if mosquitto_passwords_path.exists():
        console.print("\n  [green]✓ Mosquitto-Passwortdatei bereits vorhanden[/]")
        return

    console.print("\n  [bold dim]▸ Mosquitto-Passwortdatei erzeugen (nicht in Git, siehe .gitignore)[/]")

    # Zugangsdaten aus config.ini/config.local.ini wiederverwenden statt hier
    # erneut zu parsen — siehe python_server/configuration_loader.py.
    credentials_result = subprocess.run(
        [
            sys.executable, "-c",
            "from python_server.configuration_loader import load_mqtt_configuration\n"
            "mqtt_configuration = load_mqtt_configuration()\n"
            "print(mqtt_configuration.username)\n"
            "print(mqtt_configuration.password)\n",
        ],
        cwd=_REPO_ROOT,
        capture_output=True,
        text=True,
    )
    lines = credentials_result.stdout.strip().splitlines()
    if credentials_result.returncode != 0 or len(lines) < 2:
        console.print("  [red]✗ Konnte MQTT-Zugangsdaten nicht aus config.ini lesen[/]")
        console.print(f"  [dim]{credentials_result.stderr.strip()}[/]")
        return

    mqtt_username, mqtt_password = lines[0], lines[1]

    created = _stream(
        [
            "docker", "run", "--rm",
            "-v", f"{mosquitto_config_directory}:/mosquitto/config",
            "eclipse-mosquitto:latest",
            "mosquitto_passwd", "-b", "-c", "/mosquitto/config/passwords",
            mqtt_username, mqtt_password,
        ],
        f"Mosquitto-Passwortdatei erzeugen (Benutzer '{mqtt_username}')",
    )
    if not created:
        return

    for path, mode in (
        (mosquitto_passwords_path, 0o644),
        (mosquitto_config_directory / "mosquitto.conf", 0o644),
        (mosquitto_config_directory, 0o755),
    ):
        try:
            path.chmod(mode)
        except OSError as error:
            console.print(f"  [yellow]⚠ Berechtigungen für {path} konnten nicht gesetzt werden: {error}[/]")


# ─── Hauptprogramm ────────────────────────────────────────────────────────────

def _parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Clawmachine – Server-Setup (Docker, Python-Abhängigkeiten, WLAN-Hotspot).",
    )
    parser.add_argument(
        "-y", "--yes",
        action="store_true",
        help="Alle Rückfragen mit dem Standardwert beantworten (auch im Hotspot-Setup)",
    )
    return parser.parse_args()


def main() -> None:
    arguments = _parse_arguments()

    if os.geteuid() != 0:
        console.print("[red]✗ Bitte als root ausführen (sudo python3 setup_server.py [-y]).[/]")
        sys.exit(1)

    _show_header()

    # 1. Zustand
    console.print()
    console.print(Rule("[bold]1 · Systemzustand[/]", style="blue"))
    state = _check_state()
    _show_state(state)

    # 2. Plan
    console.print()
    console.print(Rule("[bold]2 · Geplante Schritte[/]", style="blue"))
    _show_planned(state)

    # 3. Bestätigung
    console.print()
    if arguments.yes:
        console.print("  Setup ausführen? [dim](-y: ja)[/]")
    elif not Confirm.ask("  Setup ausführen?", default=True):
        console.print("[yellow]Abgebrochen.[/]")
        sys.exit(0)

    # 4. Pakete aktualisieren
    console.print()
    console.print(Rule("[bold]3 · Pakete aktualisieren[/]", style="cyan"))
    _stream(["apt-get", "update"], "apt-get update")
    _stream(["apt-get", "upgrade", "-y"], "apt-get upgrade")

    # 5. Docker
    console.print()
    console.print(Rule("[bold]4 · Docker[/]", style="cyan"))
    if not state["docker"]:
        _install_docker()
    else:
        console.print(f"\n  [green]✓ Docker bereits installiert – {state['docker_version']}[/]")

    _add_to_docker_group()

    # 6. System-Pakete
    console.print()
    console.print(Rule("[bold]5 · System-Pakete[/]", style="cyan"))
    pkgs = ["python3", "python3-pip", "python3-venv", "mosquitto-clients"]
    if not state["python3_rich"]:
        pkgs.append("python3-rich")
    _stream(["apt-get", "install", "-y"] + pkgs, f"Installiere {', '.join(pkgs)}")

    # 7. Python-Abhängigkeiten
    _install_python_deps()

    # 8. Hotspot
    console.print()
    console.print(Rule("[bold]6 · WLAN-Hotspot[/]", style="cyan"))
    hotspot = _SCRIPT_DIR / "setup_hotspot.py"
    hotspot_command = [sys.executable, str(hotspot)]
    if arguments.yes:
        hotspot_command.append("--yes")
    subprocess.run(hotspot_command)

    # 9. Mosquitto-Zugangsdaten
    console.print()
    console.print(Rule("[bold]7 · Mosquitto-Zugangsdaten[/]", style="cyan"))
    _setup_mosquitto_password()

    # Ergebnis
    console.print()
    console.print(Rule("[bold]Ergebnis[/]", style="green"))
    _show_state(_check_state())
    console.print(Panel(
        "[green]✓ Setup abgeschlossen[/]\n\n"
        "Nächste Schritte:\n"
        "  [dim]1. Neu anmelden (docker-Gruppe wirksam)\n"
        f"  2. Projekt starten:  python3 scripts/run/start_project.py[/dim]",
        border_style="green",
    ))


if __name__ == "__main__":
    main()
