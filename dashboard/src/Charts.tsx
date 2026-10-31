import { CartesianGrid, Line, LineChart, ReferenceArea, ReferenceLine, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
import type { Reading, Thresholds } from "./types";

interface Props {
  data: Reading[];
  hours: number;
  thresholds: Thresholds;
}

/** Consecutive pump-ON samples -> [start, end] time spans for shading. */
function pumpSpans(data: Reading[]): [number, number][] {
  const spans: [number, number][] = [];
  let start: number | null = null;
  data.forEach((r, i) => {
    if (r.pump && start === null) start = r.ts;
    if (!r.pump && start !== null) {
      spans.push([start, r.ts]);
      start = null;
    }
    if (r.pump && i === data.length - 1 && start !== null) spans.push([start, r.ts]);
  });
  return spans;
}

const fmtTick = (hours: number) => (ts: number) => {
  const d = new Date(ts * 1000);
  return hours > 24
    ? d.toLocaleDateString([], { month: "short", day: "numeric" })
    : d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
};

interface MetricProps extends Props {
  title: string;
  unit: string;
  field: "soil" | "temp" | "hum";
  color: string;
  domain?: [number | string, number | string];
  withThresholds?: boolean;
}

function Metric({ title, unit, field, color, data, hours, thresholds, domain, withThresholds }: MetricProps) {
  const spans = withThresholds ? pumpSpans(data) : [];
  return (
    <section className="panel" aria-label={title}>
      <div className="chartHead">
        <h2 style={{ margin: 0 }}>{title}</h2>
        {withThresholds && <span className="key"><i /> Pump ON</span>}
      </div>
      {data.length < 2 ? (
        <div className="empty">Not enough data for this range yet</div>
      ) : (
        <div className="chartBox">
          <ResponsiveContainer>
            <LineChart data={data} margin={{ top: 8, right: 12, bottom: 0, left: -8 }}>
              <CartesianGrid stroke="var(--border)" strokeDasharray="3 3" vertical={false} />
              <XAxis dataKey="ts" type="number" scale="time" domain={["dataMin", "dataMax"]}
                tickFormatter={fmtTick(hours)} tick={{ fill: "var(--text-3)", fontSize: 12 }}
                stroke="var(--border)" minTickGap={40} />
              <YAxis domain={domain ?? ["auto", "auto"]} tick={{ fill: "var(--text-3)", fontSize: 12 }}
                stroke="var(--border)" unit={unit} width={52} />
              {spans.map(([a, b], i) => (
                <ReferenceArea key={i} x1={a} x2={b} fill="var(--band)" stroke="none" ifOverflow="hidden" />
              ))}
              {withThresholds && (
                <>
                  <ReferenceLine y={thresholds.on} stroke="var(--text-3)" strokeDasharray="4 4"
                    label={{ value: `ON < ${thresholds.on}`, position: "insideBottomRight", fill: "var(--text-2)", fontSize: 11 }} />
                  <ReferenceLine y={thresholds.off} stroke="var(--text-3)" strokeDasharray="4 4"
                    label={{ value: `OFF > ${thresholds.off}`, position: "insideTopRight", fill: "var(--text-2)", fontSize: 11 }} />
                </>
              )}
              <Tooltip
                cursor={{ stroke: "var(--text-3)" }}
                content={({ active, payload }) => {
                  if (!active || !payload?.length) return null;
                  const r = payload[0].payload as Reading;
                  return (
                    <div className="tip">
                      <div>{new Date(r.ts * 1000).toLocaleString()}</div>
                      <div><strong>{title}: {Number(r[field]).toFixed(1)}{unit}</strong></div>
                      <div>Pump {r.pump ? "ON" : "OFF"}</div>
                    </div>
                  );
                }}
              />
              <Line type="monotone" dataKey={field} stroke={color} strokeWidth={2} dot={false}
                activeDot={{ r: 5, stroke: "var(--surface)", strokeWidth: 2 }} isAnimationActive={false} />
            </LineChart>
          </ResponsiveContainer>
        </div>
      )}
    </section>
  );
}

export default function Charts(p: Props) {
  const rows = p.data.slice(-60).reverse();
  return (
    <>
      <Metric {...p} title="Soil moisture" unit="%" field="soil" color="var(--series-soil)" domain={[0, 100]} withThresholds />
      <div className="grid2">
        <Metric {...p} title="Temperature" unit="°C" field="temp" color="var(--series-temp)" />
        <Metric {...p} title="Humidity" unit="%" field="hum" color="var(--series-hum)" domain={[0, 100]} />
      </div>
      <details className="panel">
        <summary>View data as table (latest 60 points)</summary>
        <div className="tableWrap">
          <table>
            <thead><tr><th>Time</th><th>Soil %</th><th>Temp °C</th><th>Hum %</th><th>Pump</th></tr></thead>
            <tbody>
              {rows.map((r) => (
                <tr key={r.ts}>
                  <td>{new Date(r.ts * 1000).toLocaleString()}</td>
                  <td>{r.soil.toFixed(1)}</td><td>{r.temp.toFixed(1)}</td><td>{r.hum.toFixed(1)}</td>
                  <td>{r.pump ? "ON" : "OFF"}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </details>
    </>
  );
}
