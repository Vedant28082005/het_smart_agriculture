import { useCallback, useEffect, useState } from "react";
import { api } from "./api";
import Cards from "./Cards";
import Charts from "./Charts";
import Controls from "./Controls";
import { AlertsPanel, EventLog } from "./Panels";
import type { Alert, DeviceEvent, Latest, Reading } from "./types";

const RANGES = [
  { label: "1h", hours: 1 },
  { label: "6h", hours: 6 },
  { label: "24h", hours: 24 },
  { label: "7d", hours: 168 },
];
const FALLBACK = { on: 30, off: 60, max_run_s: 60, cooldown_s: 300 };

export default function App() {
  const [latest, setLatest] = useState<Latest | null>(null);
  const [alerts, setAlerts] = useState<Alert[]>([]);
  const [events, setEvents] = useState<DeviceEvent[]>([]);
  const [history, setHistory] = useState<Reading[]>([]);
  const [hours, setHours] = useState(1);
  const [backendDown, setBackendDown] = useState(false);
  const [loaded, setLoaded] = useState(false);

  const refreshLive = useCallback(async () => {
    try {
      const [l, a, e] = await Promise.all([api.latest(), api.alerts(), api.events()]);
      setLatest(l);
      setAlerts(a);
      setEvents(e);
      setBackendDown(false);
    } catch {
      setBackendDown(true);
    } finally {
      setLoaded(true);
    }
  }, []);

  useEffect(() => {
    refreshLive();
    const id = setInterval(refreshLive, 4000);
    return () => clearInterval(id);
  }, [refreshLive]);

  useEffect(() => {
    const load = () => api.history(hours).then(setHistory).catch(() => {});
    load();
    const id = setInterval(load, 15000);
    return () => clearInterval(id);
  }, [hours]);

  const online = !!latest?.online && !backendDown;
  const reading = latest?.reading ?? null;
  const thresholds = latest?.thresholds ?? FALLBACK;
  const updated = latest?.last_seen ? new Date(latest.last_seen * 1000).toLocaleTimeString() : "never";

  return (
    <main className="page">
      <header>
        <div>
          <h1>Smart Irrigation</h1>
          <div className="sub">Last updated: {updated}</div>
        </div>
        <span className={`badge ${online ? "on" : "off"}`} role="status">
          <span className="dot" /> {!loaded ? "Connecting…" : online ? "Device online" : "Device offline"}
        </span>
      </header>

      {backendDown && loaded && (
        <div className="banner" role="alert">Cannot reach the backend. Showing last known values.</div>
      )}

      <Cards reading={reading} live={online} />

      <div className="grid2">
        <Controls reading={reading} online={online} thresholds={thresholds} onChanged={refreshLive} />
        <div style={{ display: "grid", gap: 16, alignContent: "start" }}>
          <AlertsPanel alerts={alerts} />
          <EventLog events={events} />
        </div>
      </div>

      <div className="row" role="group" aria-label="History range">
        <div className="seg">
          {RANGES.map((r) => (
            <button key={r.hours} aria-pressed={hours === r.hours} onClick={() => setHours(r.hours)}>{r.label}</button>
          ))}
        </div>
      </div>
      <Charts data={history} hours={hours} thresholds={thresholds} />
    </main>
  );
}
