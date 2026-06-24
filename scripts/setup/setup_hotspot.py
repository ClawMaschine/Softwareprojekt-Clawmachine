#!/usr/bin/env python3
"""
Clawmachine Hotspot-Einrichtung
Wandelt ein WLAN-Interface in einen Access Point (SSID: praktikum) um.
Idempotent – sicher mehrfach ausführbar.
"""

import os
import re
import shutil
import subprocess
import sys
import time
from configparser import ConfigParser
from pathlib import Path


# ─── Rich (auto-install via apt oder pip) ────────────────────────────────────

def _ensure_rich() -> None:
    try:
        import rich  # noqa: F401
    except ImportError:
        print("Installiere 'rich' für die TUI …")
        result = subprocess.run(
            ["apt-get", "install", "-y", "-q", "python3-rich"],
            capture_output=True,
        )
        if result.returncode != 0:
            subprocess.run(
                [sys.executable, "-m", "pip", "install", "--quiet", "rich"],
                check=True,
            )


_ensure_rich()

from rich import box  # noqa: E402
from rich.console import Console  # noqa: E402
from rich.panel import Panel  # noqa: E402
from rich.prompt import Confirm, Prompt  # noqa: E402
from rich.rule import Rule  # noqa: E402
from rich.table import Table  # noqa: E402

console = Console()

# ─── Konfiguration (aus config.ini / config.local.ini) ───────────────────────

_SCRIPT_DIR = Path(__file__).parent
_REPO_ROOT = _SCRIPT_DIR.parent.parent
_CONFIG_INI = _REPO_ROOT / "config.ini"
_CONFIG_LOCAL_INI = _REPO_ROOT / "config.local.ini"


def _load_config() -> ConfigParser:
    cfg = ConfigParser()
    cfg.read([_CONFIG_INI, _CONFIG_LOCAL_INI])
    return cfg


_cfg = _load_config()

SSID = _cfg.get("wifi", "ssid", fallback="praktikum")
WIFI_PASSWORD = _cfg.get("wifi", "password", fallback="")
AP_IP = _cfg.get("hotspot", "ip", fallback="192.168.0.103")
AP_PREFIX = _cfg.getint("hotspot", "prefix", fallback=24)
DHCP_START = _cfg.get("hotspot", "dhcp_start", fallback="192.168.0.100")
DHCP_END = _cfg.get("hotspot", "dhcp_end", fallback="192.168.0.200")
DHCP_LEASE = _cfg.get("hotspot", "dhcp_lease", fallback="12h")
AP_CHANNEL = _cfg.getint("hotspot", "channel", fallback=6)

_CONFIG_SOURCE = (
    f"{_CONFIG_INI.name} + {_CONFIG_LOCAL_INI.name}"
    if _CONFIG_LOCAL_INI.exists()
    else _CONFIG_INI.name
)

HOSTAPD_CONF = Path("/etc/hostapd/clawmachine-hotspot.conf")
DNSMASQ_CONF = Path("/etc/dnsmasq.d/clawmachine-hotspot.conf")
NM_UNMANAGED_CONF = Path("/etc/NetworkManager/conf.d/99-clawmachine-unmanaged.conf")
WLAN_IP_SERVICE = Path("/etc/systemd/system/clawmachine-wlan-ip.service")


# ─── System-Hilfsfunktionen ───────────────────────────────────────────────────

def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def _run_required(cmd: list[str], label: str) -> None:
    console.print(f"    [dim]▸[/] {label}")
    result = _run(cmd)
    if result.returncode != 0:
        msg = (result.stderr or result.stdout).strip()
        console.print(f"    [red]✗ Fehlgeschlagen:[/] {msg}")
        sys.exit(1)
    console.print("    [green]✓[/]")


def _is_active(service: str) -> bool:
    return _run(["systemctl", "is-active", "--quiet", service]).returncode == 0


def _is_enabled(service: str) -> bool:
    return _run(["systemctl", "is-enabled", "--quiet", service]).returncode == 0


def _get_wireless_interfaces() -> list[str]:
    result = _run(["iw", "dev"])
    if result.returncode == 0 and result.stdout.strip():
        found = re.findall(r"Interface (\S+)", result.stdout)
        if found:
            return found
    ifaces = []
    for p in Path("/sys/class/net").iterdir():
        if (p / "wireless").is_dir() or (p / "phy80211").is_dir():
            ifaces.append(p.name)
    return ifaces


def _get_iface_mode(iface: str) -> str:
    result = _run(["iw", "dev", iface, "info"])
    m = re.search(r"\btype (\S+)", result.stdout)
    return m.group(1) if m else "?"


def _get_iface_ip(iface: str) -> str:
    result = _run(["ip", "addr", "show", iface])
    m = re.search(r"inet (\S+)", result.stdout)
    return m.group(1) if m else "–"


def _get_iface_driver(iface: str) -> str:
    driver_link = Path(f"/sys/class/net/{iface}/device/driver")
    if driver_link.exists():
        return driver_link.resolve().name
    return "?"


def _check_state(iface: str) -> dict:
    return {
        "hostapd_active": _is_active("hostapd"),
        "hostapd_enabled": _is_enabled("hostapd"),
        "dnsmasq_active": _is_active("dnsmasq"),
        "dnsmasq_enabled": _is_enabled("dnsmasq"),
        "hostapd_conf": HOSTAPD_CONF.exists(),
        "dnsmasq_conf": DNSMASQ_CONF.exists(),
        "iface_mode": _get_iface_mode(iface),
        "iface_ip": _get_iface_ip(iface),
        "nm_active": _is_active("NetworkManager"),
        "nm_unmanaged": NM_UNMANAGED_CONF.exists(),
    }


def _is_fully_configured(state: dict) -> bool:
    return (
        state["hostapd_active"]
        and state["dnsmasq_active"]
        and state["hostapd_conf"]
        and state["dnsmasq_conf"]
        and state["iface_mode"] == "AP"
        and AP_IP in state["iface_ip"]
    )


def _is_partially_configured(state: dict) -> bool:
    return state["hostapd_conf"] or state["dnsmasq_conf"]


# ─── TUI-Panels ───────────────────────────────────────────────────────────────

def _show_header() -> None:
    console.print()
    console.print(Panel(
        "[bold cyan]Clawmachine[/] – WLAN Hotspot Einrichtung\n"
        "[dim]Konfiguriert ein WLAN-Interface als Access Point für die ESP32-Boards.[/dim]",
        title="[bold]Hotspot Setup[/]",
        border_style="cyan",
    ))


def _show_interface_table(interfaces: list[str], selected: str) -> None:
    t = Table(box=box.ROUNDED, border_style="blue", title="Erkannte WLAN-Interfaces")
    t.add_column("Interface", style="bold")
    t.add_column("Treiber")
    t.add_column("Modus")
    t.add_column("Aktuelle IP")
    t.add_column("", width=15)
    for iface in interfaces:
        t.add_row(
            iface,
            _get_iface_driver(iface),
            _get_iface_mode(iface),
            _get_iface_ip(iface),
            "[green]◀ ausgewählt[/]" if iface == selected else "",
        )
    console.print(t)


def _show_current_state(state: dict, iface: str) -> None:
    def dot(ok: bool, yes: str = "", no: str = "") -> str:
        if ok:
            return f"[green]●[/] {yes}" if yes else "[green]●[/]"
        return f"[red]○[/] {no}" if no else "[red]○[/]"

    lines = [
        f"  {dot(state['hostapd_active'], 'aktiv', 'inaktiv')}  [dim]hostapd[/]",
        f"  {dot(state['dnsmasq_active'], 'aktiv', 'inaktiv')}  [dim]dnsmasq[/]",
        f"  {dot(state['hostapd_conf'], str(HOSTAPD_CONF), 'nicht vorhanden')}  [dim]hostapd.conf[/]",
        f"  {dot(state['dnsmasq_conf'], str(DNSMASQ_CONF), 'nicht vorhanden')}  [dim]dnsmasq.conf[/]",
        f"  {dot(state['iface_mode'] == 'AP', 'AP', state['iface_mode'])}  [dim]{iface} Modus[/]",
        f"  {dot(AP_IP in state['iface_ip'], state['iface_ip'], state['iface_ip'])}  [dim]{iface} IP[/]",
    ]

    full = _is_fully_configured(state)
    partial = _is_partially_configured(state)
    label = (
        "[green]Vollständig konfiguriert[/]" if full
        else "[yellow]Teilweise konfiguriert[/]" if partial
        else "[red]Nicht konfiguriert[/]"
    )
    border = "green" if full else "yellow" if partial else "red"
    console.print(Panel("\n".join(lines), title=f"Aktueller Status – {label}", border_style=border))


def _show_planned_config(iface: str) -> None:
    t = Table(box=box.ROUNDED, border_style="cyan", title="Geplante Hotspot-Konfiguration")
    t.add_column("Parameter", style="bold")
    t.add_column("Wert", style="cyan")
    t.add_row("Interface", iface)
    t.add_row("SSID", SSID)
    t.add_row("Passwort", "[dim](offen – kein Passwort)[/dim]" if not WIFI_PASSWORD else "***")
    t.add_row("IP-Adresse", f"{AP_IP}/{AP_PREFIX}")
    t.add_row("DHCP-Bereich", f"{DHCP_START} – {DHCP_END}")
    t.add_row("DHCP-Laufzeit", DHCP_LEASE)
    t.add_row("WLAN-Kanal", str(AP_CHANNEL))
    t.add_row("Konfiguration laut", _CONFIG_SOURCE)
    console.print(t)


def _show_planned_changes(state: dict, iface: str) -> None:
    rows: list[tuple[str, str]] = []
    if not shutil.which("hostapd"):
        rows.append(("[blue]📦[/]", "hostapd installieren (apt)"))
    if not shutil.which("dnsmasq"):
        rows.append(("[blue]📦[/]", "dnsmasq installieren (apt)"))
    if state["nm_active"] and not state["nm_unmanaged"]:
        rows.append(("[yellow]✏ [/]", f"NetworkManager: {iface} als unverwaltet eintragen"))
    action = "erstellen" if not state["hostapd_conf"] else "aktualisieren"
    rows.append(("[green]➕[/]" if not state["hostapd_conf"] else "[yellow]✏ [/]", f"{HOSTAPD_CONF} {action}"))
    action = "erstellen" if not state["dnsmasq_conf"] else "aktualisieren"
    rows.append(("[green]➕[/]" if not state["dnsmasq_conf"] else "[yellow]✏ [/]", f"{DNSMASQ_CONF} {action}"))
    rows.append(("[green]➕[/]", f"{WLAN_IP_SERVICE} erstellen (statische IP beim Boot)"))
    if not state["hostapd_active"]:
        rows.append(("[cyan]▶ [/]", "hostapd aktivieren + starten"))
    if not state["dnsmasq_active"]:
        rows.append(("[cyan]▶ [/]", "dnsmasq aktivieren + starten"))

    t = Table(box=box.ROUNDED, border_style="yellow", title="Geplante Änderungen")
    t.add_column("", width=3)
    t.add_column("Aktion")
    for icon, desc in rows:
        t.add_row(icon, desc)
    console.print(t)


def _show_final_state(iface: str) -> None:
    console.print()
    console.print(Rule("[bold]Ergebnis[/]", style="green"))
    state = _check_state(iface)

    t = Table(box=box.ROUNDED, border_style="green")
    t.add_column("Komponente", style="bold")
    t.add_column("Status")
    t.add_column("Details", style="dim")

    def s(ok: bool, ok_text: str = "aktiv", nok_text: str = "inaktiv") -> str:
        return f"[green]● {ok_text}[/]" if ok else f"[red]○ {nok_text}[/]"

    t.add_row("hostapd", s(state["hostapd_active"]), str(HOSTAPD_CONF))
    t.add_row("dnsmasq", s(state["dnsmasq_active"]), str(DNSMASQ_CONF))
    mode = state["iface_mode"]
    t.add_row(f"{iface} Modus", s(mode == "AP", "AP", mode), "")
    ip = state["iface_ip"]
    t.add_row(f"{iface} IP", s(AP_IP in ip, ip, ip), "")
    t.add_row("SSID", s(state["hostapd_active"], SSID, "–"), "")
    console.print(t)

    if _is_fully_configured(state):
        console.print(Panel(
            f"[green]✓ Hotspot erfolgreich eingerichtet[/]\n\n"
            f"  SSID:  [bold]{SSID}[/]\n"
            f"  IP:    [bold]{AP_IP}[/]\n"
            f"  DHCP:  {DHCP_START} – {DHCP_END}\n\n"
            "[dim]ESP32-Boards können sich jetzt mit dem WLAN verbinden.[/dim]",
            title="[green]Setup abgeschlossen[/]",
            border_style="green",
        ))
    else:
        console.print(Panel(
            "[yellow]⚠ Hotspot möglicherweise noch nicht vollständig aktiv.[/]\n"
            "Logs prüfen:\n"
            "  [dim]journalctl -u hostapd -u dnsmasq --no-pager -n 30[/dim]",
            border_style="yellow",
        ))


# ─── Konfigurationsschritte ───────────────────────────────────────────────────

def _write_hostapd_conf(iface: str) -> None:
    HOSTAPD_CONF.parent.mkdir(parents=True, exist_ok=True)
    if WIFI_PASSWORD:
        wpa_block = (
            f"wpa=2\n"
            f"wpa_passphrase={WIFI_PASSWORD}\n"
            f"wpa_key_mgmt=WPA-PSK\n"
            f"rsn_pairwise=CCMP\n"
        )
    else:
        wpa_block = ""
    HOSTAPD_CONF.write_text(
        f"interface={iface}\n"
        f"driver=nl80211\n"
        f"ssid={SSID}\n"
        f"hw_mode=g\n"
        f"channel={AP_CHANNEL}\n"
        f"ieee80211n=1\n"
        f"wmm_enabled=0\n"
        f"macaddr_acl=0\n"
        f"auth_algs=1\n"
        f"ignore_broadcast_ssid=0\n"
        + wpa_block
    )


def _write_dnsmasq_conf(iface: str) -> None:
    DNSMASQ_CONF.parent.mkdir(parents=True, exist_ok=True)
    DNSMASQ_CONF.write_text(
        f"interface={iface}\n"
        f"bind-interfaces\n"
        f"dhcp-range={DHCP_START},{DHCP_END},{DHCP_LEASE}\n"
        f"dhcp-option=option:router,{AP_IP}\n"
        f"dhcp-option=option:dns-server,{AP_IP}\n"
    )


def _write_wlan_ip_service(iface: str) -> None:
    WLAN_IP_SERVICE.write_text(
        "[Unit]\n"
        f"Description=Clawmachine – statische IP für {iface}\n"
        "After=network-pre.target\n"
        "Before=hostapd.service dnsmasq.service\n"
        "Wants=network-pre.target\n\n"
        "[Service]\n"
        "Type=oneshot\n"
        f"ExecStart=/sbin/ip addr flush dev {iface}\n"
        f"ExecStart=/sbin/ip addr add {AP_IP}/{AP_PREFIX} dev {iface}\n"
        f"ExecStart=/sbin/ip link set {iface} up\n"
        "RemainAfterExit=yes\n\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n"
    )


def _patch_hostapd_default() -> None:
    default_path = Path("/etc/default/hostapd")
    if not default_path.exists():
        return
    content = default_path.read_text()
    patched = re.sub(r"#?DAEMON_CONF=.*", f'DAEMON_CONF="{HOSTAPD_CONF}"', content)
    if "DAEMON_CONF" not in patched:
        patched += f'\nDAEMON_CONF="{HOSTAPD_CONF}"\n'
    default_path.write_text(patched)


def _configure_nm_unmanaged(iface: str) -> None:
    NM_UNMANAGED_CONF.parent.mkdir(parents=True, exist_ok=True)
    NM_UNMANAGED_CONF.write_text(
        "[keyfile]\n"
        f"unmanaged-devices=interface-name:{iface}\n"
    )


def _apply(iface: str, state: dict) -> None:
    console.print()
    console.print(Rule("[bold]Konfiguration wird angewendet[/]", style="cyan"))
    console.print()

    # Pakete installieren
    packages = []
    if not shutil.which("hostapd"):
        packages.append("hostapd")
    if not shutil.which("dnsmasq"):
        packages.append("dnsmasq")
    if packages:
        _run_required(["apt-get", "install", "-y"] + packages, f"Installiere {', '.join(packages)}")

    # Services stoppen (Fehler ignorieren falls noch nicht installiert)
    _run(["systemctl", "stop", "hostapd"])
    _run(["systemctl", "stop", "dnsmasq"])

    # NetworkManager: Interface als unverwaltet markieren
    if state["nm_active"]:
        console.print("    [dim]▸[/] NetworkManager: Interface als unverwaltet eintragen")
        _configure_nm_unmanaged(iface)
        console.print("    [green]✓[/]")
        _run_required(["systemctl", "restart", "NetworkManager"], "NetworkManager neu starten")
        time.sleep(2)

    # Statische IP sofort setzen
    _run(["ip", "addr", "flush", "dev", iface])
    _run_required(["ip", "addr", "add", f"{AP_IP}/{AP_PREFIX}", "dev", iface], f"Setze IP {AP_IP}/{AP_PREFIX} auf {iface}")
    _run_required(["ip", "link", "set", iface, "up"], f"Interface {iface} hochfahren")

    # Konfigurationsdateien schreiben
    console.print(f"    [dim]▸[/] Schreibe {HOSTAPD_CONF}")
    _write_hostapd_conf(iface)
    console.print("    [green]✓[/]")

    console.print(f"    [dim]▸[/] Schreibe {DNSMASQ_CONF}")
    _write_dnsmasq_conf(iface)
    console.print("    [green]✓[/]")

    console.print(f"    [dim]▸[/] Schreibe {WLAN_IP_SERVICE}")
    _write_wlan_ip_service(iface)
    console.print("    [green]✓[/]")

    console.print(f"    [dim]▸[/] Passe /etc/default/hostapd an")
    _patch_hostapd_default()
    console.print("    [green]✓[/]")

    # Services aktivieren und starten
    _run(["systemctl", "unmask", "hostapd"])
    _run_required(["systemctl", "daemon-reload"], "systemd daemon-reload")
    _run_required(
        ["systemctl", "enable", "--now", "clawmachine-wlan-ip.service"],
        "Aktiviere clawmachine-wlan-ip (statische IP)",
    )
    _run_required(["systemctl", "enable", "--now", "hostapd"], "Aktiviere + starte hostapd")
    _run_required(["systemctl", "enable", "--now", "dnsmasq"], "Aktiviere + starte dnsmasq")


# ─── Hauptprogramm ────────────────────────────────────────────────────────────

def main() -> None:
    if os.geteuid() != 0:
        console.print("[red]✗ Bitte als root ausführen (sudo python3 setup_hotspot.py).[/]")
        sys.exit(1)

    _show_header()

    # 1. Interface-Erkennung
    console.print()
    console.print(Rule("[bold]1 · Interface-Erkennung[/]", style="blue"))
    interfaces = _get_wireless_interfaces()

    if not interfaces:
        console.print("[red]✗ Keine WLAN-Interfaces gefunden. Prüfe Hardware/Treiber.[/]")
        sys.exit(1)

    default_iface = "wlan0" if "wlan0" in interfaces else interfaces[0]
    _show_interface_table(interfaces, default_iface)

    if len(interfaces) > 1:
        iface = Prompt.ask("  Interface auswählen", choices=interfaces, default=default_iface)
    else:
        iface = default_iface
        console.print(f"  → [cyan]{iface}[/] wird verwendet (einziges Interface)")

    # 2. Aktueller Zustand
    console.print()
    console.print(Rule("[bold]2 · Aktueller Zustand[/]", style="blue"))
    state = _check_state(iface)
    _show_current_state(state, iface)

    if _is_fully_configured(state):
        console.print()
        if not Confirm.ask("  Hotspot ist bereits aktiv. Neu konfigurieren?", default=False):
            _show_final_state(iface)
            return

    # 3. Geplante Konfiguration
    console.print()
    console.print(Rule("[bold]3 · Geplante Konfiguration[/]", style="blue"))
    _show_planned_config(iface)
    console.print()
    _show_planned_changes(state, iface)

    # 4. Bestätigung
    console.print()
    if not Confirm.ask("  Konfiguration anwenden?", default=True):
        console.print("[yellow]Abgebrochen.[/]")
        sys.exit(0)

    # 5. Anwenden
    _apply(iface, state)

    # 6. Ergebnis
    _show_final_state(iface)


if __name__ == "__main__":
    main()
