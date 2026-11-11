import { onPanelShown, startPanel } from "./ui/panel";

// eslint-disable-next-line @typescript-eslint/no-require-imports
declare const require: (id: string) => any;

// Refresh the sequence list whenever the panel is opened or shown again.
try {
  require("uxp").entrypoints.setup({
    panels: {
      trackRangeExporterPanel: {
        show() {
          startPanel();
          onPanelShown();
        },
      },
    },
  });
} catch {
  /* not running in UXP (tests) */
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", startPanel);
} else {
  startPanel();
}
