import type { Reading } from "./types";
import { reasonText } from "./reasons";

interface Props {
  reading: Reading | null;
  live: boolean;
}

function Card({ label, value, unit, note, live }: { label: string; value: string; unit?: string; note?: string; live: boolean }) {
  return (
    <div className={`card ${live ? "" : "stale"}`}>
      <div className="label">{label}</div>
      <div className="value">
        {value}
        {unit && value !== "—" && <small>{unit}</small>}
      </div>
      <div className="note">{note}</div>
    </div>
  );
}

export default function Cards({ reading: r, live }: Props) {
  const v = (n: number | undefined, d = 1) => (r && n !== undefined && n !== null ? n.toFixed(d) : "—");
  return (
    <section className="cards" aria-label="Live readings">
      <Card live={live} label="Soil moisture" value={v(r?.soil)} unit="%" note={r ? `raw ${r.soil_raw}` : ""} />
      <Card live={live} label="Temperature" value={v(r?.temp)} unit="°C" />
      <Card live={live} label="Humidity" value={v(r?.hum)} unit="%" />
      <Card live={live} label="Water tank" value={r ? (r.tank_ok ? "OK" : "Empty") : "—"} />
      <Card live={live} label="Pump" value={r ? (r.pump ? "ON" : "OFF") : "—"} note={reasonText(r?.reason)} />
      <Card live={live} label="Mode" value={r ? (r.mode === "auto" ? "Auto" : "Manual") : "—"} />
    </section>
  );
}
