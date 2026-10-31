export interface Reading {
  id: number;
  ts: number;
  soil: number;
  soil_raw: number;
  temp: number;
  hum: number;
  tank_ok: boolean;
  pump: boolean;
  mode: "auto" | "manual";
  reason: string;
  sensor_ok: boolean;
}

export interface Thresholds {
  on: number;
  off: number;
  max_run_s: number;
  cooldown_s: number;
}

export interface Latest {
  reading: Reading | null;
  online: boolean;
  last_seen: number | null;
  server_time: number;
  thresholds: Thresholds;
}

export interface Alert {
  code: string;
  severity: "warning" | "serious" | "critical";
  message: string;
}

export interface DeviceEvent {
  id: number;
  ts: number;
  type: string;
  message: string;
  payload: { reason?: string };
}
