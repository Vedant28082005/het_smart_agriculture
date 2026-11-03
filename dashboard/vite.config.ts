import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// The dashboard calls /api on its own origin and Vite forwards it to the backend. That lets one
// public URL (e.g. a Tailscale Funnel on port 5173) serve both the page and the API.
export default defineConfig({
  plugins: [react()],
  server: {
    allowedHosts: true,
    proxy: { "/api": "http://localhost:8000" },
  },
});
