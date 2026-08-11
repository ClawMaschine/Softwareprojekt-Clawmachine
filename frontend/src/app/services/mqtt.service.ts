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

// Haupt-Steuertopic des Servers — Befehle mit X:/Y:/Z:/claw:-Präfix werden
// dort unverändert an den Motor-Controller weitergeleitet (siehe
// MOTOR_COMMAND_PREFIXES in python_server/clawmachine/claw_machine.py).
const CONTROL_TOPIC = 'clawmachine/web_interface/command';

// Nur für die Anzeige — welche Geräte tatsächlich existieren, weiß allein
// der Server (DeviceRegistry). Fehlt ein Name hier, wird einfach der rohe
// Gerätename als Label angezeigt.
const DEVICE_LABELS: Record<string, string> = {
  motor_controller: 'Motor Controller',
  web_interface:    'Web Interface',
  player_input:     'Player Input',
};

// Geräteliste: das Webinterface fragt aktiv beim Server nach (statt der
// Server sie ungefragt zu pushen) und bekommt den aktuellen Stand der
// server-seitigen DeviceRegistry als JSON-Array zurück — siehe
// DEVICE_LIST_REQUEST_TOPIC/DEVICE_LIST_TOPIC in claw_machine.py.
const DEVICE_LIST_REQUEST_TOPIC = 'clawmachine/web_interface/devices/request';
const DEVICE_LIST_TOPIC = 'clawmachine/web_interface/devices';
const DEVICE_LIST_REFRESH_INTERVAL_MS = 5000;

// Wie bei den ESP-Boards: eigenes Status-Topic mit Last-Will (siehe
// ClawMqttConnection::ensureMqttConnected in claw_mqtt_connection.cpp) —
// das Webinterface soll sich genau wie player_input als eigenes Gerät
// mit Online/Offline-Status im Dashboard zeigen, nicht nur Befehle senden.
const STATUS_TOPIC = 'clawmachine/claw_web_interface/status';

interface DeviceListEntry {
  name: string;
  isOnline: boolean;
  uptimeMilliseconds: number | null;
  addedAtUnixSeconds: number | null;
}

@Injectable({ providedIn: 'root' })
export class MqttService implements OnDestroy {
  private client: MqttClient | null = null;

  readonly connectionStatus$ = new BehaviorSubject<ConnectionStatus>('disconnected');

  // Startet leer — welche Geräte es gibt, erfährt das Webinterface nicht durch
  // Raten, sondern ausschließlich über die Antwort auf DEVICE_LIST_REQUEST_TOPIC.
  readonly devices$ = new BehaviorSubject<DeviceState[]>([]);

  readonly commandLog$ = new BehaviorSubject<MessageLog[]>([]);
  readonly inputLog$   = new BehaviorSubject<MessageLog[]>([]);

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
      this.client!.subscribe('clawmachine/+/status');
      this.client!.subscribe('clawmachine/+/metadata/uptime');
      this.client!.subscribe('clawmachine/claw_motor_controller/command');
      this.client!.subscribe('clawmachine/claw_web_interface/command');
      this.client!.subscribe('clawmachine/claw_player_input/+');
      this.client!.subscribe(DEVICE_LIST_TOPIC);

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

    const parts = topic.split('/');

    if (parts.length === 3 && parts[2] === 'status') {
      this.updateDevice(parts[1], {
        isOnline: payload === 'online',
        lastSeen: new Date(),
      });
    } else if (parts.length === 4 && parts[2] === 'metadata' && parts[3] === 'uptime') {
      const ms = parseInt(payload, 10);
      if (!isNaN(ms)) {
        this.updateDevice(parts[1], { uptimeMs: ms, lastSeen: new Date() });
      }
    } else if (topic === 'clawmachine/claw_motor_controller/command') {
      this.appendLog(this.commandLog$, topic, payload);
    } else if (topic === 'clawmachine/claw_web_interface/command') {
      this.appendLog(this.commandLog$, topic, payload);
    } else if (
      topic === 'clawmachine/claw_player_input/joycon' ||
      topic === 'clawmachine/claw_player_input/panel'
    ) {
      this.appendLog(this.inputLog$, topic, payload);
    }
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
        label: DEVICE_LABELS[entry.name] ?? entry.name,
        isOnline: entry.isOnline,
        uptimeMs: entry.uptimeMilliseconds,
        lastSeen:
          existing?.lastSeen ??
          (entry.addedAtUnixSeconds ? new Date(entry.addedAtUnixSeconds * 1000) : null),
      };
    });
    this.devices$.next(next);
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

  private appendLog(
    log$: BehaviorSubject<MessageLog[]>,
    topic: string,
    payload: string,
  ): void {
    const entry: MessageLog = { topic, payload, timestamp: new Date() };
    log$.next([entry, ...log$.value].slice(0, 30));
  }

  ngOnDestroy(): void {
    this.client?.end();
    this.deviceListRefreshSubscription?.unsubscribe();
  }
}
