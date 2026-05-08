#pragma once
#include <pgmspace.h>

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>mmWave</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{background:#0f0f0f;color:#fff;font-family:-apple-system,BlinkMacSystemFont,sans-serif;
         display:flex;flex-direction:column;align-items:center;justify-content:center;
         min-height:100vh;gap:1.5rem}
    h1{font-size:.8rem;color:#555;text-transform:uppercase;letter-spacing:.2em}
    .row{display:flex;gap:1.5rem;flex-wrap:wrap;justify-content:center}
    .card{background:#1a1a1a;border:1px solid #252525;border-radius:16px;
          padding:2rem 0;text-align:center;width:160px}
    .label{font-size:.65rem;color:#555;text-transform:uppercase;letter-spacing:.14em;margin-bottom:.6rem}
    .val{font-size:3.8rem;font-weight:700;color:#3cf;line-height:1;font-variant-numeric:tabular-nums}
    .unit{font-size:.7rem;color:#444;margin-top:.5rem}
    footer{font-size:.65rem;color:#666;display:flex;align-items:center;gap:.4rem}
    .pdot{width:7px;height:7px;border-radius:50%;background:#444}
    .pdot.on{background:#3d3}
  </style>
</head>
<body>
  <h1>mmWave Monitor</h1>
  <div class="row">
    <div class="card">
      <div class="label">Breathing Rate</div>
      <div class="val" id="br">--</div>
      <div class="unit">rpm</div>
    </div>
    <div class="card">
      <div class="label">Heart Rate</div>
      <div class="val" id="hr">--</div>
      <div class="unit">bpm</div>
    </div>
  </div>
  <footer>
    <div id="pdot" class="pdot"></div><span id="pst">no presence</span>
    &nbsp;·&nbsp;<span id="lx">--</span> lux
    &nbsp;·&nbsp;<span id="tmode"></span>
  </footer>
  <script>
    var ws, retries = 0;
    var ePdot  = document.getElementById('pdot');
    var ePst   = document.getElementById('pst');
    var eBr    = document.getElementById('br');
    var eHr    = document.getElementById('hr');
    var eLx    = document.getElementById('lx');
    var eTmode = document.getElementById('tmode');
    function connect() {
      ws = new WebSocket('ws://' + location.hostname + '/ws');
      ws.onopen = function() { retries = 0; };
      ws.onmessage = function(e) {
        var d = JSON.parse(e.data);
        if (d.br != null) eBr.textContent = d.br;
        if (d.hr != null) eHr.textContent = d.hr;
        if (d.lx != null) eLx.textContent = d.lx;
        if (d.presence != null) {
          ePdot.className = 'pdot' + (d.presence ? ' on' : '');
          ePst.textContent = d.presence ? 'presence' : 'no presence';
        }
      };
      ws.onclose = function() {
        setTimeout(connect, Math.min(1000 * Math.pow(2, retries++), 10000));
      };
    }
    connect();
    fetch('/info').then(function(r){return r.json();}).then(function(d){
      var m = ['', 'Always', 'Dark only', 'Light only'];
      eTmode.textContent = 'Track: ' + m[d.track] + (d.track > 1 ? ' (' + d.threshold + ' lux)' : '');
    });
  </script>
</body>
</html>
)html";
