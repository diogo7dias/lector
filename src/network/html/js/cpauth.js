// Shared access-code gate. Included on every page as <script src="/js/cpauth.js">.
// Self-contained: builds its own overlay DOM and styles so it needs nothing from
// the host page. On load it asks the server whether a code is still needed and,
// if so, blocks with an overlay until the 4-digit code (shown on the reader
// screen) is entered. On success the session cookie is set and the page reloads.
(function () {
  if (window.__cpAuthInit) return;
  window.__cpAuthInit = true;

  var gate, input, msg, btn;

  function build() {
    gate = document.createElement('div');
    gate.setAttribute('style',
      'position:fixed;inset:0;z-index:99999;display:none;background:rgba(15,13,8,.72);' +
      'align-items:center;justify-content:center;' +
      "font-family:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif");

    var card = document.createElement('div');
    card.setAttribute('style',
      'background:#fbf9f4;color:#26231d;max-width:340px;width:calc(100% - 36px);' +
      'border-radius:16px;box-shadow:0 20px 60px rgba(0,0,0,.35);padding:22px 20px;text-align:center');

    var title = document.createElement('div');
    title.setAttribute('style', 'font-size:17px;font-weight:700;margin-bottom:6px');
    title.textContent = 'Enter the access code';
    card.appendChild(title);

    var hint = document.createElement('div');
    hint.setAttribute('style', 'font-size:13px;color:#5f5a4f;margin-bottom:16px');
    hint.textContent = "Look at your reader’s screen — it shows a 4-digit code. Type it here once.";
    card.appendChild(hint);

    input = document.createElement('input');
    input.setAttribute('inputmode', 'numeric');
    input.setAttribute('maxlength', '4');
    input.setAttribute('autocomplete', 'off');
    input.setAttribute('aria-label', 'Access code');
    input.setAttribute('style',
      'width:150px;text-align:center;letter-spacing:10px;font-size:26px;font-weight:700;' +
      'padding:10px 8px;border:1.5px solid #236b63;border-radius:12px;background:#f0ebe0;color:#26231d;outline:none');
    card.appendChild(input);

    msg = document.createElement('div');
    msg.setAttribute('style', 'min-height:18px;font-size:12.5px;color:#a83c2c;margin:10px 0 4px');
    card.appendChild(msg);

    btn = document.createElement('button');
    btn.type = 'button';
    btn.setAttribute('style',
      'width:100%;font-size:15px;font-weight:700;padding:11px;border:0;border-radius:12px;' +
      'background:#236b63;color:#fff;cursor:pointer');
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
    if (code.length !== 4) { msg.style.color = '#a83c2c'; msg.textContent = 'Enter 4 digits.'; return; }
    btn.disabled = true; msg.style.color = '#5f5a4f'; msg.textContent = 'Checking…';
    fetch('/api/auth', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'code=' + encodeURIComponent(code)
    })
      .then(function (r) { return r.json().then(function (j) { return { s: r.status, j: j }; }); })
      .then(function (o) {
        if (o.s === 200 && o.j.ok) { location.reload(); return; }
        btn.disabled = false; msg.style.color = '#a83c2c';
        if (o.s === 429 || (o.j && o.j.lockedOut)) {
          msg.textContent = 'Too many tries. Wait ' + ((o.j && o.j.retryAfter) || 30) + 's.';
        } else {
          msg.textContent = 'Wrong code. Try again.'; input.value = ''; input.focus();
        }
      })
      .catch(function () { btn.disabled = false; msg.style.color = '#a83c2c'; msg.textContent = 'Network error.'; });
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
