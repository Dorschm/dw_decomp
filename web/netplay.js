/* Multiplayer presence overlay for the Digimon World web port.
 * The game itself stays single-player; this reads the local tamer
 * (web_net_local), exchanges it with net.php, and draws the other
 * tamers on the same map as markers placed with the game's own camera
 * (web_net_project). Chat/emotes show as bubbles. No game data is sent. */
(function () {
  'use strict';
  var EMOTES = ['', '👋', '❤️', '❗', '❓', '😄', '🎵', '⚔️', '🍖', 'GG'];
  var API = 'net.php';
  var POLL_MS = 250;

  var stage = document.querySelector('.stage');
  if (!stage) return;
  var layer = document.createElement('div');
  layer.style.cssText = 'position:absolute;inset:0;pointer-events:none;overflow:hidden';
  stage.appendChild(layer);

  var id = (function () {
    try {
      var v = localStorage.getItem('dw_net_id');
      if (v) return v;
    } catch (e) {}
    var a = new Uint8Array(6);
    crypto.getRandomValues(a);
    var s = Array.prototype.map.call(a, function (b) {
      return ('0' + b.toString(16)).slice(-2);
    }).join('');
    try { localStorage.setItem('dw_net_id', s); } catch (e) {}
    return s;
  })();
  var name = (function () {
    try { return localStorage.getItem('dw_net_name') || ''; } catch (e) { return ''; }
  })();

  /* ---- controls ---- */
  var bar = document.createElement('div');
  bar.className = 'bar';
  bar.innerHTML =
    '<input id="netName" maxlength="16" placeholder="tamer name" ' +
    'style="background:#1a1d24;border:1px solid #333a47;color:#dfe3ea;' +
    'padding:7px 10px;border-radius:5px;width:140px">' +
    '<input id="netSay" maxlength="80" placeholder="say something…" ' +
    'style="background:#1a1d24;border:1px solid #333a47;color:#dfe3ea;' +
    'padding:7px 10px;border-radius:5px;flex:1;min-width:160px">' +
    '<span id="netEmo"></span><span id="netInfo" class="note"></span>';
  stage.parentNode.insertBefore(bar, stage.nextSibling);
  var nameEl = bar.querySelector('#netName');
  var sayEl = bar.querySelector('#netSay');
  var emoEl = bar.querySelector('#netEmo');
  var infoEl = bar.querySelector('#netInfo');
  nameEl.value = name;
  for (var i = 1; i < EMOTES.length; i++) (function (n) {
    var b = document.createElement('button');
    b.textContent = EMOTES[n];
    b.style.padding = '7px 9px';
    b.addEventListener('click', function () { pendingEmo = n; });
    emoEl.appendChild(b);
  })(i);
  /* keep typing out of the game's key handling */
  ['keydown', 'keyup', 'keypress'].forEach(function (t) {
    [nameEl, sayEl].forEach(function (el) {
      el.addEventListener(t, function (e) { e.stopPropagation(); }, true);
    });
  });
  nameEl.addEventListener('change', function () {
    name = nameEl.value.trim();
    try { localStorage.setItem('dw_net_name', name); } catch (e) {}
  });
  var pendingSay = '', pendingEmo = 0;
  sayEl.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' && sayEl.value.trim()) {
      pendingSay = sayEl.value.trim();
      sayEl.value = '';
    }
  });

  /* ---- state ---- */
  var remotes = {};   /* id -> {name,map,x,y,z,rot,tx,ty,tz,el,bubble,bt} */
  var seq = -1, online = 0, lastOk = 0, busy = false;

  function mod() { return window.Module && Module.calledRun && Module.HEAP32 && Module._web_net_local ? Module : null; }

  function hue(s) {
    var h = 0;
    for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) % 360;
    return h;
  }

  function makeEl(p) {
    var el = document.createElement('div');
    el.style.cssText = 'position:absolute;transform:translate(-50%,-100%);' +
      'text-align:center;font:600 12px/1.2 "Segoe UI",sans-serif;color:#fff;' +
      'text-shadow:0 1px 2px #000,0 0 3px #000;white-space:nowrap;display:none';
    var h = hue(p.id);
    el.innerHTML =
      '<div class="b" style="display:none;margin:0 auto 4px;max-width:200px;' +
      'white-space:normal;background:#fff;color:#111;text-shadow:none;' +
      'padding:3px 7px;border-radius:8px;font-weight:500"></div>' +
      '<svg width="26" height="38" viewBox="0 0 26 38">' +
      '<ellipse cx="13" cy="35" rx="9" ry="3" fill="rgba(0,0,0,.35)"/>' +
      '<rect x="6" y="15" width="14" height="17" rx="5" fill="hsl(' + h + ',65%,50%)" stroke="#000" stroke-opacity=".5"/>' +
      '<circle cx="13" cy="10" r="8" fill="#f1c8a0" stroke="#000" stroke-opacity=".5"/>' +
      '<path d="M5 9 Q13 -3 21 9 Q13 5 5 9Z" fill="hsl(' + ((h + 40) % 360) + ',55%,35%)"/>' +
      '</svg><div class="n"></div>';
    el.querySelector('.n').textContent = p.name;
    layer.appendChild(el);
    return el;
  }

  function bubble(r, text) {
    var b = r.el.querySelector('.b');
    b.textContent = text;
    b.style.display = 'block';
    r.bt = performance.now() + 6000;
  }

  function sayText(c) {
    var e = c.emo > 0 ? EMOTES[c.emo] + ' ' : '';
    return e + (c.say || '');
  }

  function tick() {
    var M = mod();
    if (!M || busy) return;
    var L = M._web_net_local();
    var h = M.HEAP32, o = L >> 2;
    var inField = h[o] === 1;
    var body = { id: id, name: name || 'Tamer', since: seq, map: -1 };
    if (inField) {
      body.map = h[o + 1]; body.x = h[o + 2]; body.y = h[o + 3];
      body.z = h[o + 4]; body.rot = h[o + 5]; body.anim = h[o + 7];
    }
    if (pendingSay) { body.say = pendingSay; pendingSay = ''; }
    if (pendingEmo) { body.emo = pendingEmo; pendingEmo = 0; }
    busy = true;
    fetch(API, { method: 'POST', body: JSON.stringify(body) })
      .then(function (r) { return r.json(); })
      .then(function (d) {
        busy = false;
        if (d.error) { infoEl.textContent = 'multiplayer: ' + d.error; return; }
        lastOk = performance.now();
        online = d.online;
        var seen = {};
        d.players.forEach(function (p) {
          seen[p.id] = 1;
          var r = remotes[p.id];
          if (!r) {
            r = remotes[p.id] = { x: p.x, y: p.y, z: p.z };
            r.el = makeEl(p);
          }
          r.name = p.name; r.map = p.map;
          r.tx = p.x; r.ty = p.y; r.tz = p.z;
          r.el.querySelector('.n').textContent = p.name;
        });
        Object.keys(remotes).forEach(function (k) {
          if (!seen[k]) { remotes[k].el.remove(); delete remotes[k]; }
        });
        d.chat.forEach(function (c) {
          if (c.seq > seq) seq = c.seq;
          if (c.id === id) return;
          var r = remotes[c.id];
          if (r) bubble(r, sayText(c));
        });
        if (d.seq > seq && !d.chat.length) seq = d.seq;
      })
      .catch(function () { busy = false; });
    window.__netLocal = { inField: inField, map: body.map };
  }

  function draw() {
    requestAnimationFrame(draw);
    var M = mod();
    var L = M && M._web_net_local();
    var here = L ? M.HEAP32[L >> 2] === 1 : false;
    var map = here ? M.HEAP32[(L >> 2) + 1] : -1;
    var cr = stage.getBoundingClientRect();
    var cv = document.getElementById('canvas').getBoundingClientRect();
    var mg = here ? M._web_get_margins() : 0;
    var E = mg & 0xffff, F = (mg >> 16) & 0xffff;
    var iw = 320 + 2 * E, ih = 240 + 2 * F;
    var k = Math.min(cv.width / iw, cv.height / ih);
    var ox = cv.left - cr.left + (cv.width - iw * k) / 2;
    var oy = cv.top - cr.top + (cv.height - ih * k) / 2;
    var now = performance.now();
    var shown = 0;
    Object.keys(remotes).forEach(function (kid) {
      var r = remotes[kid];
      var vis = here && r.map === map;
      if (!vis) { r.el.style.display = 'none'; return; }
      /* ease toward the latest network position */
      r.x += (r.tx - r.x) * 0.25;
      r.y += (r.ty - r.y) * 0.25;
      r.z += (r.tz - r.z) * 0.25;
      var P = M._web_net_project(r.x | 0, r.y | 0, r.z | 0) >> 1;
      var sx = M.HEAP16[P], sy = M.HEAP16[P + 1];
      /* getEntityScreenPos is centre-origin: (0,0) = middle of the 320x240 view */
      var px = ox + (sx + 160 + E) * k, py = oy + (sy + 120 + F) * k;
      if (px < -50 || py < -50 || px > cr.width + 50 || py > cr.height + 50) {
        r.el.style.display = 'none';
        return;
      }
      shown++;
      r.el.style.display = 'block';
      r.el.style.left = px + 'px';
      r.el.style.top = py + 'px';
      r.el.style.transform = 'translate(-50%,-100%) scale(' + Math.max(0.6, k * 0.7) + ')';
      if (r.bt && now > r.bt) {
        r.el.querySelector('.b').style.display = 'none';
        r.bt = 0;
      }
    });
    var ok = now - lastOk < 3000;
    infoEl.textContent = ok ? 'online: ' + online + (shown ? ' · ' + shown + ' nearby' : '')
                            : 'multiplayer: connecting…';
  }

  setInterval(tick, POLL_MS);
  requestAnimationFrame(draw);
})();
