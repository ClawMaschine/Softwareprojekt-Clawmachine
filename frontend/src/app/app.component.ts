import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription, interval } from 'rxjs';
import { MqttService, ConnectionStatus, DeviceState, MessageLog } from './services/mqtt.service';
import { DeviceCardComponent } from './components/device-card/device-card.component';
import { environment } from '../environments/environment';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, FormsModule, DeviceCardComponent],
  templateUrl: './app.component.html',
  styleUrl: './app.component.css',
})
export class AppComponent implements OnInit, OnDestroy {
  brokerUrl = `ws://${window.location.hostname}:${environment.mqttWebsocketPort}`;
  connectionStatus: ConnectionStatus = 'disconnected';
  devices: DeviceState[] = [];
  deviceLogs: Record<string, MessageLog[]> = {};

  // Gleicher Default wie PANEL_MOTOR_SPEED in claw_machine.py. Anders als die
  // Beschleunigung ist das keine Einstellung auf dem ESP, sondern wird bei
  // jedem Bewegungsbefehl direkt mitgeschickt (z.B. "left:80") — ein Eingabe-
  // feld hier reicht deshalb, kein zusätzliches MQTT-Roundtrip/Settings-Topic.
  controlSpeed = 80;

  // Gleicher Default wie CLAW_MOTOR_ACCELERATION_PERCENT_PER_SECOND in
  // firmware_config.h — Prozentpunkte Geschwindigkeit pro Sekunde.
  accelerationPercentPerSecond = 50;

  private subs: Subscription[] = [];

  constructor(readonly mqttService: MqttService) {}

  ngOnInit(): void {
    this.subs.push(
      this.mqttService.connectionStatus$.subscribe(s => (this.connectionStatus = s)),
      this.mqttService.devices$.subscribe(d => (this.devices = d)),
      this.mqttService.deviceLogs$.subscribe(l => (this.deviceLogs = l)),
      // Trigger change detection every second to refresh uptime displays
      interval(1000).subscribe(() => {
        this.devices = [...this.mqttService.devices$.value];
      }),
    );
    this.mqttService.connect(this.brokerUrl);
  }

  ngOnDestroy(): void {
    this.subs.forEach(s => s.unsubscribe());
    this.mqttService.disconnect();
  }

  onConnect(): void {
    this.mqttService.connect(this.brokerUrl);
  }

  onDisconnect(): void {
    this.mqttService.disconnect();
  }

  get statusLabel(): string {
    const map: Record<ConnectionStatus, string> = {
      disconnected: 'Getrennt',
      connecting:   'Verbinde …',
      connected:    'Verbunden',
      error:        'Fehler',
    };
    return map[this.connectionStatus];
  }

  get onlineCount(): number {
    return this.devices.filter(d => d.isOnline).length;
  }

  // Zeigt im Geräte-Log nur den Teil des Topics, der über den Gerätenamen
  // hinausgeht (z.B. "clawmachine/player_input/panel" + "player_input" -> "panel").
  logSubTopic(topic: string, deviceId: string): string {
    return topic.replace(`clawmachine/${deviceId}/`, '');
  }

  // Achsen-Vorzeichen spiegeln die Server-Logik für das physische Panel
  // (siehe claw_machine.py): rechts/unten = negative Geschwindigkeit,
  // links/oben = positive Geschwindigkeit. Der Server mappt "left"/"right"
  // auf X und "front"/"back" auf Y — der tatsächliche Tastenname muss also
  // mitgeschickt werden, nicht nur die Geschwindigkeit.
  startMoveX(direction: 'left' | 'right'): void {
    const speed = direction === 'left' ? this.controlSpeed : -this.controlSpeed;
    this.mqttService.publishCommand(`${direction}:${speed}`);
  }

  startMoveY(direction: 'up' | 'down'): void {
    const name = direction === 'up' ? 'front' : 'back';
    const speed = direction === 'up' ? -this.controlSpeed : this.controlSpeed;
    this.mqttService.publishCommand(`${name}:${speed}`);
  }

  stopMoveX(): void {
    this.mqttService.publishCommand('left:0');
  }

  stopMoveY(): void {
    this.mqttService.publishCommand('front:0');
  }

  sendClaw(action: 'open' | 'close'): void {
    this.mqttService.publishCommand(`claw:${action}`);
  }

  sendAcceleration(): void {
    this.mqttService.publishCommand(`accel:${this.accelerationPercentPerSecond}`);
  }

  timeLabel(d: Date): string {
    return d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }
}
