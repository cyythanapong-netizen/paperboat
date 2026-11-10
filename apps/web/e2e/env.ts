// Where the E2E servers listen. Override with PB_E2E_WEB_PORT / PB_E2E_BACKEND_PORT when these are taken.
export const WEB_PORT = Number(process.env.PB_E2E_WEB_PORT ?? 4180);
export const BACKEND_PORT = Number(process.env.PB_E2E_BACKEND_PORT ?? 4181);
export const BACKEND_URL = `ws://localhost:${BACKEND_PORT}`;
// The empty-Flow fixture (engine/tests/sdk-backend/empty-main.cpp), started by its spec:
// one with a station object and its linked block, one with nothing.
export const STATION_BACKEND_PORT = Number(process.env.PB_E2E_STATION_BACKEND_PORT ?? 4182);
export const EMPTY_BACKEND_PORT = Number(process.env.PB_E2E_EMPTY_BACKEND_PORT ?? 4183);
