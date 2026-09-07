import ReactDOM from "react-dom/client";
import "./fonts.css";
import "@fontsource/ibm-plex-mono/latin-400.css";
import "@fontsource/ibm-plex-mono/latin-500.css";
import App from "./App";
import { installBrowserDiagnostics } from "./diagnostics";
installBrowserDiagnostics();
ReactDOM.createRoot(document.getElementById("root")!).render(<App />);
