import { Component, Input } from '@angular/core';
import { CommonModule } from '@angular/common';
import { EndstopState } from '../../services/mqtt.service';

@Component({
  selector: 'app-endstop-icon',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './endstop-icon.component.html',
  styleUrl: './endstop-icon.component.css',
})
export class EndstopIconComponent {
  @Input({ required: true }) state!: EndstopState;
}
