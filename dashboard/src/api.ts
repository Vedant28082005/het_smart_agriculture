import type { Alert, DeviceEvent, Latest, Reading, Thresholds } from "./types";

const BASE = (import.meta.env.VITE_API_URL as string | undefined) ?? "http://localhost:8000";

export class ApiError extends Error {
  constructor(public status: number, message: string) {
    super(message);
  }
}

async function call<T>(path: string, init?: RequestInit): Promise<T> {
  let res: Response;
  try {
    res = await fetch(`${BASE}${path}`, {
      ...init,
      headers: { "Content-Type": "application/json" },
    });
  } catch {
    throw new ApiError(0, "Cannot reach the backend");
  }
  const body = await res.json().catch(() => ({}));
  if (!res.ok) {
    const d = body.detail;
    // FastAPI validation errors come back as a list of {msg}
    const msg = Array.isArray(d) ? d.map((x: { msg: string }) => x.msg).join("; ") : d ?? body.error ?? res.statusText;
    throw new ApiError(res.status, String(msg));
  }
  return body as T;
}

const post = (path: string, data: unknown) =>
  call<unknown>(path, { method: "POST", body: JSON.stringify(data) });

export const api = {
  latest: () => call<Latest>("/api/latest"),
  history: (hours: number) => call<Reading[]>(`/api/history?hours=${hours}&limit=400`),
  events: () => call<DeviceEvent[]>("/api/events?limit=30"),
  alerts: () => call<Alert[]>("/api/alerts"),
  setPump: (state: "on" | "off") => post("/api/pump", { state }),
  setMode: (mode: "auto" | "manual") => post("/api/mode", { mode }),
  setThresholds: (t: Thresholds) => post("/api/thresholds", t),
};
