// Paperboat docs theme switch: auto (system) / light / dark, remembered in localStorage.
(function () {
  var KEY = "pb-docs-theme";
  var root = document.documentElement;

  function apply(mode) {
    if (mode === "light" || mode === "dark") {
      root.setAttribute("data-theme", mode);
    } else {
      root.removeAttribute("data-theme");
    }
  }

  function current() {
    return localStorage.getItem(KEY) || "auto";
  }

  apply(current());

  function makeSwitch() {
    var wrap = document.createElement("div");
    wrap.id = "pb-theme-switch";
    ["auto", "light", "dark"].forEach(function (m) {
      var b = document.createElement("button");
      b.textContent = m;
      b.setAttribute("aria-pressed", String(m === current()));
      b.addEventListener("click", function () {
        localStorage.setItem(KEY, m);
        apply(m);
        wrap.querySelectorAll("button").forEach(function (x) {
          x.setAttribute("aria-pressed", String(x.textContent === m));
        });
      });
      wrap.appendChild(b);
    });
    document.body.appendChild(wrap);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", makeSwitch);
  } else {
    makeSwitch();
  }
})();
