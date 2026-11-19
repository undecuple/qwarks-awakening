/* Qwark's Awakening site: optional enhancement. The page is complete without it (and plain.html needs no script at all).
   Same origin, no network calls, no storage, no analytics. website.py inlines this file into index.html after the image
   viewer's script; the page's Content-Security-Policy allows the whole inline script by its sha256 only.
   What it does: shows one section at a time like a front-end menu, reads the arrow keys, Enter, Esc and a game pad.
   The image viewer (a modal dialog) is the other part of the inline script (website.py VIEWER_JS); the keys below
   stand aside while it is open.
   Carried from the Killzone 3 port's public/site/site.js (same author, MIT): the hash-driven section switching, the
   menu key handling and the game pad polling. Its own screenshot viewer is not carried (this page has a dialog). */
(function () {
  "use strict";
  var root = document.documentElement;
  root.classList.add("js");

  function ready(fn) {
    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", fn);
    else fn();
  }

  ready(function () {
    var menu = document.getElementById("menu");
    var panel = document.getElementById("panel");
    var detail = document.getElementById("detail");
    if (!menu || !panel) return;
    var links = Array.prototype.slice.call(menu.querySelectorAll("ol a"));
    var secs = Array.prototype.slice.call(panel.querySelectorAll(":scope > .sec"));
    var current = null;
    var narrow = window.matchMedia("(max-width: 860px)");
    var reduce = window.matchMedia("(prefers-reduced-motion: reduce)");

    function idOf(link) { return link.getAttribute("href").slice(1); }

    /* The section a hash points at: the section itself, or the one that holds the target (#game-tods, #h-status). */
    function sectionFor(hash) {
      var id = decodeURIComponent((hash || "").replace(/^#/, ""));
      if (!id) return { sec: secs[0], target: null };
      var el = document.getElementById(id);
      if (!el) return { sec: secs[0], target: null };
      var sec = el.closest(".sec");
      return { sec: sec || secs[0], target: el === sec ? null : el };
    }

    function show(sec, target) {
      secs.forEach(function (s) { s.classList.toggle("on", s === sec); });
      links.forEach(function (a) {
        var on = idOf(a) === sec.id;
        a.classList.toggle("on", on);
        if (on) {
          a.setAttribute("aria-current", "true");
          if (detail) detail.textContent = a.getAttribute("data-detail") || "";
        } else {
          a.removeAttribute("aria-current");
        }
      });
      if (current !== sec) {
        current = sec;
        panel.scrollTop = 0;
        if (narrow.matches && root.classList.contains("ready")) {
          var top = panel.getBoundingClientRect().top + window.pageYOffset - 8;
          window.scrollTo(0, Math.max(0, top));
        }
      }
      if (target) target.scrollIntoView({ block: "start", behavior: reduce.matches ? "auto" : "smooth" });
    }

    function fromHash() {
      var r = sectionFor(location.hash);
      show(r.sec, r.target);
    }

    function selectIndex(i, focus) {
      if (i < 0) i = links.length - 1;
      if (i >= links.length) i = 0;
      location.replace("#" + idOf(links[i]));   /* the section follows the hash; no history entry per step */
      if (focus) links[i].focus();
    }

    function currentIndex() {
      var i = links.indexOf(document.activeElement);
      if (i >= 0) return i;
      for (i = 0; i < links.length; i++) if (links[i].classList.contains("on")) return i;
      return 0;
    }

    function inMenuOrBody() {
      var a = document.activeElement;
      return !a || a === document.body || menu.contains(a);
    }

    function viewerOpen() { return !!document.querySelector("dialog[open]"); }
    function openPanel() { panel.focus({ preventScroll: true }); }
    function backToMenu() { links[currentIndex()].focus(); }

    window.addEventListener("hashchange", fromHash);

    /* ---- keys: Up/Down walk the menu, Enter opens the section, Esc goes back to the menu ---- */
    document.addEventListener("keydown", function (e) {
      if (e.altKey || e.ctrlKey || e.metaKey || viewerOpen()) return;
      var k = e.key;
      if (narrow.matches && k !== "Escape") return;   /* on a phone the page scrolls; the keys keep their usual job */
      if (k === "ArrowDown" && inMenuOrBody()) { e.preventDefault(); selectIndex(currentIndex() + 1, true); }
      else if (k === "ArrowUp" && inMenuOrBody()) { e.preventDefault(); selectIndex(currentIndex() - 1, true); }
      else if (k === "Enter" && menu.contains(document.activeElement)) {
        /* the link opens the section (the hash); then the focus moves into it so the arrows scroll it */
        window.setTimeout(openPanel, 0);
      } else if (k === "Escape" && panel.contains(document.activeElement)) { e.preventDefault(); backToMenu(); }
    });

    /* ---- game pad (optional): d-pad up/down, A (0) = Enter, B (1) = Esc ---- */
    var pads = 0, last = {}, lastTime = {};
    function pressed(gp, i) { return gp.buttons[i] && gp.buttons[i].pressed; }
    function fire(name, now, repeat) {
      if (last[name] && !(repeat && now - lastTime[name] > 260)) return false;
      last[name] = true; lastTime[name] = now;
      return true;
    }
    function poll() {
      var list = navigator.getGamepads ? navigator.getGamepads() : [];
      var now = Date.now(), gp = null, i;
      for (i = 0; i < list.length; i++) if (list[i]) { gp = list[i]; break; }
      if (gp && !viewerOpen()) {
        var st = {
          up: pressed(gp, 12) || (gp.axes[1] || 0) < -0.6, down: pressed(gp, 13) || (gp.axes[1] || 0) > 0.6,
          a: pressed(gp, 0), b: pressed(gp, 1)
        };
        Object.keys(st).forEach(function (n) {
          if (!st[n]) { last[n] = false; return; }
          if (!fire(n, now, n !== "a" && n !== "b")) return;
          if (n === "down") selectIndex(currentIndex() + 1, true);
          else if (n === "up") selectIndex(currentIndex() - 1, true);
          else if (n === "a") {
            var el = document.activeElement;
            if (el && el !== document.body && el.click) el.click(); else openPanel();
            if (menu.contains(el)) window.setTimeout(openPanel, 0);
          } else if (n === "b") backToMenu();
        });
      }
      if (pads > 0) window.requestAnimationFrame(poll);
    }
    window.addEventListener("gamepadconnected", function () {
      pads += 1;
      if (pads === 1) window.requestAnimationFrame(poll);
    });
    window.addEventListener("gamepaddisconnected", function () { pads = Math.max(0, pads - 1); });

    fromHash();
    root.classList.add("ready");
  });
})();
