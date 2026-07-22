// Shared access-code gate. Included on every page as <script src="/js/cpauth.js">.
// Self-contained: builds its own overlay DOM and styles so it needs nothing from
// the host page. On load it asks the server whether a code is still needed and,
// if so, blocks with an overlay until the 4-digit code (shown on the reader
// screen) is entered. On success the session cookie is set and the page reloads.
// Monochrome (Ledger theme): white card, black ink, square edges.
(function () {
  if (window.__cpAuthInit) return;
  window.__cpAuthInit = true;

  var gate, input, msg, btn;

  function build() {
    gate = document.createElement('div');
    gate.setAttribute('style',
      'position:fixed;inset:0;z-index:99999;display:none;background:rgba(0,0,0,.55);' +
      'align-items:center;justify-content:center;' +
      "font-family:-apple-system,system-ui,'Segoe UI',Roboto,Helvetica,Arial,sans-serif");

    var card = document.createElement('div');
    card.setAttribute('style',
      'background:#fff;color:#111;max-width:320px;width:calc(100% - 36px);' +
      'border:1px solid #111;box-shadow:0 20px 60px rgba(0,0,0,.4);padding:20px 18px;text-align:center');

    var title = document.createElement('div');
    title.setAttribute('style', 'font-size:15px;font-weight:800;letter-spacing:.02em;margin-bottom:6px');
    title.textContent = 'Enter the access code';
    card.appendChild(title);

    var hint = document.createElement('div');
    hint.setAttribute('style', 'font-size:12.5px;color:#666;margin-bottom:16px');
    hint.textContent = "Look at your reader’s screen — it shows a 4-digit code. Type it here once.";
    card.appendChild(hint);

    input = document.createElement('input');
    input.setAttribute('inputmode', 'numeric');
    input.setAttribute('maxlength', '4');
    input.setAttribute('autocomplete', 'off');
    input.setAttribute('aria-label', 'Access code');
    input.setAttribute('style',
      'width:150px;text-align:center;letter-spacing:10px;font-size:26px;font-weight:700;' +
      'padding:9px 8px;border:1.5px solid #111;background:#f4f4f4;color:#111;outline:none');
    card.appendChild(input);

    msg = document.createElement('div');
    msg.setAttribute('style', 'min-height:18px;font-size:12px;color:#111;margin:10px 0 4px');
    card.appendChild(msg);

    btn = document.createElement('button');
    btn.type = 'button';
    btn.setAttribute('style',
      'width:100%;font-size:14px;font-weight:700;letter-spacing:.03em;padding:11px;border:1px solid #111;' +
      'background:#111;color:#fff;cursor:pointer');
    btn.textContent = 'Unlock';
    card.appendChild(btn);

    gate.appendChild(card);
    document.body.appendChild(gate);

    btn.addEventListener('click', submit);
    input.addEventListener('keydown', function (e) { if (e.key === 'Enter') submit(); });
  }

  function show() {
    gate.style.display = 'flex';
    setTimeout(function () { input.focus(); }, 60);
  }

  function submit() {
    var code = (input.value || '').trim();
    if (code.length !== 4) { msg.textContent = 'Enter 4 digits.'; return; }
    btn.disabled = true; msg.style.color = '#666'; msg.textContent = 'Checking…';
    fetch('/api/auth', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'code=' + encodeURIComponent(code)
    })
      .then(function (r) { return r.json().then(function (j) { return { s: r.status, j: j }; }); })
      .then(function (o) {
        if (o.s === 200 && o.j.ok) { location.reload(); return; }
        btn.disabled = false; msg.style.color = '#111';
        if (o.s === 429 || (o.j && o.j.lockedOut)) {
          msg.textContent = 'Too many tries. Wait ' + ((o.j && o.j.retryAfter) || 30) + 's.';
        } else {
          msg.textContent = 'Wrong code. Try again.'; input.value = ''; input.focus();
        }
      })
      .catch(function () { btn.disabled = false; msg.style.color = '#111'; msg.textContent = 'Network error.'; });
  }

  function start() {
    build();
    fetch('/api/auth')
      .then(function (r) { return r.json(); })
      .then(function (j) { if (j && !j.authenticated) show(); })
      .catch(function () {});
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
