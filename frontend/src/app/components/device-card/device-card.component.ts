import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';
import { DeviceState } from '../../services/mqtt.service';

@Component({
  selector: 'app-device-card',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './device-card.component.html',
  styleUrl: './device-card.component.css',
})
export class DeviceCardComponent {
  @Input({ required: true }) device!: DeviceState;

  get uptimeFormatted(): string {
    const ms = this.device.uptimeMs;
    if (ms === null) return '—';
    const totalSeconds = Math.floor(ms / 1000);
    const days    = Math.floor(totalSeconds / 86400);
    const hours   = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;

    if (days > 0)  return `${days}d ${hours}h ${minutes}m`;
    if (hours > 0) return `${hours}h ${minutes}m ${seconds}s`;
    if (minutes > 0) return `${minutes}m ${seconds}s`;
    return `${seconds}s`;
  }

  get deviceIcon(): string {
    switch (this.device.id) {
      case 'motor_controller': return '⚙';
      case 'control_panel':    return '🖥';
      case 'player_input':     return '🎮';
      default:                 return '📡';
    }
  }

  get lastSeenFormatted(): string {
    const d = this.device.lastSeen;
    if (!d) return '—';
    return d.toLocaleTimeString('de-DE');
  }
}
