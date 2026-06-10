import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription, interval } from 'rxjs';
import { MqttService, ConnectionStatus, DeviceState, MessageLog } from './services/mqtt.service';
import { DeviceCardComponent } from './components/device-card/device-card.component';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, FormsModule, DeviceCardComponent],
  templateUrl: './app.component.html',
  styleUrl: './app.component.css',
})
export class AppComponent implements OnInit, OnDestroy {
  brokerUrl = 'ws://localhost:9001';
  connectionStatus: ConnectionStatus = 'disconnected';
  devices: DeviceState[] = [];
  commandLog: MessageLog[] = [];
  inputLog: MessageLog[] = [];

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

  timeLabel(d: Date): string {
    return d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }
}
