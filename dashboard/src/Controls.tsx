import { useEffect, useState } from "react";
import { api, ApiError } from "./api";
import { isLockout, reasonText } from "./reasons";
import type { Reading, Thresholds } from "./types";

interface Props {
  reading: Reading | null;
  online: boolean;
  thresholds: Thresholds;
  onChanged: () => void;
}

/** Why the pump button is disabled, or null if it can be used. */
function pumpBlockReason(r: Reading | null, online: boolean): string | null {
  if (!online || !r) return "Device is offline";
  if (r.mode !== "manual") return "Switch to manual mode to control the pump";
  if (!r.tank_ok) return "Tank is empty, pump is locked out";
  if (isLockout(r.reason) && !r.pump) return reasonText(r.reason);
  return null;
}

function validate(t: Record<keyof Thresholds, string>): Partial<Record<keyof Thresholds, string>> {
  const e: Partial<Record<keyof Thresholds, string>> = {};
  const n = (k: keyof Thresholds) => (t[k].trim() === "" ? NaN : Number(t[k]));
  const on = n("on"), off = n("off"), run = n("max_run_s"), cool = n("cooldown_s");
  if (!Number.isInteger(on) || on < 0 || on > 100) e.on = "0-100";
  if (!Number.isInteger(off) || off < 0 || off > 100) e.off = "0-100";
  if (!e.on && !e.off && on >= off) e.on = "Must be below OFF";
  if (!Number.isInteger(run) || run < 1 || run > 3600) e.max_run_s = "1-3600 s";
  if (!Number.isInteger(cool) || cool < 0 || cool > 86400) e.cooldown_s = "0-86400 s";
  return e;
}

export default function Controls({ reading, online, thresholds, onChanged }: Props) {
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [form, setForm] = useState(() => toForm(thresholds));
  const [dirty, setDirty] = useState(false);

  // Follow the device's thresholds until the user starts editing.
  useEffect(() => {
    if (!dirty) setForm(toForm(thresholds));
  }, [thresholds, dirty]);

  const run = async (fn: () => Promise<unknown>) => {
    setBusy(true);
    setError("");
    try {
      await fn();
      onChanged();
    } catch (e) {
      setError(e instanceof ApiError ? (e.status === 504 ? "Device unreachable" : e.message) : "Request failed");
    } finally {
      setBusy(false);
    }
  };

  const block = pumpBlockReason(reading, online);
  const mode = reading?.mode;
  const errors = validate(form);
  const formValid = Object.keys(errors).length === 0;

  const submit = (ev: React.FormEvent) => {
    ev.preventDefault();
    if (!formValid) return;
    run(async () => {
      await api.setThresholds({
        on: Number(form.on), off: Number(form.off),
        max_run_s: Number(form.max_run_s), cooldown_s: Number(form.cooldown_s),
      });
      setDirty(false);
    });
  };

  const field = (k: keyof Thresholds, label: string) => (
    <label>
      {label}
      <input
        inputMode="numeric"
        value={form[k]}
        aria-invalid={!!errors[k]}
        onChange={(e) => { setDirty(true); setForm({ ...form, [k]: e.target.value }); }}
      />
      <span className="err" style={{ margin: 0, minHeight: 16 }}>{errors[k] ?? ""}</span>
    </label>
  );

  return (
    <section className="panel" aria-label="Controls">
      <h2>Controls</h2>
      <div className="row">
        <div className="seg" role="group" aria-label="Mode">
          <button aria-pressed={mode === "auto"} disabled={busy || !online} onClick={() => run(() => api.setMode("auto"))}>Auto</button>
          <button aria-pressed={mode === "manual"} disabled={busy || !online} onClick={() => run(() => api.setMode("manual"))}>Manual</button>
        </div>
        <button
          className={`primary ${reading?.pump ? "stop" : ""}`}
          disabled={busy || block !== null}
          onClick={() => run(() => api.setPump(reading?.pump ? "off" : "on"))}
        >
          {reading?.pump ? "Turn pump OFF" : "Turn pump ON"}
        </button>
      </div>
      {block && <p className="hint">{block}</p>}
      {error && <p className="err" role="alert">{error}</p>}

      <h2 style={{ marginTop: 20 }}>Thresholds</h2>
      <form onSubmit={submit}>
        {field("on", "Pump ON below (%)")}
        {field("off", "Pump OFF above (%)")}
        {field("max_run_s", "Max run (s)")}
        {field("cooldown_s", "Cooldown (s)")}
        <button className="primary full" type="submit" disabled={busy || !online || !formValid}>Save thresholds</button>
      </form>
    </section>
  );
}

function toForm(t: Thresholds) {
  return {
    on: String(t.on), off: String(t.off),
    max_run_s: String(t.max_run_s), cooldown_s: String(t.cooldown_s),
  };
}
