import { startPanel } from "./ui/panel";

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", startPanel);
} else {
  startPanel();
}
