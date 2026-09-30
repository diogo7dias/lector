// Contents look kit: every component at device pixels (480 x 800), with the same
// metrics the firmware draws with (freeink-sdk lists/list.h `contents`, lector
// src/components/ListChrome.cpp `title_page`). One face, Literata:
//   25 semibold tracked caps (titles) · 26 semibold (headings) · 20 regular/bold (body)
//   19 italic and 19 small caps (secondary) · 16 semibold (numerals)
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;');
const ROMAN = ['', 'I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X', 'XI', 'XII', 'XIII', 'XIV', 'XV'];

const K = {
  // The book title page (in-book menu) or a screen's title page: title in tracked
  // capitals, then optional italic line, rule, small-caps line, italic line.
  tp({ t, a, c, p, rule = true }) {
    return `<div class="tp">${t ? `<div class="tp-t">${esc(t)}</div>` : ''}${a ? `<div class="tp-a">${esc(a)}</div>` : ''}${rule ? '<div class="tp-r"></div>' : ''}${c ? `<div class="tp-c">${esc(c)}</div>` : ''}${p ? `<div class="tp-p">${esc(p)}</div>` : ''}</div>`;
  },
  head(n, label) {
    return `<div class="h"><small>${n ? ROMAN[n] : ''}</small>${esc(label)}</div>`;
  },
  // value: string (lower-cased), '>' for a screen, null for an action. sel: cursor.
  row(label, value = null, sel = false, extra = '') {
    const v = value === '>' ? '›' : value == null ? '' : String(value).toLowerCase();
    return `<div class="r${sel ? ' sel' : ''}${extra}"><span class="l">${esc(label)}</span>${v !== '' ? `<span class="d"></span><span class="v">${esc(v)}</span>` : ''}</div>`;
  },
  // Row with an italic second line (book entries, networks, fonts).
  row2(label, sub, value = null, sel = false) {
    const v = value === '>' ? '›' : value == null ? '' : String(value).toLowerCase();
    return `<div class="r2${sel ? ' sel' : ''}"><div class="r"><span class="l">${esc(label)}</span>${v !== '' ? `<span class="d"></span><span class="v">${esc(v)}</span>` : ''}</div><div class="sub">${esc(sub)}</div></div>`;
  },
  // A whole list from a compact spec: '#Label' = heading (auto-numbered), '>' marks the
  // cursor row, [label, value] rows.
  list(items, startNumber = 1) {
    let n = startNumber - 1;
    return items.map((it) => {
      if (typeof it === 'string' && it.startsWith('#')) return K.head(++n, it.slice(1));
      if (typeof it === 'string') return K.row(it);
      const [label, value, sel] = it;
      return K.row(label, value, sel);
    }).join('');
  },
  fn(text) { return `<div class="fn">${esc(text)}</div>`; },
  prose(html, cls = '') { return `<div class="prose ${cls}">${html}</div>`; },
  rule(pct) { return `<div class="bar"><i style="width:${pct}%"></i></div>`; },
  // A popup: a card with a heading and rows, over whatever sits behind it.
  card(inner, top = 260) { return `<div class="veil"></div><div class="card" style="top:${top}px">${inner}</div>`; },
  more(dir) { return `<div class="more ${dir}">${dir === 'up' ? '⌃' : '⌄'}</div>`; },
};

Object.assign(K, {
  // A screen's title page: tracked caps, an italic line, the rule.
  title(t, a) { return K.tp({ t, a }); },
  // Device status at the foot, set like a book's folio: small caps, centred.
  folio(text = '14:32 · 87%') { return `<div class="folio">${esc(text)}</div>`; },
  // A button that is not obvious, said once in italic above the folio.
  hint(text) { return `<div class="hint">${esc(text)}</div>`; },
  // Toast: a paper strip at the top edge, italic line over a hairline, optional bar.
  toast(text, pct) { return `<div class="toast">${esc(text)}${pct != null ? `<div class="tbar"><i style="width:${pct}%"></i></div>` : ''}</div>`; },
  // Deterministic QR-like block pattern.
  qr(size = 198, seed = 7) {
    const n = 25, c = size / n; let s = seed, cells = '';
    const rnd = () => (s = (s * 9301 + 49297) % 233280) / 233280;
    const finder = (x, y) => [[x, y], [x + 18, y], [x, y + 18]].some(([fx, fy]) => x >= 0);
    for (let y = 0; y < n; y++) for (let x = 0; x < n; x++) {
      const inF = (a, b) => x >= a && x < a + 7 && y >= b && y < b + 7;
      let on;
      if (inF(0, 0) || inF(18, 0) || inF(0, 18)) {
        const fx = x < 7 ? x : x - 18, fy = y < 7 ? y : y - 18;
        const r = Math.max(Math.abs(fx - 3), Math.abs(fy - 3));
        on = r === 3 || r <= 1;
      } else on = rnd() > 0.52;
      if (on) cells += `<rect x="${x * c}" y="${y * c}" width="${c + 0.3}" height="${c + 0.3}"/>`;
    }
    return `<svg class="qr" width="${size}" height="${size}" viewBox="0 0 ${size} ${size}" fill="#111">${cells}</svg>`;
  },
  // A woodcut-ish wallpaper: crosshatched moon and hills (placeholder art).
  wallpaper() {
    return `<svg class="wall" viewBox="0 0 480 800" preserveAspectRatio="none"><defs>
      <pattern id="hx" width="6" height="6" patternUnits="userSpaceOnUse" patternTransform="rotate(35)"><rect width="6" height="6" fill="#f3f1ea"/><rect width="2.2" height="6" fill="#111"/></pattern>
      <pattern id="hx2" width="5" height="5" patternUnits="userSpaceOnUse" patternTransform="rotate(-40)"><rect width="5" height="5" fill="url(#hx)"/><rect width="1.6" height="5" fill="#111"/></pattern></defs>
      <rect width="480" height="800" fill="url(#hx)"/><circle cx="330" cy="230" r="92" fill="#f3f1ea"/><circle cx="352" cy="214" r="80" fill="url(#hx)"/>
      <path d="M0 560 Q120 470 230 540 T480 500 V800 H0Z" fill="url(#hx2)"/><path d="M0 660 Q160 590 300 650 T480 630 V800 H0Z" fill="#111"/>
      <path d="M86 660 l18 -120 l18 120z M150 650 l12 -80 l12 80z" fill="#111"/></svg>`;
  },
  page(paras) { return `<div class="page">${paras.map((p) => `<p>${p}</p>`).join('')}</div>`; },
});

const PROSE = [
  'When they were at the Vatican, Dorothea let her husband pass on to the museum of inscriptions. She herself had gone through the long gallery of statues, and now stood by the reclining Ariadne, then called the Cleopatra, in the marble voluptuousness of her beauty.',
  'A young man stood near, looking at her. He was a tall, slight man, with an air of careless ease, and a face that had the quick changes of expression one sees in the lighter races.',
  'Dorothea turned away, and the two men came out together, and walked some way without speaking. The morning was fine, and the sky over the Palatine was of that deep blue which seems to hold light rather than reflect it.',
];
