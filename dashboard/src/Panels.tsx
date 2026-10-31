import type { Alert, DeviceEvent } from "./types";
import { reasonText } from "./reasons";

const ICON = { critical: "▲", serious: "◆", warning: "●" } as const;

export function AlertsPanel({ alerts }: { alerts: Alert[] }) {
  return (
    <section className="panel" aria-label="Alerts">
      <h2>Alerts</h2>
      {alerts.length === 0 ? (
        <div className="ok">✓ No active alerts</div>
      ) : (
        <ul className="alerts">
          {alerts.map((a) => (
            <li key={a.code} className={a.severity}>
              <strong>
                {ICON[a.severity]} {a.severity[0].toUpperCase() + a.severity.slice(1)}
              </strong>
              {a.message}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}

export function EventLog({ events }: { events: DeviceEvent[] }) {
  return (
    <section className="panel" aria-label="Event log">
      <h2>Event log</h2>
      {events.length === 0 ? (
        <div className="hint">No events yet.</div>
      ) : (
        <ul className="events">
          {events.map((e) => (
            <li key={e.id}>
              <time>{new Date(e.ts * 1000).toLocaleString([], { hour: "2-digit", minute: "2-digit", second: "2-digit", month: "short", day: "numeric" })}</time>
              {e.payload.reason ? reasonText(e.payload.reason) : e.message}
            </li>
          ))}
        </ul>
      )}
    </section>
  );
}
