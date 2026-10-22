import { Component, EventEmitter, Input, Output } from '@angular/core';
import { CommonModule } from '@angular/common';
import { InputSourcesState } from '../../services/mqtt.service';

export interface InputSourceToggleEvent {
  name: string;
  isAllowed: boolean;
}

interface InputSourceRow {
  name: string;
  label: string;
  isAllowed: boolean;
}

// Rein kosmetisch, kein hartes Verdrahten welche Quellen existieren — kommt
// ausschließlich aus dem sources-Input (server-seitiger Stand).
const SOURCE_LABELS: Record<string, string> = {
  panel: 'Bedienpanel',
  joycon: 'Joy-Con',
};

function formatSourceLabel(name: string): string {
  return SOURCE_LABELS[name] ?? name;
}

@Component({
  selector: 'app-input-source-toggle',
  standalone: true,
  imports: [CommonModule],
  templateUrl: './input-source-toggle.component.html',
  styleUrl: './input-source-toggle.component.css',
})
export class InputSourceToggleComponent {
  @Input({ required: true }) sources!: InputSourcesState;
  @Input() disabled = false;
  @Output() toggle = new EventEmitter<InputSourceToggleEvent>();

  get rows(): InputSourceRow[] {
    return Object.entries(this.sources).map(([name, isAllowed]) => ({
      name,
      label: formatSourceLabel(name),
      isAllowed,
    }));
  }

  onToggle(name: string, currentlyAllowed: boolean): void {
    this.toggle.emit({ name, isAllowed: !currentlyAllowed });
  }
}
