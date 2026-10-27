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

// Rohstatus vom Endstop-Board, vom Server unverändert durchgereicht (siehe
// WEB_INTERFACE_ENDSTOP_TOPIC in claw_machine.py). *2-Endstops (x2/y2) sind
// Teil des Payloads, werden aber aktuell nirgends in der UI ausgewertet.
export interface EndstopState {
  x1: boolean;
  x2: boolean;
  y1: boolean;
  y2: boolean;
  z1: boolean;
}

const EMPTY_ENDSTOP_STATE: EndstopState = { x1: false, x2: false, y1: false, y2: false, z1: false };

// Name -> "darf diese Eingabequelle aktuell Befehle durchreichen?" (siehe
// InputSourceControl in claw_machine.py). Aktuell "panel" und "joycon".
export type InputSourcesState = Record<string, boolean>;

// Antwort des Servers auf SPEED_SETTINGS_REQUEST_TOPIC (siehe publish_speed_settings()
// in claw_machine.py).
export interface SpeedSettings {
  xyPercent: number;
  zPercent: number;
  clawDegreesPerSecond: number;
  xyAccelerationPercentPerSecond: number;
  zAccelerationPercentPerSecond: number;
}

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

// Endstop-Rohstatus, vom Server bei jeder Änderung ungefragt gepusht (kein
// Request/Response wie bei der Geräteliste nötig).
const ENDSTOP_TOPIC = 'clawmachine/web_interface/endstop';

// Welche Eingabequellen (Panel/Joy-Con) aktuell durchgereicht werden dürfen —
// gleiches Anfrage/Antwort-Prinzip wie die Geräteliste, plus ein eigenes
// Set-Topic zum Sperren/Entsperren einer Quelle.
const INPUT_SOURCES_REQUEST_TOPIC = 'clawmachine/web_interface/input_sources/request';
const INPUT_SOURCES_TOPIC = 'clawmachine/web_interface/input_sources';
const INPUT_SOURCES_SET_TOPIC = 'clawmachine/web_interface/input_sources/set';
const SPEED_SETTINGS_REQUEST_TOPIC = 'clawmachine/web_interface/speed_settings/request';
const SPEED_SETTINGS_TOPIC = 'clawmachine/web_interface/speed_settings';

// Wie bei den ESP-Boards: eigenes Status-Topic mit Last-Will (siehe
// ClawMqttConnection::ensureMqttConnected in claw_mqtt_connection.cpp).
// Der Name bekommt pro Tab eine zufällige vierstellige ID angehängt (siehe
// webInterfaceId weiter unten), da sich sonst mehrere gleichzeitig offene
// Tabs denselben Status-Topic teilen und sich gegenseitig überschreiben
// würden. Der Server erkennt das Präfix und registriert es bewusst NICHT
// als Gerät (siehe WEB_INTERFACE_DEVICE_PREFIX/WebInterfacePresence in
// claw_machine.py) — taucht also nicht in der Geräteliste auf.
const WEB_INTERFACE_STATUS_TOPIC_PREFIX = 'clawmachine/claw_web_interface_';
const WEB_INTERFACE_STATUS_TOPIC_SUFFIX = '/status';

function generateFourDigitId(): string {
  return Math.floor(1000 + Math.random() * 9000).toString();
}

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

  // Letzter bekannter Endstop-Rohstatus — startet "alles nicht ausgelöst",
  // bis die erste echte Nachricht vom Server ankommt.
  readonly endstopState$ = new BehaviorSubject<EndstopState>(EMPTY_ENDSTOP_STATE);

  // Startet leer wie devices$ — welche Quellen es gibt und ob sie erlaubt
  // sind, kommt ausschließlich über die Antwort auf INPUT_SOURCES_REQUEST_TOPIC.
  readonly inputSources$ = new BehaviorSubject<InputSourcesState>({});

  // Aktuell am Server eingestellte Geschwindigkeiten; null, solange noch keine Antwort da ist.
  readonly speedSettings$ = new BehaviorSubject<SpeedSettings | null>(null);

  private deviceListRefreshSubscription: Subscription | null = null;

  // Einmal pro Tab/Instanz erzeugt (nicht pro connect()-Aufruf), damit die ID
  // über Reconnects hinweg stabil bleibt und sich im Broker nachvollziehen lässt.
  private readonly webInterfaceId = generateFourDigitId();

  private get statusTopic(): string {
    return `${WEB_INTERFACE_STATUS_TOPIC_PREFIX}${this.webInterfaceId}${WEB_INTERFACE_STATUS_TOPIC_SUFFIX}`;
  }

  constructor(private ngZone: NgZone) {}

  connect(brokerUrl: string): void {
    if (this.client) {
      this.client.end(true);
    }

    this.ngZone.run(() => this.connectionStatus$.next('connecting'));

    this.client = mqtt.connect(brokerUrl, {
      clientId: `clawmachine_dashboard_${this.webInterfaceId}`,
      username: environment.mqttUsername,
      password: environment.mqttPassword,
      reconnectPeriod: 3000,
      // Last Will — bricht die Browser-Tab die Verbindung ab (Reload, Tab zu),
      // markiert der Broker uns automatisch als offline. Gleiches Prinzip wie
      // die ESP-Boards in ClawMqttConnection::ensureMqttConnected().
      will: { topic: this.statusTopic, payload: 'offline', qos: 1, retain: true },
    });

    this.client.on('connect', () => {
      this.ngZone.run(() => this.connectionStatus$.next('connected'));
      this.client!.publish(this.statusTopic, 'online', { retain: true });
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

      // Einmalig anfragen reicht — Änderungen werden danach vom Server
      // ungefragt gepusht (siehe publish_input_sources() in claw_machine.py).
      this.requestInputSources();
      this.requestSpeedSettings();
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
    this.client?.publish(this.statusTopic, 'offline', { retain: true });
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

  // Anfrage an den Server — die Antwort kommt asynchron über
  // INPUT_SOURCES_TOPIC (siehe handleMessage) rein.
  requestInputSources(): void {
    if (!this.client || !this.client.connected) {
      return;
    }
    this.client.publish(INPUT_SOURCES_REQUEST_TOPIC, '');
  }

  requestSpeedSettings(): void {
    if (!this.client || !this.client.connected) {
      return;
    }
    this.client.publish(SPEED_SETTINGS_REQUEST_TOPIC, '');
  }

  // Sperrt/entsperrt eine Eingabequelle — der Server bestätigt die Änderung
  // (oder lehnt sie ab, z.B. bei unbekanntem Namen) über einen erneuten Push
  // auf INPUT_SOURCES_TOPIC, inputSources$ wird also nicht optimistisch hier
  // schon aktualisiert.
  setInputSourceAllowed(name: string, isAllowed: boolean): void {
    if (!this.client || !this.client.connected) {
      return;
    }
    this.client.publish(INPUT_SOURCES_SET_TOPIC, `${name}:${isAllowed ? 'on' : 'off'}`);
  }

  private handleMessage(topic: string, payload: string): void {
    if (topic === DEVICE_LIST_TOPIC) {
      this.applyDeviceList(payload);
      return;
    }

    if (topic === ENDSTOP_TOPIC) {
      this.applyEndstopState(payload);
      return;
    }

    if (topic === INPUT_SOURCES_TOPIC) {
      this.applyInputSources(payload);
      return;
    }

    if (topic === SPEED_SETTINGS_TOPIC) {
      this.applySpeedSettings(payload);
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

  private applyEndstopState(payload: string): void {
    let raw: Partial<EndstopState>;
    try {
      raw = JSON.parse(payload);
    } catch {
      return;
    }
    this.endstopState$.next({
      x1: Boolean(raw.x1),
      x2: Boolean(raw.x2),
      y1: Boolean(raw.y1),
      y2: Boolean(raw.y2),
      z1: Boolean(raw.z1),
    });
  }

  private applySpeedSettings(payload: string): void {
    try {
      this.speedSettings$.next(JSON.parse(payload) as SpeedSettings);
    } catch {
      return;
    }
  }

  private applyInputSources(payload: string): void {
    let sources: InputSourcesState;
    try {
      sources = JSON.parse(payload);
    } catch {
      return;
    }
    this.inputSources$.next(sources);
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
