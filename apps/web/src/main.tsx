import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { ReactFlowProvider } from "@xyflow/react";
import "@xyflow/react/dist/style.css";
import "./styles.css";
import { App } from "./App";
import { ErrorBoundary } from "./ErrorBoundary";
import { loadInstalledToolkits } from "./installedToolkits";

// Installed toolkits register their instruments first, so the first dashboard has them (pb#353).
void loadInstalledToolkits().finally(() => createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <ErrorBoundary>
      <ReactFlowProvider>
        <App />
      </ReactFlowProvider>
    </ErrorBoundary>
  </StrictMode>,
));
