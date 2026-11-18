// ===== Embedded font removal =====
// CrossPoint renders with built-in / SD-card fonts and never loads fonts
// embedded in an EPUB, so they are dead weight (often 100KB-2MB per book).
const FONT_EXT_REGEX = /\.(ttf|otf|woff2?|eot)$/;
const FONT_MEDIA_TYPE_REGEX = /^(font\/|application\/font-|application\/x-font-|application\/vnd\.ms-(opentype|fontobject)$)/i;

// Returns a Set of zip-root-relative paths of embedded font files, detected by
// file extension and by OPF manifest media-type (catches odd extensions).
function collectFontPaths(zip, opfContent, opfPath) {
  const fontPaths = new Set();
  zip.forEach(p => { if (FONT_EXT_REGEX.test(p.toLowerCase())) fontPaths.add(p); });

  if (opfContent && opfPath) {
    try {
      const doc = new DOMParser().parseFromString(opfContent, 'application/xml');
      if (!doc.querySelector('parsererror')) {
        for (const item of [...doc.getElementsByTagNameNS('*', 'item')]) {
          const type = item.getAttribute('media-type') || '';
          const href = item.getAttribute('href') || '';
          if (href && FONT_MEDIA_TYPE_REGEX.test(type)) {
            const resolved = resolvePath(opfPath, decodeHref(href));
            if (zip.files[resolved]) fontPaths.add(resolved);
          }
        }
      }
    } catch (e) {
      // Extension-based detection already covers the common cases
    }
  }
  return fontPaths;
}

// Strips all @font-face rules from CSS. Rules can also embed fonts directly as
// multi-hundred-KB data: URIs, so this is a size win even without font files.
function stripFontFaceRules(css) {
  let count = 0;
  const stripped = css.replace(/@font-face(?:\s|\/\*[\s\S]*?\*\/)*\{[^{}]*\}/gi, () => { count++; return ''; });
  return { css: stripped, count };
}

// Removes font-obfuscation <EncryptedData> entries from META-INF/encryption.xml.
// Returns { dropFile: true } when nothing meaningful remains, { modified, xml }
// when some entries were removed, or { modified: false } to keep the file as-is.
function stripFontEncryptionEntries(xmlText, fontPaths) {
  try {
    const doc = new DOMParser().parseFromString(xmlText, 'application/xml');
    if (doc.querySelector('parsererror')) return { modified: false };
    const entries = [...doc.getElementsByTagNameNS('*', 'EncryptedData')];
    let removed = 0;
    for (const entry of entries) {
      const ref = entry.getElementsByTagNameNS('*', 'CipherReference')[0];
      const uri = ref ? decodeHref(ref.getAttribute('URI') || '') : '';
      if (fontPaths.has(uri.replace(/^\//, ''))) {
        entry.parentNode.removeChild(entry);
        removed++;
      }
    }
    if (!removed) return { modified: false };
    const remaining = doc.getElementsByTagNameNS('*', 'EncryptedData').length +
                      doc.getElementsByTagNameNS('*', 'EncryptedKey').length;
    if (remaining === 0) return { dropFile: true };
    return { modified: true, xml: safeSerialize(doc, xmlText) };
  } catch (e) {
    return { modified: false };
  }
}

/**
 * Fix OPF content: fix media-types, strip svg properties,
 * update split image manifest entries, remove stripped font
 * entries, ensure cover meta. DOMParser with regex fallback.
 */
function fixOPF(opfText, opfOriginal, opfDir, splitImages = {}, fontPaths = null) {
  let t = opfText;

  try {
    const parser = new DOMParser();
    const doc = parser.parseFromString(t, 'application/xml');
    if (doc.querySelector('parsererror')) throw new Error('OPF parse failed');

    const items = [...doc.getElementsByTagNameNS('*', 'item')];
    const manifestEl = doc.getElementsByTagNameNS('*', 'manifest')[0];

    // Fix media-types for converted images
    for (const item of items) {
      const href = item.getAttribute('href') || '';
      const type = item.getAttribute('media-type') || '';
      if (href.endsWith('.jpg') && type.match(/^image\/(png|gif|webp|bmp)$/)) {
        item.setAttribute('media-type', 'image/jpeg');
      }
    }

    // Remove 'svg' from properties
    for (const item of items) {
      const props = item.getAttribute('properties') || '';
      if (props.includes('svg')) {
        const newProps = props.split(/\s+/).filter(p => p !== 'svg').join(' ').trim();
        if (newProps) item.setAttribute('properties', newProps);
        else item.removeAttribute('properties');
      }
    }

    // Remove manifest entries for stripped embedded fonts
    if (fontPaths && fontPaths.size) {
      for (const item of items) {
        const href = decodeHref(item.getAttribute('href') || '');
        const full = resolvePath((opfDir ? opfDir + '/' : '') + 'x', href);
        if (fontPaths.has(full)) item.parentNode.removeChild(item);
      }
    }

    // Update split image hrefs and add manifest entries for parts
    for (const [splitKey, splitInfo] of Object.entries(splitImages)) {
      const parts = splitInfo.parts || splitInfo;
      let origHref = opfDir && splitKey.startsWith(opfDir + '/') ? splitKey.substring(opfDir.length + 1) : splitKey;
      const origHrefJpg = origHref.replace(/\.(png|gif|webp|bmp|jpeg)$/i, '.jpg');
      const part1Href = origHrefJpg.replace(/\.jpg$/i, '_part1.jpg');

      for (const item of items) {
        const h = item.getAttribute('href') || '';
        if (h === origHref || h === origHrefJpg || decodeHref(h) === origHref || decodeHref(h) === origHrefJpg) {
          item.setAttribute('href', part1Href);
          break;
        }
      }

      if (manifestEl) {
        const ns = manifestEl.namespaceURI || 'http://www.idpf.org/2007/opf';
        for (let j = 1; j < parts.length; j++) {
          const p = parts[j];
          const href = opfDir && p.path.startsWith(opfDir + '/') ? p.path.substring(opfDir.length + 1) : p.path;
          const newItem = doc.createElementNS(ns, 'item');
          newItem.setAttribute('id', `img-${p.id}`);
          newItem.setAttribute('href', href);
          newItem.setAttribute('media-type', 'image/jpeg');
          manifestEl.appendChild(newItem);
        }
      }
    }

    t = safeSerialize(doc, opfOriginal);
  } catch (e) {
    // Regex fallback
    t = t.replace(/(<(?:\w+:)?item\b[^>]*href="[^"]+\.jpg"[^>]*)media-type="image\/(png|gif|webp|bmp)"/g, '$1media-type="image/jpeg"');
    t = t.replace(/(<(?:\w+:)?item\b[^>]*)media-type="image\/(png|gif|webp|bmp)"([^>]*href="[^"]+\.jpg")/g, '$1media-type="image/jpeg"$3');
    t = t.replace(/\s+svg(?=["'\s>])/g, '');
    if (fontPaths && fontPaths.size) {
      for (const p of fontPaths) {
        const href = opfDir && p.startsWith(opfDir + '/') ? p.substring(opfDir.length + 1) : p;
        const itemRegex = new RegExp(`<(?:\\w+:)?item\\b[^>]*href=["']${escapeRegex(href)}["'][^>]*\\/?>\\s*`, 'gi');
        t = t.replace(itemRegex, '');
      }
    }
    for (const [splitKey, splitInfo] of Object.entries(splitImages)) {
      const parts = splitInfo.parts || splitInfo;
      let origHref = opfDir && splitKey.startsWith(opfDir + '/') ? splitKey.substring(opfDir.length + 1) : splitKey;
      const origHrefJpg = origHref.replace(/\.(png|gif|webp|bmp|jpeg)$/i, '.jpg');
      const part1Href = origHrefJpg.replace(/\.jpg$/i, '_part1.jpg');
      const origImgRegex = new RegExp(`(href=["'])(${escapeRegex(origHref)}|${escapeRegex(origHrefJpg)})(["'])`, 'gi');
      t = t.replace(origImgRegex, `$1${part1Href}$3`);
      let adds = '';
      for (let j = 1; j < parts.length; j++) {
        const p = parts[j];
        const href = opfDir && p.path.startsWith(opfDir + '/') ? p.path.substring(opfDir.length + 1) : p.path;
        adds += `<item id="img-${xmlEscape(p.id)}" href="${xmlEscape(href)}" media-type="image/jpeg"/>\n`;
      }
      if (adds && t.includes('</manifest>')) t = t.replace('</manifest>', adds + '</manifest>');
    }
  }

  // Ensure cover meta
  const cm = ensureCoverMeta(t);
  if (cm.fixed) t = cm.o;

  return t;
}

// Fix SVG cover - converts SVG-wrapped covers to plain HTML img tags
function fixSvgCover(content) {
  const hasSvg = content.includes('<svg') || content.includes('<svg:');
  if (!hasSvg || !content.includes('xlink:href')) return { c: content, fixed: false, count: 0 };
  if (!content.includes('calibre:cover') && !content.includes('name="cover"') && !content.includes('<title>Cover</title>')) return { c: content, fixed: false, count: 0 };

  try {
    const parser = new DOMParser();
    const doc = parser.parseFromString(content, 'application/xhtml+xml');

    if (doc.querySelector('parsererror')) {
      // Fallback to regex
      const m = content.match(/xlink:href=["']([^"']+)["']/);
      if (!m) return { c: content, fixed: false, count: 0 };
      return { c: `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="en" xml:lang="en">
<head><meta content="text/html; charset=UTF-8" http-equiv="default-style"/><title>Cover</title></head>
<body><section epub:type="cover"><img style="max-width:100%;height:auto" alt="Cover" src="${m[1]}"/></section></body>
</html>`, fixed: true, count: 1 };
    }

    // Find SVG elements - check both standard and namespaced variants
    let imgHref = null;
    const svgNS = 'http://www.w3.org/2000/svg';
    const xlinkNS = 'http://www.w3.org/1999/xlink';

    // Try to find all SVG elements
    const svgs = [
      ...doc.getElementsByTagName('svg'),
      ...doc.getElementsByTagNameNS(svgNS, 'svg'),
      ...doc.getElementsByTagName('svg:svg')
    ];

    for (const svg of svgs) {
      // Find image element inside - try all variants
      const imageEl = svg.getElementsByTagName('image')[0] ||
                      svg.getElementsByTagNameNS(svgNS, 'image')[0] ||
                      svg.getElementsByTagName('svg:image')[0];

      if (imageEl) {
        imgHref = imageEl.getAttributeNS(xlinkNS, 'href') ||
                  imageEl.getAttribute('xlink:href') ||
                  imageEl.getAttribute('href');
        if (imgHref) break;
      }
    }

    if (!imgHref) {
      // Fallback to regex
      const m = content.match(/xlink:href=["']([^"']+)["']/);
      if (!m) return { c: content, fixed: false, count: 0 };
      imgHref = m[1];
    }

    return {
      c: `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="en" xml:lang="en">
<head><meta content="text/html; charset=UTF-8" http-equiv="default-style"/><title>Cover</title></head>
<body><section epub:type="cover"><img style="max-width:100%;height:auto" alt="Cover" src="${imgHref}"/></section></body>
</html>`,
      fixed: true,
      count: 1
    };
  } catch (e) {
    // Fallback to regex
    const m = content.match(/xlink:href=["']([^"']+)["']/);
    if (!m) return { c: content, fixed: false, count: 0 };
    return { c: `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="en" xml:lang="en">
<head><meta content="text/html; charset=UTF-8" http-equiv="default-style"/><title>Cover</title></head>
<body><section epub:type="cover"><img style="max-width:100%;height:auto" alt="Cover" src="${m[1]}"/></section></body>
</html>`, fixed: true, count: 1 };
  }
}

// Fix SVG-wrapped images - unwrap SVG and replace with plain img
function fixSvgWrappedImages(content) {
  const hasSvg = content.includes('<svg') || content.includes('<svg:');
  if (!hasSvg || !content.includes('xlink:href')) return { c: content, fixed: false, count: 0 };

  try {
    const parser = new DOMParser();
    const doc = parser.parseFromString(content, 'application/xhtml+xml');

    if (doc.querySelector('parsererror')) {
      // Fallback to regex
      let fixedCount = 0;
      const svgImageRegex = /<(?:svg:)?svg\b[^>]*>[\s\S]*?<(?:svg:)?image\b[^>]*xlink:href=["']([^"']+)["'][^>]*\/?>\s*<\/(?:svg:)?svg>/gi;
      const newContent = content.replace(svgImageRegex, (match, href) => { fixedCount++; return `<img style="max-width:100%;height:auto" src="${href}" alt="" />`; });
      return { c: newContent, fixed: fixedCount > 0, count: fixedCount };
    }

    const svgNS = 'http://www.w3.org/2000/svg';
    const xlinkNS = 'http://www.w3.org/1999/xlink';

    const svgElements = [...doc.querySelectorAll('svg'), ...doc.getElementsByTagNameNS(svgNS, 'svg')];
    const uniqueSvgs = [...new Set(svgElements)];
    let fixedCount = 0;

    for (const svg of uniqueSvgs) {
      const imageEl = svg.querySelector('image[*|href]') || svg.getElementsByTagNameNS(svgNS, 'image')[0] || svg.getElementsByTagNameNS('*', 'image')[0];
      if (!imageEl) continue;
      const href = imageEl.getAttributeNS(xlinkNS, 'href') || imageEl.getAttribute('xlink:href') || imageEl.getAttribute('href');
      if (!href) continue;
      const width = imageEl.getAttribute('width') || svg.getAttribute('width');
      const height = imageEl.getAttribute('height') || svg.getAttribute('height');
      const img = doc.createElementNS('http://www.w3.org/1999/xhtml', 'img');
      img.setAttribute('src', href);
      img.setAttribute('alt', '');
      img.setAttribute('style', 'max-width:100%;height:auto');
      if (width) img.setAttribute('width', width);
      if (height) img.setAttribute('height', height);
      svg.parentNode.replaceChild(img, svg);
      fixedCount++;
    }

    if (fixedCount === 0) return { c: content, fixed: false, count: 0 };
    return { c: safeSerialize(doc, content), fixed: true, count: fixedCount };

  } catch (e) {
    // Fallback to regex
    let fixedCount = 0;
    const svgImageRegex = /<(?:svg:)?svg\b[^>]*>[\s\S]*?<(?:svg:)?image\b[^>]*xlink:href=["']([^"']+)["'][^>]*\/?>\s*<\/(?:svg:)?svg>/gi;
    const newContent = content.replace(svgImageRegex, (match, href) => { fixedCount++; return `<img style="max-width:100%;height:auto" src="${href}" alt="" />`; });
    return { c: newContent, fixed: fixedCount > 0, count: fixedCount };
  }
}

// Ensure cover meta tag exists in OPF — DOMParser with regex fallback
function ensureCoverMeta(opfString) {
  try {
    const parser = new DOMParser();
    const doc = parser.parseFromString(opfString, 'application/xml');
    if (doc.querySelector('parsererror')) throw new Error('Parse failed');

    // Find cover image id: properties="cover-image", or id/href containing "cover"
    let coverId = null;
    const items = [...doc.getElementsByTagNameNS('*', 'item')];
    for (const item of items) {
      const props = item.getAttribute('properties') || '';
      const id = item.getAttribute('id') || '';
      const type = item.getAttribute('media-type') || '';
      if (!type.startsWith('image/')) continue;
      if (props.includes('cover-image')) { coverId = id; break; }
    }
    if (!coverId) {
      for (const item of items) {
        const id = item.getAttribute('id') || '';
        const href = item.getAttribute('href') || '';
        const type = item.getAttribute('media-type') || '';
        if (!type.startsWith('image/')) continue;
        if (id.toLowerCase().includes('cover') || href.toLowerCase().includes('cover')) { coverId = id; break; }
      }
    }
    if (!coverId) return { o: opfString, fixed: false };

    // Find or create <meta name="cover" content="..."/>
    const metas = [...doc.getElementsByTagNameNS('*', 'meta')];
    const coverMeta = metas.find(m => m.getAttribute('name') === 'cover');
    if (coverMeta) {
      if (coverMeta.getAttribute('content') === coverId) return { o: opfString, fixed: false };
      coverMeta.setAttribute('content', coverId);
    } else {
      const metadata = doc.getElementsByTagNameNS('*', 'metadata')[0];
      if (!metadata) return { o: opfString, fixed: false };
      const ns = metadata.namespaceURI || 'http://www.idpf.org/2007/opf';
      const newMeta = doc.createElementNS(ns, 'meta');
      newMeta.setAttribute('name', 'cover');
      newMeta.setAttribute('content', coverId);
      metadata.appendChild(newMeta);
    }
    return { o: safeSerialize(doc, opfString), fixed: true };
  } catch (e) {
    // Regex fallback
    return ensureCoverMetaRegex(opfString);
  }
}

function ensureCoverMetaRegex(o) {
  let coverId = null, m;
  if (!coverId && (m = o.match(/<\w+:?item[^>]+id="([^"]+)"[^>]+properties="[^"]*cover-image[^"]*"/i))) coverId = m[1];
  if (!coverId && (m = o.match(/<\w+:?item[^>]+properties="[^"]*cover-image[^"]*"[^>]+id="([^"]+)"/i))) coverId = m[1];
  if (!coverId && (m = o.match(/<\w+:?item[^>]*id="([^"]+)"[^>]*href="[^"]*cover[^"]*"[^>]*media-type="image\//i))) coverId = m[1];
  if (!coverId && (m = o.match(/<\w+:?item[^>]*href="[^"]*cover[^"]*"[^>]*id="([^"]+)"[^>]*media-type="image\//i))) coverId = m[1];
  if (!coverId && (m = o.match(/<\w+:?item[^>]*id="([^"]*cover[^"]*)"[^>]*media-type="image\//i))) coverId = m[1];
  if (!coverId && (m = o.match(/<\w+:?item[^>]*media-type="image\/[^"]*"[^>]*id="([^"]*cover[^"]*)"/i))) coverId = m[1];
  if (!coverId) return { o, fixed: false };
  const metaMatch = o.match(/<\w+:?meta\s+name=["']cover["']\s+content=["']([^"']+)["']/i) || o.match(/<\w+:?meta\s+content=["']([^"']+)["']\s+name=["']cover["']/i);
  if (metaMatch) {
    if (metaMatch[1] === coverId && !metaMatch[1].includes('/')) return { o, fixed: false };
    const esc = xmlEscape(coverId);
    o = o.replace(/<\w+:?meta\s+name=["']cover["']\s+content=["'][^"']+["']\s*\/?>/gi, `<meta name="cover" content="${esc}" />`);
    o = o.replace(/<\w+:?meta\s+content=["'][^"']+["']\s+name=["']cover["']\s*\/?>/gi, `<meta name="cover" content="${esc}" />`);
    return { o, fixed: true };
  }
  const idx = o.indexOf('</metadata>');
  if (idx !== -1) return { o: o.substring(0, idx) + `    <meta name="cover" content="${xmlEscape(coverId)}"/>\n  </metadata>` + o.substring(idx + 11), fixed: true };
  return { o, fixed: false };
}

