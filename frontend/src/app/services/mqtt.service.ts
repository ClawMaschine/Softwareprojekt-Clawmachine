import { Injectable, NgZone, OnDestroy } from '@angular/core';
import { BehaviorSubject, Subscription, interval } from 'rxjs';
import mqtt, { MqttClient } from 'mqtt';
import { environment } from '../../environments/environment';

export interface DeviceState {
  id: string;
  label: string;
  isOnline: boolean;
  uptimeMs: number | null;
  lastSeen: Date | null;
}

export interface MessageLog {
  topic: string;
  payload: string;
  timestamp: Date;
}

export type ConnectionStatus = 'disconnected' | 'connecting' | 'connected' | 'error';

const CLAWMACHINE_TOPIC_PREFIX = 'clawmachine/';

// Haupt-Steuertopic des Servers — Befehle mit X:/Y:/Z:/claw:-Präfix werden
// dort unverändert an den Motor-Controller weitergeleitet (siehe
// MOTOR_COMMAND_PREFIXES in python_server/clawmachine/claw_machine.py).
const CONTROL_TOPIC = 'clawmachine/web_interface/command';

// Geräteliste: das Webinterface fragt aktiv beim Server nach (statt der
// Server sie ungefragt zu pushen) und bekommt den aktuellen Stand der
// server-seitigen DeviceRegistry als JSON-Array zurück — siehe
// DEVICE_LIST_REQUEST_TOPIC/DEVICE_LIST_TOPIC in claw_machine.py.
const DEVICE_LIST_REQUEST_TOPIC = 'clawmachine/web_interface/devices/request';
const DEVICE_LIST_TOPIC = 'clawmachine/web_interface/devices';
const DEVICE_LIST_REFRESH_INTERVAL_MS = 5000;

// Wie bei den ESP-Boards: eigenes Status-Topic mit Last-Will (siehe
// ClawMqttConnection::ensureMqttConnected in claw_mqtt_connection.cpp) —
// das Webinterface soll sich genau wie jedes andere Gerät als eigenes Gerät
// mit Online/Offline-Status im Dashboard zeigen, nicht nur Befehle senden.
const STATUS_TOPIC = 'clawmachine/claw_web_interface/status';

const MAX_LOG_ENTRIES_PER_DEVICE = 30;

interface DeviceListEntry {
  name: string;
  isOnline: boolean;
  uptimeMilliseconds: number | null;
  addedAtUnixSeconds: number | null;
}

// Rein kosmetisch, kennt keine konkreten Gerätenamen: "player_input" wird zu
// "Player Input". Kein Gerät wird hier fest verdrahtet — welche es gibt,
// kommt ausschließlich über die Antwort auf DEVICE_LIST_REQUEST_TOPIC rein.
function formatDeviceLabel(name: string): string {
  return name
    .split(/[_-]+/)
    .filter(Boolean)
    .map(part => part.charAt(0).toUpperCase() + part.slice(1))
    .join(' ');
}

@Injectable({ providedIn: 'root' })
export class MqttService implements OnDestroy {
  private client: MqttClient | null = null;

  readonly connectionStatus$ = new BehaviorSubject<ConnectionStatus>('disconnected');

  // Startet leer — welche Geräte es gibt, erfährt das Webinterface nicht durch
  // Raten, sondern ausschließlich über die Antwort auf DEVICE_LIST_REQUEST_TOPIC.
  readonly devices$ = new BehaviorSubject<DeviceState[]>([]);

  // Ein Nachrichten-Log pro Gerät, keyed nach Device-ID — läuft dynamisch für
  // jedes Gerät mit, das der Server gerade kennt (siehe devices$), statt fest
  // verdrahteter Logs für einzelne Gerätenamen.
  readonly deviceLogs$ = new BehaviorSubject<Record<string, MessageLog[]>>({});

  private deviceListRefreshSubscription: Subscription | null = null;

  constructor(private ngZone: NgZone) {}

  connect(brokerUrl: string): void {
    if (this.client) {
      this.client.end(true);
    }

    this.ngZone.run(() => this.connectionStatus$.next('connecting'));

    this.client = mqtt.connect(brokerUrl, {
      clientId: `clawmachine_dashboard_${Math.random().toString(16).substring(2, 8)}`,
      username: environment.mqttUsername,
      password: environment.mqttPassword,
      reconnectPeriod: 3000,
      // Last Will — bricht die Browser-Tab die Verbindung ab (Reload, Tab zu),
      // markiert der Broker uns automatisch als offline. Gleiches Prinzip wie
      // die ESP-Boards in ClawMqttConnection::ensureMqttConnected().
      will: { topic: STATUS_TOPIC, payload: 'offline', qos: 1, retain: true },
    });

    this.client.on('connect', () => {
      this.ngZone.run(() => this.connectionStatus$.next('connected'));
      this.client!.publish(STATUS_TOPIC, 'online', { retain: true });
      // Ein einziges Wildcard-Subscribe für den gesamten Namensraum statt
      // einzelner Subscribes pro bekanntem Gerätenamen — welche Geräte
      // tatsächlich existieren, wird rein aus den eingehenden Topics und der
      // Antwort auf DEVICE_LIST_REQUEST_TOPIC abgeleitet, nicht vorab fest verdrahtet.
      this.client!.subscribe(`${CLAWMACHINE_TOPIC_PREFIX}#`);

      this.requestDeviceList();
      this.deviceListRefreshSubscription?.unsubscribe();
      this.deviceListRefreshSubscription = interval(DEVICE_LIST_REFRESH_INTERVAL_MS).subscribe(
        () => this.requestDeviceList(),
      );
    });

    this.client.on('message', (topic: string, payload: Buffer) => {
      this.ngZone.run(() => this.handleMessage(topic, payload.toString()));
    });

    this.client.on('error', () => {
      this.ngZone.run(() => this.connectionStatus$.next('error'));
    });

    this.client.on('close', () => {
      this.ngZone.run(() => this.connectionStatus$.next('disconnected'));
    });

    this.client.on('reconnect', () => {
      this.ngZone.run(() => this.connectionStatus$.next('connecting'));
    });
  }

  disconnect(): void {
    // Bei sauberem Trennen explizit offline melden statt nur aufs LWT zu
    // warten (das greift erst, wenn der Broker den Verbindungsabbruch
    // erkennt — bei einem bewussten "Trennen"-Klick soll das sofort sichtbar sein).
    this.client?.publish(STATUS_TOPIC, 'offline', { retain: true });
    this.client?.end();
    this.client = null;

    this.deviceListRefreshSubscription?.unsubscribe();
    this.deviceListRefreshSubscription = null;
  }

  // Anfrage an den Server — die Antwort kommt asynchron über DEVICE_LIST_TOPIC
  // (siehe handleMessage) rein, nicht als Rückgabewert dieser Methode.
  requestDeviceList(): void {
    if (!this.client || !this.client.connected) {
      return;
    }
    this.client.publish(DEVICE_LIST_REQUEST_TOPIC, '');
  }

  publishCommand(command: string): void {
    if (!this.client || !this.client.connected) {
      return;
    }
    this.client.publish(CONTROL_TOPIC, command);
  }

  private handleMessage(topic: string, payload: string): void {
    if (topic === DEVICE_LIST_TOPIC) {
      this.applyDeviceList(payload);
      return;
    }

    if (!topic.startsWith(CLAWMACHINE_TOPIC_PREFIX)) {
      return;
    }
    const [deviceName, ...subTopicParts] = topic.slice(CLAWMACHINE_TOPIC_PREFIX.length).split('/');
    const subTopic = subTopicParts.join('/');
    if (!deviceName || !subTopic) {
      return;
    }

    if (subTopic === 'status') {
      this.updateDevice(deviceName, { isOnline: payload === 'online', lastSeen: new Date() });
      return;
    }
    if (subTopic === 'metadata/uptime') {
      const ms = parseInt(payload, 10);
      if (!isNaN(ms)) {
        this.updateDevice(deviceName, { uptimeMs: ms, lastSeen: new Date() });
      }
      return;
    }

    // Alles andere (Befehle, Player-Input, Internal-Nachrichten, …) landet im
    // Aktivitäts-Log des jeweiligen Geräts — dynamisch für jeden Gerätenamen,
    // den der Server gerade kennt (siehe appendDeviceLog).
    this.appendDeviceLog(deviceName, topic, payload);
  }

  // Antwort auf eine requestDeviceList()-Anfrage — ersetzt die Geräteliste
  // durch den server-seitigen Stand (einzige Quelle der Wahrheit dafür,
  // WELCHE Geräte es gibt). lastSeen bleibt dabei erhalten, falls schon aus
  // einem Live-Update (Status/Uptime, siehe updateDevice) bekannt, damit ein
  // periodischer Refresh nicht auf den Registrierungszeitpunkt zurückspringt.
  private applyDeviceList(payload: string): void {
    let entries: DeviceListEntry[];
    try {
      entries = JSON.parse(payload);
    } catch {
      return;
    }

    const current = this.devices$.value;
    const next = entries.map((entry): DeviceState => {
      const existing = current.find(d => d.id === entry.name);
      return {
        id: entry.name,
        label: formatDeviceLabel(entry.name),
        isOnline: entry.isOnline,
        uptimeMs: entry.uptimeMilliseconds,
        lastSeen:
          existing?.lastSeen ??
          (entry.addedAtUnixSeconds ? new Date(entry.addedAtUnixSeconds * 1000) : null),
      };
    });
    this.devices$.next(next);

    // Logs verwaister Geräte (nicht mehr in der aktuellen Liste) aufräumen,
    // damit deviceLogs$ nicht unbegrenzt wächst.
    const knownIds = new Set(next.map(d => d.id));
    const currentLogs = this.deviceLogs$.value;
    const prunedLogs = Object.fromEntries(
      Object.entries(currentLogs).filter(([id]) => knownIds.has(id)),
    );
    if (Object.keys(prunedLogs).length !== Object.keys(currentLogs).length) {
      this.deviceLogs$.next(prunedLogs);
    }
  }

  // Aktualisiert nur bereits bekannte Geräte (Live-Update zwischen zwei
  // Geräteliste-Anfragen). Taucht hier ein noch unbekannter Gerätename auf,
  // wird er NICHT einfach lokal erfunden — welche Geräte es gibt, bestimmt
  // ausschließlich der Server (siehe applyDeviceList); der nächste periodische
  // requestDeviceList() holt ihn dann korrekt nach.
  private updateDevice(id: string, patch: Partial<DeviceState>): void {
    const current = this.devices$.value;
    const idx = current.findIndex(d => d.id === id);
    if (idx < 0) {
      return;
    }

    const updated = [...current];
    updated[idx] = { ...updated[idx], ...patch };
    this.devices$.next(updated);
  }

  // Nur für Geräte, die der Server bereits kennt (siehe devices$) — sonst
  // würde jede Nachricht auf clawmachine/# (auch von unbekannten/fremden
  // Topics) unbegrenzt eigene Log-Buckets anlegen.
  private appendDeviceLog(deviceName: string, topic: string, payload: string): void {
    const isKnownDevice = this.devices$.value.some(d => d.id === deviceName);
    if (!isKnownDevice) {
      return;
    }

    const current = this.deviceLogs$.value;
    const existingLog = current[deviceName] ?? [];
    const entry: MessageLog = { topic, payload, timestamp: new Date() };
    this.deviceLogs$.next({
      ...current,
      [deviceName]: [entry, ...existingLog].slice(0, MAX_LOG_ENTRIES_PER_DEVICE),
    });
  }

  ngOnDestroy(): void {
    this.client?.end();
    this.deviceListRefreshSubscription?.unsubscribe();
  }
}
