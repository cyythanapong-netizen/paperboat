import { createRoot } from "react-dom/client";
import "@paperboat/shared/tokens.css";
import "@xyflow/react/dist/style.css";
import "./styles.css";
import { App } from "./App.tsx";

createRoot(document.getElementById("root")!).render(<App />);
