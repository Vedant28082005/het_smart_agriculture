const TEXT: Record<string, string> = {
  AUTO_LOW_MOISTURE: "Soil too dry, pump started automatically",
  AUTO_TARGET_REACHED: "Soil moist enough, pump idle",
  MANUAL_ON: "Pump switched on manually",
  MANUAL_OFF: "Pump switched off manually",
  LOCKOUT_TANK_EMPTY: "Tank empty, pump locked out",
  LOCKOUT_MAX_RUNTIME: "Max run time hit, pump cooling down",
  LOCKOUT_SENSOR_FAIL: "Sensor failure, pump locked out",
};

export const reasonText = (code?: string) => (code ? TEXT[code] ?? code : "");
export const isLockout = (code?: string) => !!code && code.startsWith("LOCKOUT_");
