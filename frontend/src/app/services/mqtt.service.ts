import { Injectable, NgZone, OnDestroy } from '@angular/core';
import { BehaviorSubject } from 'rxjs';
import mqtt, { MqttClient } from 'mqtt';

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

const KNOWN_DEVICES: Record<string, string> = {
  motor_controller: 'Motor Controller',
  control_panel:    'Control Panel',
  player_input:     'Player Input',
};

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
      username: 'clawmachine',
      password: 'claw_secret',
      reconnectPeriod: 3000,
    });

    this.client.on('connect', () => {
      this.ngZone.run(() => this.connectionStatus$.next('connected'));
      this.client!.subscribe('clawmachine/+/status');
      this.client!.subscribe('clawmachine/+/metadata/uptime');
      this.client!.subscribe('clawmachine/motor_controller/command');
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
    this.client?.end();
    this.client = null;
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
