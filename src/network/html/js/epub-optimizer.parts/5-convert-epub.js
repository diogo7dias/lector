// Convert EPUB file - returns converted blob
async function convertEpubFile(file, progressCallback) {
  const startTime = Date.now();
  const originalSize = file.size;

  // Initialize logging
  clearLog();
  showLog();
  log(`<strong>${file.name}</strong> <span class="log-detail">(${formatBytes(originalSize)})</span>`, '', 'INFO');
  log(`Quality: ${JPEG_QUALITY}% | Overlap: ${OVERLAP_PERCENT}% | Rotation: ${HANDEDNESS === 'right' ? 'CW' : 'CCW'} | Grayscale: ${ENABLE_GRAYSCALE ? 'ON' : 'OFF'} | Auto-crop: ${ENABLE_AUTO_CROP ? 'ON' : 'OFF'}`, '', 'INFO');

  const zip = await JSZip.loadAsync(file);
  const renamed = {};
  zip.forEach(p => {
    const l = p.toLowerCase();
    if (l.match(/\.(png|gif|webp|bmp|jpeg)$/)) {
      renamed[p] = p.replace(/\.(png|gif|webp|bmp|jpeg)$/i, '.jpg');
    }
  });

  const out = new JSZip();
  const entries = Object.entries(zip.files);
  const splitImages = {};
  const xhtmlFiles = {};
  let opfPath = null, opfContent = null;
  let mainIdentifier = null;

  // Write mimetype FIRST per EPUB OCF spec
  if (zip.files['mimetype']) {
    const mimetypeData = await zip.files['mimetype'].async('arraybuffer');
    out.file('mimetype', mimetypeData, { compression: 'STORE', createFolders: false });
  }

  // First pass: process images
  for (let i = 0; i < entries.length; i++) {
    if (operationCancelled) throw new Error('Cancelled by user');
    const [path, fileObj] = entries[i];
    if (fileObj.dir || path === 'mimetype') continue;
    const low = path.toLowerCase();

    if (low.match(/\.(png|gif|webp|bmp|jpg|jpeg)$/)) {
      const data = await fileObj.async('arraybuffer');
      const imageState = getImageState(path);

      let result;
      try {
        result = await processImage(data, imageState, path);
      } catch (imageError) {
        // Log error but continue with original image
        console.error(`Failed to process image ${path}:`, imageError);
        log(`Warning: Failed to process ${path.split('/').pop()}, using original`, 'warning', 'IMG-ERR');

        // Use original image data as fallback
        result = {
          parts: [{
            data: data,
            suffix: '',
            width: 0,
            height: 0,
            size: data.byteLength
          }],
          meta: {
            origW: 0,
            origH: 0,
            origSize: data.byteLength,
            wasSplit: false,
            rotated: false,
            finalW: 0,
            finalH: 0,
            finalSize: data.byteLength,
            imageState: imageState,
            processingError: true
          }
        };
      }

      const parts = result.parts;
      const meta = result.meta;

      const baseName = path.replace(/\.[^.]+$/, '');
      const newExt = '.jpg';

      // Log image processing
      const imgName = path.split('/').pop();
      const origFormat = path.split('.').pop();
      logImage(imgName, meta.origW, meta.origH, origFormat, meta.origSize, meta.finalW, meta.finalH, meta.finalSize, meta.wasSplit, meta.splitCount || 0, parts, meta.imageState || 0);

      if (parts.length === 1 && parts[0].suffix === '') {
        const newPath = renamed[path] || path.replace(/\.[^.]+$/, newExt);
        out.file(newPath, parts[0].data, { compression: 'STORE', createFolders: false });
      } else {
        // Store with full path for collision prevention, but also keep original filename
        const origName = path.split('/').pop();
        const origDir = path.includes('/') ? path.substring(0, path.lastIndexOf('/')) : '';

        // Key by full path to avoid collisions
        splitImages[path] = {
          origName: origName,
          origDir: origDir,
          parts: []
        };

        for (const part of parts) {
          const partName = baseName.split('/').pop() + part.suffix + newExt;
          const partPath = (path.includes('/') ? path.substring(0, path.lastIndexOf('/') + 1) : '') + partName;
          out.file(partPath, part.data, { compression: 'STORE', createFolders: false });
          // Store metadata for XHTML/OPF updates
          splitImages[path].parts.push({
            path: partPath,
            imgName: partName,
            id: baseName.split('/').pop() + part.suffix,
            suffix: part.suffix
          });
        }
      }
    } else if (low.match(/\.(xhtml|html|htm)$/)) {
      xhtmlFiles[path] = await safeReadText(fileObj);
    } else if (low.endsWith('.opf')) {
      opfPath = path;
      opfContent = await safeReadText(fileObj);
    }

    if (progressCallback) progressCallback((i / entries.length) * 60);
  }

  // Embedded fonts are dropped entirely — CrossPoint never renders with them.
  // Counters cover font files, @font-face rules (incl. data: URI fonts), and
  // encryption.xml shrinkage; they count uncompressed content bytes removed
  // (the final archive-size delta is reported separately by logSummary).
  const fontPaths = collectFontPaths(zip, opfContent, opfPath);
  let removedFontCount = 0;
  let removedFontBytes = 0;
  let removedFontFaceRules = 0;

  // Second pass: update XHTML using DOMParser
  for (const [xhtmlPath, content] of Object.entries(xhtmlFiles)) {
    if (operationCancelled) throw new Error('Cancelled by user');
    let t = content;

    const stripped = stripComments(t);
    if (stripped.count) {
      t = stripped.text;
      logFix(`Comments (${stripped.count}, ${formatBytes(stripped.bytes)})`, xhtmlPath.split('/').pop());
    }

    const r = fixSvgCover(t);
    if (r.fixed) { t = r.c; logFix('SVG cover', xhtmlPath.split('/').pop()); }

    const r2 = fixSvgWrappedImages(t);
    if (r2.fixed) { t = r2.c; logFix(`SVG images (${r2.count})`, xhtmlPath.split('/').pop()); }

    // Use DOMParser for all img modifications: remove width/height and handle split images
    try {
      const parser = new DOMParser();
      const doc = parser.parseFromString(t, 'application/xhtml+xml');
      const parseError = doc.querySelector('parsererror');

      if (!parseError) {
        let modified = false;

        // Remove width/height attributes from ALL img tags (dimensions may have changed)
        // This prevents CrossPoint and other readers from using wrong dimensions
        const allImgElements = doc.querySelectorAll('img');
        for (const img of allImgElements) {
          if (img.hasAttribute('width')) { img.removeAttribute('width'); modified = true; }
          if (img.hasAttribute('height')) { img.removeAttribute('height'); modified = true; }

          const src = img.getAttribute('src');
          if (src) {
            const decodedSrc = decodeHref(src);
            const resolvedSrc = resolvePath(xhtmlPath, decodedSrc);

            const match = Object.entries(renamed).find(([oldPath]) => resolvedSrc === oldPath);

            if (match) {
              const [oldPath, newPath] = match;
              img.setAttribute('src', decodedSrc.replace(oldPath.split('/').pop(), newPath.split('/').pop()));
              modified = true;
            }
          }
        }

        // Handle split images with path collision prevention
        if (Object.keys(splitImages).length > 0) {
          // Get XHTML directory for resolving relative paths
          const xhtmlDir = xhtmlPath.includes('/') ? xhtmlPath.substring(0, xhtmlPath.lastIndexOf('/')) : '';
          const rootFolders = ['ops', 'oebps', 'epub', 'content'];

          for (const [fullPath, splitInfo] of Object.entries(splitImages)) {
            const origName = splitInfo.origName;
            const origDir = splitInfo.origDir;
            const parts = splitInfo.parts;
            const newName = origName.replace(/\.(png|gif|webp|bmp|jpeg)$/i, '.jpg');

            // Extract immediate parent directory for collision prevention
            const splitDirParts = origDir.split('/').filter(p => p);
            const lastDir = splitDirParts.length > 0 ? splitDirParts[splitDirParts.length - 1].toLowerCase() : null;
            const immediateParent = (lastDir && !rootFolders.includes(lastDir)) ? splitDirParts[splitDirParts.length - 1] : null;

            // Get XHTML's parent directory parts for relative path resolution
            const xhtmlDirParts = xhtmlDir.split('/').filter(p => p);

            // Find all img elements
            const allImgs = doc.querySelectorAll('img');
            const matchingImgs = [];

            for (const img of allImgs) {
              const src = img.getAttribute('src') || '';
              const srcParts = src.split('/').filter(p => p && p !== '..' && p !== '.');
              const srcName = srcParts.pop() || '';

              // Check filename match
              if (srcName !== origName && srcName !== newName) continue;

              // Path collision prevention with root folder handling
              if (immediateParent) {
                // Image is in a specific subfolder (not root like OPS/OEBPS)
                if (srcParts.length === 0) {
                  // src has no path - check if XHTML is in same folder as image
                  const xhtmlLastDir = xhtmlDirParts.length > 0 ? xhtmlDirParts[xhtmlDirParts.length - 1] : null;
                  if (xhtmlLastDir !== immediateParent) continue;
                } else {
                  // src has path - verify parent directory matches
                  if (srcParts[srcParts.length - 1] !== immediateParent) continue;
                }
              } else {
                // Image is in root folder (like OEBPS/cover.jpg)
                // Only match if src has NO subfolder path OR points to root folder
                if (srcParts.length > 0) {
                  const srcLastDir = srcParts[srcParts.length - 1].toLowerCase();
                  if (!rootFolders.includes(srcLastDir)) continue;
                }
              }

              matchingImgs.push(img);
            }

            // Process each matching img — Pro's strip+inject approach
            for (const img of matchingImgs) {
              const src = img.getAttribute('src') || '';

              // Part 1: update src in-place, strip original sizing
              img.setAttribute('src', src.replace(origName, parts[0].imgName).replace(newName, parts[0].imgName));

              if (parts.length > 1) {
                // Strip original width/height/class that were sized for the unsplit image
                img.removeAttribute('width');
                img.removeAttribute('height');
                img.removeAttribute('class');
                img.setAttribute('style', 'max-width:100%;height:auto');

                // Neutralize container height constraints that were sized for the original
                let container = img.parentElement;
                const safeContainers = ['div', 'p', 'figure', 'aside', 'section', 'body'];
                while (container && !safeContainers.includes(container.tagName.toLowerCase())) container = container.parentElement;
                const insertTarget = container || img.parentElement;
                // Strip constraining classes/styles from container — they were for the unsplit image
                if (insertTarget && insertTarget.tagName.toLowerCase() !== 'body') {
                  insertTarget.removeAttribute('class');
                  insertTarget.removeAttribute('style');
                }
                const insertParent = insertTarget.parentElement;
                const insertRef = insertTarget.nextSibling;
                const ns = doc.documentElement.namespaceURI || 'http://www.w3.org/1999/xhtml';

                // Insert new minimal wrappers for parts 2+ in reading order
                for (let pi = 1; pi < parts.length; pi++) {
                  const wrapper = doc.createElementNS(ns, 'div');
                  const newImg = doc.createElementNS(ns, 'img');
                  const partSrc = src.replace(origName, parts[pi].imgName).replace(newName, parts[pi].imgName);
                  newImg.setAttribute('src', partSrc);
                  newImg.setAttribute('alt', '');
                  newImg.setAttribute('style', 'max-width:100%;height:auto');
                  wrapper.appendChild(newImg);
                  if (insertRef) insertParent.insertBefore(wrapper, insertRef);
                  else insertParent.appendChild(wrapper);
                }
              }
              modified = true;
            }
          }
        }

        // Only serialize if we made changes
        if (modified) {
          t = safeSerialize(doc, content);
        }
      }
    } catch (e) {
      console.warn('DOMParser error for', xhtmlPath, e.message);
    }

    // Strip @font-face rules from inline <style> blocks — embedded font files
    // are removed, so leftover rules would reference missing resources
    t = t.replace(/(<style\b[^>]*>)([\s\S]*?)(<\/style>)/gi, (m, open, css, close) => {
      const ff = stripFontFaceRules(css);
      if (!ff.count) return m;
      removedFontFaceRules += ff.count;
      removedFontBytes += new TextEncoder().encode(css).length - new TextEncoder().encode(ff.css).length;
      logFix('@font-face', `${ff.count} rule(s) removed from ${escapeHtml(xhtmlPath.split('/').pop())}`);
      return open + ff.css + close;
    });

    // Inject universal image constraint — prevents overflow on e-ink displays
    if (t.includes('</head>')) {
      t = t.replace('</head>', DEFENSIVE_STYLE + '</head>');
    }

    out.file(xhtmlPath, t, { compression: 'DEFLATE', compressionOptions: { level: 8 }, createFolders: false });
  }

  // Extract main identifier from OPF using DOMParser with regex fallback
  if (opfContent) {
    mainIdentifier = extractIdentifier(opfContent);
  }

  // Third pass: update OPF using fixOPF (DOMParser with regex fallback)
  if (opfContent) {
    let t = opfContent;
    for (const [o, n] of Object.entries(renamed)) {
      t = t.split(o.split('/').pop()).join(n.split('/').pop());
    }
    const opfDir = opfPath.includes('/') ? opfPath.substring(0, opfPath.lastIndexOf('/')) : '';
    t = fixOPF(t, opfContent, opfDir, splitImages, fontPaths);
    if (t !== opfContent) logFix('OPF', 'manifest updated');
    out.file(opfPath, t, { compression: 'DEFLATE', compressionOptions: { level: 8 }, createFolders: false });
  }

  // Copy remaining files
  for (const [path, fileObj] of entries) {
    if (operationCancelled) throw new Error('Cancelled by user');
    if (fileObj.dir || path === 'mimetype') continue;
    const low = path.toLowerCase();
    if (low.match(/\.(png|gif|webp|bmp|jpg|jpeg)$/) || low.match(/\.(xhtml|html|htm)$/) || low.endsWith('.opf')) continue;

    if (fontPaths.has(path)) {
      const fontData = await fileObj.async('arraybuffer');
      removedFontCount++;
      removedFontBytes += fontData.byteLength;
      log(`${escapeHtml(path.split('/').pop())} <span class="log-detail">(${formatBytes(fontData.byteLength)})</span>`, 'success', 'REMOVE');
      continue;
    }

    let data = await fileObj.async('arraybuffer');
    if (low.endsWith('.css')) {
      let t = await safeReadText(fileObj);
      for (const [o, n] of Object.entries(renamed)) {
        t = t.split(o.split('/').pop()).join(n.split('/').pop());
      }
      const ff = stripFontFaceRules(t);
      if (ff.count) {
        removedFontFaceRules += ff.count;
        removedFontBytes += new TextEncoder().encode(t).length - new TextEncoder().encode(ff.css).length;
        t = ff.css;
        logFix('@font-face', `${ff.count} rule(s) removed from ${escapeHtml(path.split('/').pop())}`);
      }
      data = new TextEncoder().encode(t);
    } else if (path === 'META-INF/encryption.xml' && fontPaths.size) {
      const t = await safeReadText(fileObj);
      const enc = stripFontEncryptionEntries(t, fontPaths);
      if (enc.dropFile) {
        removedFontBytes += data.byteLength;
        logFix('encryption.xml', 'removed (contained only font obfuscation entries)');
        continue;
      }
      if (enc.modified) {
        const encoded = new TextEncoder().encode(enc.xml);
        removedFontBytes += Math.max(0, data.byteLength - encoded.length);
        data = encoded;
        logFix('encryption.xml', 'font obfuscation entries removed');
      }
    } else if (low.endsWith('.ncx')) {
      let t = await safeReadText(fileObj);
      for (const [o, n] of Object.entries(renamed)) {
        t = t.split(o.split('/').pop()).join(n.split('/').pop());
      }
      const oldT = t;
      t = syncNCXIdentifier(t, mainIdentifier);
      if (t !== oldT) logFix('NCX identifier', 'Synced with OPF');
      data = new TextEncoder().encode(t);
    }
    out.file(path, data, { compression: 'DEFLATE', compressionOptions: { level: 8 }, createFolders: false });
  }

  if (removedFontCount > 0 || removedFontFaceRules > 0) {
    log(`Removed ${removedFontCount} embedded font(s) and ${removedFontFaceRules} @font-face rule(s), ${formatBytes(removedFontBytes)} of font data`, '', 'INFO');
  }

  if (progressCallback) progressCallback(100);

  // Generate final blob
  const newBlob = await out.generateAsync({ type: 'blob', mimeType: 'application/epub+zip' });
  const newSize = newBlob.size;
  const timeElapsed = (Date.now() - startTime) / 1000;

  // Log completion
  log('Conversion complete!', 'success', 'DONE');
  logSummary(originalSize, newSize, timeElapsed);

  // Auto-export only if NOT in batch mode (batch mode exports at the end)
  if (!isBatchMode && exportLogCheckbox && exportLogCheckbox.checked) {
    setTimeout(() => {
      exportLogToFile(null, false); // isBatch = false for single file
    }, 100);
  }

  return newBlob;
}

