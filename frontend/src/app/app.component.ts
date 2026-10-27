import { Component, OnInit, OnDestroy } from '@angular/core';
import { CommonModule } from '@angular/common';
import { FormsModule } from '@angular/forms';
import { Subscription, interval } from 'rxjs';
import {
  MqttService,
  ConnectionStatus,
  DeviceState,
  MessageLog,
  EndstopState,
  InputSourcesState,
} from './services/mqtt.service';
import { DeviceCardComponent } from './components/device-card/device-card.component';
import { EndstopIconComponent } from './components/endstop-icon/endstop-icon.component';
import {
  InputSourceToggleComponent,
  InputSourceToggleEvent,
} from './components/input-source-toggle/input-source-toggle.component';
import { environment } from '../environments/environment';

@Component({
  selector: 'app-root',
  standalone: true,
  imports: [CommonModule, FormsModule, DeviceCardComponent, EndstopIconComponent, InputSourceToggleComponent],
  templateUrl: './app.component.html',
  styleUrl: './app.component.css',
})
export class AppComponent implements OnInit, OnDestroy {
  brokerUrl = `ws://${window.location.hostname}:${environment.mqttWebsocketPort}`;
  connectionStatus: ConnectionStatus = 'disconnected';
  devices: DeviceState[] = [];
  deviceLogs: Record<string, MessageLog[]> = {};
  endstopState: EndstopState = { x1: false, x2: false, y1: false, y2: false, z1: false };
  inputSources: InputSourcesState = {};

  // Standard-Geschwindigkeiten (Prozent der max. Drehzahl) bzw. Klaue (Grad pro
  // Sekunde). Gelten am Server (siehe [motor_speed] in config.ini) und werden von
  // dort über speedSettings$ in die Felder übernommen; die Zahlen hier sind nur
  // Platzhalter, bis die erste Antwort da ist.
  xySpeedPercent = 100;
  zSpeedPercent = 100;

  // Beschleunigung in Prozentpunkten Geschwindigkeit pro Sekunde, getrennt für X/Y
  // und Z (xy_accel/z_accel in config.ini). Werte kommen vom Server über speedSettings$.
  xyAccelerationPercentPerSecond = 50;
  zAccelerationPercentPerSecond = 50;

  // Gleicher Default wie DEFAULT_SPEED_DEGREES_PER_SECOND in claw_servo_motor.h —
  // Fahrgeschwindigkeit der Klaue in Grad pro Sekunde.
  clawSpeedDegreesPerSecond = 35;

  // Fahrbereich der Klaue in Grad, entspricht MIN_/MAX_ALLOWED_ANGLE_DEGREES in
  // lib/claw_motor/claw_servo_motor.h (kleiner = offen, groesser = zu). Der Servo begrenzt
  // selbst; hier nur, damit der Regler denselben Bereich zeigt.
  readonly clawMinAngleDegrees = 90;
  readonly clawMaxAngleDegrees = 160;
  clawAngleDegrees = 90;
  // Waehrend des Ziehens hoechstens alle 100 ms senden, nicht bei jedem Pixel.
  private lastClawAngleSentAtMilliseconds = 0;
  private static readonly CLAW_ANGLE_SEND_INTERVAL_MILLISECONDS = 100;

  private subs: Subscription[] = [];

  constructor(readonly mqttService: MqttService) {}

  ngOnInit(): void {
    this.subs.push(
      this.mqttService.connectionStatus$.subscribe(s => (this.connectionStatus = s)),
      this.mqttService.devices$.subscribe(d => (this.devices = d)),
      this.mqttService.deviceLogs$.subscribe(l => (this.deviceLogs = l)),
      this.mqttService.endstopState$.subscribe(e => (this.endstopState = e)),
      this.mqttService.inputSources$.subscribe(s => (this.inputSources = s)),
      this.mqttService.speedSettings$.subscribe(settings => {
        if (settings) {
          this.xySpeedPercent = settings.xyPercent;
          this.zSpeedPercent = settings.zPercent;
          this.clawSpeedDegreesPerSecond = settings.clawDegreesPerSecond;
          this.xyAccelerationPercentPerSecond = settings.xyAccelerationPercentPerSecond;
          this.zAccelerationPercentPerSecond = settings.zAccelerationPercentPerSecond;
        }
      }),
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
    // 100 % bzw. -100 % — der Server skaliert auf die eingestellte XY-Geschwindigkeit.
    const speed = direction === 'left' ? 100 : -100;
    this.mqttService.publishCommand(`${direction}:${speed}`);
  }

  startMoveY(direction: 'up' | 'down'): void {
    const name = direction === 'up' ? 'back' : 'front';
    const speed = direction === 'up' ? 100 : -100;
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

  onClawAngleInput(): void {
    const now = Date.now();
    if (now - this.lastClawAngleSentAtMilliseconds < AppComponent.CLAW_ANGLE_SEND_INTERVAL_MILLISECONDS) {
      return;
    }
    this.sendClawAngle();
  }

  // Beim Loslassen immer senden, damit der Endwert sicher ankommt.
  sendClawAngle(): void {
    this.lastClawAngleSentAtMilliseconds = Date.now();
    this.mqttService.publishCommand(`claw:${this.clawAngleDegrees}`);
  }

  sendXyAcceleration(): void {
    this.mqttService.publishCommand(`xy_accel:${this.xyAccelerationPercentPerSecond}`);
  }

  sendZAcceleration(): void {
    this.mqttService.publishCommand(`z_accel:${this.zAccelerationPercentPerSecond}`);
  }

  sendXySpeed(): void {
    this.mqttService.publishCommand(`xy_speed:${this.xySpeedPercent}`);
  }

  sendZSpeed(): void {
    this.mqttService.publishCommand(`z_speed:${this.zSpeedPercent}`);
  }

  sendClawSpeed(): void {
    this.mqttService.publishCommand(`claw_speed:${this.clawSpeedDegreesPerSecond}`);
  }

  onToggleInputSource(event: InputSourceToggleEvent): void {
    this.mqttService.setInputSourceAllowed(event.name, event.isAllowed);
  }

  timeLabel(d: Date): string {
    return d.toLocaleTimeString('de-DE', { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  }
}
