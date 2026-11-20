// Loaded first: shows any script error inside the panel instead of an empty panel.
(function () {
  function show(msg) {
    try {
      var el = document.getElementById("boot-error");
      if (!el) return;
      el.textContent = (el.textContent ? el.textContent + "\n" : "") + msg;
      el.style.display = "block";
    } catch (e) { /* nothing else to do */ }
  }
  window.addEventListener("error", function (e) {
    show("Script error: " + (e && e.message ? e.message : e) + (e && e.filename ? " (" + e.filename + ":" + e.lineno + ")" : ""));
  });
  window.addEventListener("unhandledrejection", function (e) {
    show("Error: " + (e && e.reason && e.reason.message ? e.reason.message : e && e.reason));
  });
})();
