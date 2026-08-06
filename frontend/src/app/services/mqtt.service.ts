import { Injectable, NgZone, OnDestroy } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
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

const KNOWN_DEVICES: Record<string, string> = {
  motor_controller: 'Motor Controller',
  control_panel:    'Control Panel',
  web_interface:    'Web Interface',
  player_input:     'Player Input',
};

// Wie bei den ESP-Boards: eigenes Status-Topic mit Last-Will (siehe
// ClawMqttConnection::ensureMqttConnected in claw_mqtt_connection.cpp) —
// das Webinterface soll sich genau wie player_input als eigenes Gerät
// mit Online/Offline-Status im Dashboard zeigen, nicht nur Befehle senden.
const STATUS_TOPIC = 'clawmachine/web_interface/status';

@Injectable({ providedIn: 'root' })
export class MqttService implements OnDestroy {
  private client: MqttClient | null = null;

  readonly connectionStatus$ = new BehaviorSubject<ConnectionStatus>('disconnected');

  readonly devices$ = new BehaviorSubject<DeviceState[]>(
    Object.entries(KNOWN_DEVICES).map(([id, label]) => ({
      id, label, isOnline: false, uptimeMs: null, lastSeen: null,
    }))
  );

  readonly commandLog$ = new BehaviorSubject<MessageLog[]>([]);
  readonly inputLog$   = new BehaviorSubject<MessageLog[]>([]);

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
      this.client!.subscribe('clawmachine/motor_controller/command');
      this.client!.subscribe('clawmachine/web_interface/command');
      this.client!.subscribe('clawmachine/player_input/joycon');
      this.client!.subscribe('clawmachine/player_input/panel');
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
  }

  publishCommand(command: string): void {
    if (!this.client || !this.client.connected) {
      return;
    }
    this.client.publish(CONTROL_TOPIC, command);
  }

  private handleMessage(topic: string, payload: string): void {
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
    } else if (topic === 'clawmachine/motor_controller/command') {
      this.appendLog(this.commandLog$, topic, payload);
    } else if (topic === 'clawmachine/web_interface/command') {
      this.appendLog(this.commandLog$, topic, payload);
    } else if (
      topic === 'clawmachine/player_input/joycon' ||
      topic === 'clawmachine/player_input/panel'
    ) {
      this.appendLog(this.inputLog$, topic, payload);
    }
  }

  private updateDevice(id: string, patch: Partial<DeviceState>): void {
    const current = this.devices$.value;
    const idx = current.findIndex(d => d.id === id);

    if (idx >= 0) {
      const updated = [...current];
      updated[idx] = { ...updated[idx], ...patch };
      this.devices$.next(updated);
    } else {
      // Unknown device discovered via MQTT
      this.devices$.next([
        ...current,
        { id, label: id, isOnline: false, uptimeMs: null, lastSeen: null, ...patch },
      ]);
    }
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
  }
}
