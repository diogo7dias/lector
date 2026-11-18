// EPUB optimizer for the File Manager: repairs EPUB metadata and re-encodes,
// splits, crops and rotates images. FilesPage.html loads it on demand, after
// JSZip; it reads that page's top-level conversion settings (JPEG_QUALITY,
// MAX_WIDTH, HANDEDNESS, imageStates, CROP_*, ...) and log helpers, which stay
// in the page, so nothing declared here may be declared there too.

// ═══════════════════════════════════════════════════════════════════
// EPUB Utilities — ported from EPUB Optimizer Pro
// ═══════════════════════════════════════════════════════════════════

/** Defensive CSS injected into every XHTML <head> — prevents e-ink overflow. */
const DEFENSIVE_STYLE = '<style type="text/css">img,svg{max-width:100%;height:auto}body{overflow-wrap:break-word}table{max-width:100%;table-layout:fixed}pre,code{white-space:pre-wrap;word-wrap:break-word}*{box-sizing:border-box}</style>';

/** Escape a string for safe insertion into XML attribute values / text content. */
function xmlEscape(str) {
  return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

function escapeRegex(str) { return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'); }

/**
 * Decode a URI-encoded href (e.g., "my%20image.jpg" → "my image.jpg").
 * Handles double-encoding gracefully.
 */
function decodeHref(href) {
  try { return decodeURIComponent(href); }
  catch (e) { return href; }
}

/**
 * Safely read a text file from the zip, handling BOM and encoding.
 * Strips UTF-8 BOM. Detects encoding from XML declaration or meta tag.
 */
async function safeReadText(fileObj) {
  const raw = await fileObj.async('uint8array');

  // Detect and strip UTF-8 BOM (EF BB BF)
  let offset = 0;
  if (raw.length >= 3 && raw[0] === 0xEF && raw[1] === 0xBB && raw[2] === 0xBF) {
    offset = 3;
  }

  // Try UTF-8 first (vast majority of EPUBs)
  const utf8 = new TextDecoder('utf-8', { fatal: true });
  try {
    return utf8.decode(raw.subarray(offset));
  } catch (e) { /* not valid UTF-8 */ }

  // Peek at XML declaration or meta charset for encoding hint
  const ascii = new TextDecoder('ascii', { fatal: false }).decode(raw.subarray(offset, offset + 512));
  const encodingMatch = ascii.match(/encoding=["']([^"']+)["']/i) ||
                        ascii.match(/charset=["']?([^"'\s;]+)/i);
  const encoding = encodingMatch ? encodingMatch[1].toLowerCase() : 'windows-1252';

  try {
    return new TextDecoder(encoding, { fatal: false }).decode(raw.subarray(offset));
  } catch (e) {
    // Last resort: lossy latin1
    return new TextDecoder('iso-8859-1', { fatal: false }).decode(raw.subarray(offset));
  }
}

/**
 * Find the canonical OPF path by parsing META-INF/container.xml.
 * Falls back to scanning for any .opf file.
 */
async function findOPFPath(zip) {
  try {
    const containerPath = Object.keys(zip.files).find(p => p.toLowerCase() === 'meta-inf/container.xml');
    if (containerPath) {
      const containerXml = await zip.files[containerPath].async('string');
      const match = containerXml.match(/<rootfile[^>]+full-path=["']([^"']+)["']/i);
      if (match && zip.files[match[1]]) return match[1];
    }
  } catch (e) { /* fall through */ }
  let fallback = null;
  zip.forEach(p => { if (!fallback && p.toLowerCase().endsWith('.opf')) fallback = p; });
  return fallback;
}

function sanitizeMetadataFilenamePart(value) {
  const text = String(value || '').replace(/\s+/g, ' ').trim();
  return (text.normalize ? text.normalize('NFC') : text)
    .replace(/[<>:"/\\|?*\x00-\x1F]/g, ' ')
    .replace(/\s+/g, ' ')
    .replace(/^[. ]+/g, '')
    .replace(/[. ]+$/g, '')
    .trim();
}

function buildMetadataFilename(title, author) {
  title = sanitizeMetadataFilenamePart(title);
  author = sanitizeMetadataFilenamePart(author);
  if (!title) return '';
  let base = author ? `${title} - ${author}` : title;
  if (base.length > 180) base = base.substring(0, 180).replace(/\s+\S*$/g, '').trim() || base.substring(0, 180).trim();
  return `${base}.epub`;
}

async function getMetadataFilenameForEpub(file) {
  if (typeof JSZip === 'undefined') return '';
  const zip = await JSZip.loadAsync(file);
  const opfPath = await findOPFPath(zip);
  if (!opfPath || !zip.files[opfPath]) return '';

  const doc = new DOMParser().parseFromString(await safeReadText(zip.files[opfPath]), 'application/xml');
  if (doc.querySelector('parsererror')) return '';

  const title = doc.getElementsByTagNameNS('*', 'title')[0]?.textContent || '';
  const creators = Array.from(doc.getElementsByTagNameNS('*', 'creator'));
  const getCreatorRole = el => (
    el.getAttribute('role') ||
    el.getAttribute('opf:role') ||
    el.getAttributeNS('http://www.idpf.org/2007/opf', 'role') ||
    ''
  ).toLowerCase();
  const authorEl = creators.find(el => getCreatorRole(el) === 'aut') ||
                   creators.find(el => !getCreatorRole(el));
  const authorText = authorEl?.textContent?.trim();
  const author = authorText ||
                 authorEl?.getAttribute('file-as') ||
                 authorEl?.getAttribute('opf:file-as') ||
                 authorEl?.getAttributeNS('http://www.idpf.org/2007/opf', 'file-as') ||
                 '';
  return buildMetadataFilename(title, author);
}

/**
 * Resolve a relative href against a base file path.
 * Handles multiple ../, ./, absolute /, and bare relative paths.
 */
function resolvePath(basePath, href) {
  if (href.startsWith('/')) return href.substring(1);
  href = href.replace(/^\.\//, '');
  const baseDir = basePath.includes('/') ? basePath.substring(0, basePath.lastIndexOf('/')) : '';
  const baseParts = baseDir ? baseDir.split('/') : [];
  const hrefParts = href.split('/');
  while (hrefParts.length > 0 && hrefParts[0] === '..') {
    hrefParts.shift();
    if (baseParts.length > 0) baseParts.pop();
  }
  const resolved = [...baseParts, ...hrefParts].join('/');
  return resolved.replace(/\/+/g, '/');
}

/** Remove XML comments from XHTML source text. Returns {text, count, bytes}. */
function stripComments(xml) {
  const SKIPPED = [
    { open: '<![CDATA[', close: ']]>' },
    { open: '<?', close: '?>' },
  ];

  const encoder = new TextEncoder();
  const out = [];
  let i = 0;
  let count = 0;
  let bytes = 0;

  const doctypeEnd = (from) => {
    const gt = xml.indexOf('>', from);
    const subset = xml.indexOf('[', from);
    if (subset < 0 || (gt >= 0 && subset > gt)) return gt < 0 ? xml.length : gt + 1;
    const close = xml.indexOf(']>', subset);
    return close < 0 ? xml.length : close + 2;
  };

  while (i < xml.length) {
    const next = xml.indexOf('<', i);
    if (next < 0) { out.push(xml.slice(i)); break; }
    if (next > i) out.push(xml.slice(i, next));

    if (xml.startsWith('<!--', next)) {
      const close = xml.indexOf('-->', next + 4);
      const end = close < 0 ? xml.length : close + 3;
      count++;
      bytes += encoder.encode(xml.slice(next, end)).length;
      i = end;
      continue;
    }

    if (xml.startsWith('<!DOCTYPE', next)) {
      const end = doctypeEnd(next + 9);
      out.push(xml.slice(next, end));
      i = end;
      continue;
    }

    const skipped = SKIPPED.find(s => xml.startsWith(s.open, next));
    if (skipped) {
      const close = xml.indexOf(skipped.close, next + skipped.open.length);
      const end = close < 0 ? xml.length : close + skipped.close.length;
      out.push(xml.slice(next, end));
      i = end;
      continue;
    }

    out.push('<');
    i = next + 1;
  }

  return { text: out.join(''), count, bytes };
}

/**
 * Serialize an XML doc back to string, preserving the original <?xml?> declaration
 * and cleaning up XMLSerializer namespace prefix noise (xmlns:ns0 etc).
 */
function safeSerialize(doc, originalContent) {
  let result = new XMLSerializer().serializeToString(doc);

  // Restore <?xml?> declaration if original had one
  if (originalContent && /^\s*<\?xml\b/.test(originalContent) && !/^\s*<\?xml\b/.test(result)) {
    const declMatch = originalContent.match(/^\s*(<\?xml[^?]*\?>)/);
    if (declMatch) result = declMatch[1] + '\n' + result;
  }

  // Clean up XMLSerializer namespace prefix noise (xmlns:ns0="..." ns0:attr="...")
  result = result.replace(/ xmlns:ns\d+="[^"]*"/g, '');
  result = result.replace(/ ns\d+:/g, ' ');

  return result;
}

/**
 * Extract main identifier from OPF for NCX sync. DOMParser with regex fallback.
 */
function extractIdentifier(opfContent) {
  let mainIdentifier = null;
  try {
    const doc = new DOMParser().parseFromString(opfContent, 'application/xml');
    if (!doc.querySelector('parsererror')) {
      const pkg = doc.getElementsByTagNameNS('*', 'package')[0];
      const uid = pkg ? pkg.getAttribute('unique-identifier') : null;
      if (uid) {
        const el = [...doc.getElementsByTagNameNS('*', 'identifier')].find(e => e.getAttribute('id') === uid);
        if (el) mainIdentifier = (el.textContent || '').trim();
      }
      if (!mainIdentifier) {
        const el = doc.getElementsByTagNameNS('*', 'identifier')[0];
        if (el) mainIdentifier = (el.textContent || '').trim();
      }
    }
  } catch (e) { /* fall through to regex */ }
  if (!mainIdentifier) {
    const uniqueIdMatch = opfContent.match(/<(?:\w+:)?package[^>]*unique-identifier=["']([^"']+)["']/i);
    if (uniqueIdMatch) {
      const idRegex = new RegExp(`<dc:identifier[^>]*id=["']${uniqueIdMatch[1]}["'][^>]*>([^<]+)</dc:identifier>`, 'i');
      const idMatch = opfContent.match(idRegex);
      if (idMatch) mainIdentifier = idMatch[1].trim();
    }
    if (!mainIdentifier) {
      const firstIdMatch = opfContent.match(/<dc:identifier[^>]*>([^<]+)</i);
      if (firstIdMatch) mainIdentifier = firstIdMatch[1].trim();
    }
  }
  return mainIdentifier;
}

/**
 * Sync NCX dtb:uid with the given identifier. DOMParser with regex fallback.
 */
function syncNCXIdentifier(ncxText, mainIdentifier) {
  if (!mainIdentifier) return ncxText;
  let t = ncxText;
  try {
    const doc = new DOMParser().parseFromString(t, 'application/xml');
    if (!doc.querySelector('parsererror')) {
      const meta = [...doc.getElementsByTagNameNS('*', 'meta')].find(m => m.getAttribute('name') === 'dtb:uid');
      if (meta) {
        meta.setAttribute('content', mainIdentifier);
        t = safeSerialize(doc, ncxText);
      }
    }
  } catch (e) {
    t = t.replace(/<meta\s+name=["']dtb:uid["']\s+content=["'][^"']*["']\s*\/?>/gi, `<meta name="dtb:uid" content="${xmlEscape(mainIdentifier)}"/>`);
  }
  return t;
}

