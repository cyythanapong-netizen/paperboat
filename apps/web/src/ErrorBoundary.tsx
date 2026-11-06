import { Component, type ErrorInfo, type ReactNode } from "react";

type Props = {
  children: ReactNode;
};

type State = {
  error: Error | null;
};

export class ErrorBoundary extends Component<Props, State> {
  override state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  override componentDidCatch(error: Error, info: ErrorInfo) {
    console.error("Uncaught error in UI:", error, info);
  }

  override render() {
    if (this.state.error) {
      return (
        <div style={{ padding: "2rem", color: "#e53e3e", fontFamily: "sans-serif" }}>
          <h2>Something went wrong in the application.</h2>
          <pre style={{ background: "#2d3748", color: "#f7fafc", padding: "1rem", borderRadius: "4px" }}>
            {this.state.error.message}
          </pre>
          <button
            onClick={() => window.location.reload()}
            style={{
              marginTop: "1rem",
              padding: "0.5rem 1rem",
              background: "#3182ce",
              color: "#fff",
              border: "none",
              borderRadius: "4px",
              cursor: "pointer",
            }}
          >
            Reload
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}
