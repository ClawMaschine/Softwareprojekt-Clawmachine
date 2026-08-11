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
  commandLog: MessageLog[] = [];
  inputLog: MessageLog[] = [];

  // Gleiche Geschwindigkeit wie PANEL_MOTOR_SPEED in claw_machine.py, damit
  // sich die Web-Steuerung wie das physische Panel verhält.
  readonly controlSpeed = 80;

  private subs: Subscription[] = [];

  constructor(readonly mqttService: MqttService) {}

  ngOnInit(): void {
    this.subs.push(
      this.mqttService.connectionStatus$.subscribe(s => (this.connectionStatus = s)),
      this.mqttService.devices$.subscribe(d => (this.devices = d)),
      this.mqttService.commandLog$.subscribe(l => (this.commandLog = l)),
      this.mqttService.inputLog$.subscribe(l => (this.inputLog = l)),
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

  shortTopic(topic: string): string {
    return topic.replace('clawmachine/', '');
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

  timeLabel(d: Date): string {
    return d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }
}
