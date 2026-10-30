(function () {
  "use strict";

  var list = document.getElementById("games");
  var status = document.getElementById("status");
  var HEX = /^[0-9a-fA-F]{6}$/;

  function getJson(url) {
    return fetch(url, { cache: "no-cache" }).then(function (res) {
      if (!res.ok) throw new Error(url + ": " + res.status);
      return res.json();
    });
  }

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  function render(game, meta) {
    var color = HEX.test(game.color) ? "#" + game.color.toLowerCase() : "#c6ff6b";
    var li = el("li", "game");
    li.style.setProperty("--c", color);

    var segment = el("div", "segment");
    segment.setAttribute("aria-hidden", "true");
    li.appendChild(segment);

    var info = el("div", "info");
    info.appendChild(el("h2", null, game.title));
    if (meta && meta.description) info.appendChild(el("p", "desc", meta.description));

    var chip = el("p", "chip");
    var swatch = el("span", "swatch");
    swatch.setAttribute("aria-hidden", "true");
    chip.appendChild(swatch);
    chip.appendChild(el("span", null, "Look for this color \u00b7 " + color.toUpperCase()));
    info.appendChild(chip);

    li.appendChild(info);
    return li;
  }

  function watchActive() {
    var items = list.children;

    // The focus line sweeps from the page top to the page bottom as you scroll, so every
    // game (including the ones near the ends) gets a turn however short the page is.
    function update() {
      var max = document.documentElement.scrollHeight - window.innerHeight;
      var progress = max > 0 ? Math.min(Math.max(window.scrollY / max, 0), 1) : 0;
      var focus = window.scrollY + progress * window.innerHeight;
      var best = null;
      var bestDist = Infinity;
      for (var i = 0; i < items.length; i++) {
        var r = items[i].getBoundingClientRect();
        var dist = Math.abs(r.top + window.scrollY + r.height / 2 - focus);
        if (dist < bestDist) { bestDist = dist; best = items[i]; }
      }
      for (var j = 0; j < items.length; j++) {
        items[j].classList.toggle("active", items[j] === best);
      }
    }

    window.addEventListener("scroll", update, { passive: true });
    window.addEventListener("resize", update);
    update();
  }

  getJson("../games/index.json")
    .then(function (index) {
      var games = (index && index.games) || [];
      return Promise.all(
        games.map(function (game) {
          return getJson("../games/" + encodeURIComponent(game.id) + "/meta.json").then(
            function (meta) { return { game: game, meta: meta }; },
            function () { return { game: game, meta: null }; }
          );
        })
      );
    })
    .then(function (rows) {
      if (!rows.length) {
        status.textContent = "No games in the store yet. Check back soon.";
        return;
      }
      status.hidden = true;
      // The tube is listed bottom-up: the first game in the index sits at the bottom.
      rows.reverse().forEach(function (row) { list.appendChild(render(row.game, row.meta)); });
      list.classList.add("ready");
      watchActive();
    })
    .catch(function () {
      status.textContent = "Couldn't load the game list. Please try again later.";
    });
})();
