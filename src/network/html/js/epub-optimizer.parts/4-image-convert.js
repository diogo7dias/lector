// Process single image - returns array of {data, suffix} objects
const IMAGE_LOAD_TIMEOUT_MS = 30000; // 30 second timeout for image loading
async function processImage(data, imageState = 0, imagePath = '') {
  return new Promise((resolve, reject) => {
    const url = URL.createObjectURL(new Blob([data]));
    const img = new Image();
    const origSize = data.byteLength;
    // Set up timeout to handle cases where image never loads
    const timeoutId = setTimeout(() => {
      URL.revokeObjectURL(url);
      reject(new Error('Image load timeout'));
    }, IMAGE_LOAD_TIMEOUT_MS);

    img.onload = async () => {
      clearTimeout(timeoutId);
      URL.revokeObjectURL(url);
      const origW = img.width, origH = img.height;
      const source = createAutoCroppedCanvas(img, imagePath, origW, origH);
      const sourceCanvas = source.canvas;
      const sourceW = source.width;
      const sourceH = source.height;
      const sourceWasCropped = !!source.crop;

      // imageState: 0=Normal, 1=H-Split (CW/CCW), 2=V-Split, 3=Rotate & Fit
      // ========================================================================
      // STATE 1: H-Split (Rotate + Split) - EXACT COPY FROM index.html
      // Step 1: Scale WIDTH to 800px (keep aspect ratio)
      // Step 2: Rotate 90° CW or CCW based on HANDEDNESS
      // Step 3: If WIDTH > 480, split vertically with overlap
      // ========================================================================
      if (imageState === 1) {
        // Step 1: Scale WIDTH to 800 (this is the key difference!)
        const scale = MAX_HEIGHT / sourceW;  // 800 / sourceW
        const scaledW = MAX_HEIGHT;  // 800
        const scaledH = Math.round(sourceH * scale);

        const scaledCanvas = document.createElement('canvas');
        scaledCanvas.width = scaledW;
        scaledCanvas.height = scaledH;
        const scaledCtx = scaledCanvas.getContext('2d');
        scaledCtx.imageSmoothingEnabled = true;
        scaledCtx.imageSmoothingQuality = 'high';
        scaledCtx.fillStyle = '#FFF';
        scaledCtx.fillRect(0, 0, scaledW, scaledH);
        scaledCtx.drawImage(sourceCanvas, 0, 0, sourceW, sourceH, 0, 0, scaledW, scaledH);

        // Step 2: Rotate 90° CW or CCW
        const rotW = scaledH;
        const rotH = scaledW;  // 800

        const rotCanvas = document.createElement('canvas');
        rotCanvas.width = rotW;
        rotCanvas.height = rotH;
        const rotCtx = rotCanvas.getContext('2d');
        rotCtx.fillStyle = '#FFF';
        rotCtx.fillRect(0, 0, rotW, rotH);

        const isClockwise = HANDEDNESS === 'right';
        if (isClockwise) {
          // Rotate 90° CW
          rotCtx.translate(rotW, 0);
          rotCtx.rotate(Math.PI / 2);
        } else {
          // Rotate 90° CCW
          rotCtx.translate(0, rotH);
          rotCtx.rotate(-Math.PI / 2);
        }
        rotCtx.drawImage(scaledCanvas, 0, 0);
        rotCtx.setTransform(1, 0, 0, 1, 0, 0); // Reset transform
        applyGrayscale(rotCtx, rotW, rotH);

        // Step 3: If WIDTH > 480, split vertically
        if (rotW <= MAX_WIDTH) {
          const blob = await new Promise(res => rotCanvas.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
          const arrBuf = await blob.arrayBuffer();
          resolve({
            parts: [{ data: arrBuf, suffix: '', width: rotW, height: rotH, size: arrBuf.byteLength }],
            meta: { origW, origH, origSize, wasSplit: false, rotated: true, finalW: rotW, finalH: rotH, finalSize: arrBuf.byteLength, imageState: 1 }
          });
        } else {
          // Split by WIDTH (vertical cuts) - from RIGHT to LEFT for CW, LEFT to RIGHT for CCW
          const parts = [];
          const maxW = MAX_WIDTH;  // 480
          
          // Centered distribution: calculate numParts first, then distribute evenly
          let overlapPx, step, numParts;
          const minOverlapPx = Math.round(maxW * (OVERLAP_PERCENT / 100));  // Configurable overlap
          const maxStep = maxW - minOverlapPx;
          numParts = Math.ceil((rotW - minOverlapPx) / maxStep);
          if (numParts < 2) numParts = 2;
          // Now calculate step to distribute evenly
          step = Math.round((rotW - maxW) / (numParts - 1));
          overlapPx = maxW - step;
          // Ensure minimum overlap
          if (overlapPx < minOverlapPx) {
            overlapPx = minOverlapPx;
            step = maxW - overlapPx;
          }

          // Calculate all x positions first to ensure consistency
          const positions = [];
          for (let i = 0; i < numParts; i++) {
            let x;
            if (isClockwise) {
              // CW: right to left - start from right edge
              x = rotW - maxW - (i * step);
            } else {
              // CCW: left to right - start from left edge
              x = i * step;
            }
            // Clamp to valid range
            x = Math.max(0, Math.min(x, rotW - maxW));
            positions.push(x);
          }

          // Ensure first and last positions are at edges
          if (isClockwise) {
            positions[0] = rotW - maxW; // First part at right edge
            positions[numParts - 1] = 0; // Last part at left edge
          } else {
            positions[0] = 0; // First part at left edge
            positions[numParts - 1] = rotW - maxW; // Last part at right edge
          }

          for (let i = 0; i < numParts; i++) {
            const x = positions[i];
            const partW = maxW; // Always full width for consistency

            const partCanvas = document.createElement('canvas');
            partCanvas.width = partW;
            partCanvas.height = rotH;
            const partCtx = partCanvas.getContext('2d');
            // Clear canvas first
            partCtx.fillStyle = '#FFFFFF';
            partCtx.fillRect(0, 0, partW, rotH);
            // Draw the slice
            partCtx.drawImage(rotCanvas, x, 0, partW, rotH, 0, 0, partW, rotH);

            const blob = await new Promise(res => partCanvas.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
            const arrBuf = await blob.arrayBuffer();
            parts.push({ data: arrBuf, suffix: `_part${i + 1}`, width: partW, height: rotH, size: arrBuf.byteLength });
          }

          const totalSize = parts.reduce((sum, p) => sum + p.size, 0);
          resolve({
            parts,
            meta: { origW, origH, origSize, wasSplit: true, splitCount: numParts, rotated: true, finalW: parts[0].width, finalH: parts[0].height, finalSize: totalSize, imageState: 1 }
          });
        }
      }
      // ========================================================================
      // STATE 2: V-Split (Vertical Split, no rotation)
      // Step 1: Scale HEIGHT to 800px (up or down)
      // Step 2: If WIDTH > 480, split vertically with overlap
      // ========================================================================
      else if (imageState === 2) {
        // ALWAYS scale height to 800 (up or down)
        const scale = MAX_HEIGHT / sourceH;  // 800 / sourceH
        const scaledW = Math.round(sourceW * scale);
        const scaledH = MAX_HEIGHT;  // Always 800

        const scaledCanvas = document.createElement('canvas');
        scaledCanvas.width = scaledW;
        scaledCanvas.height = scaledH;
        const scaledCtx = scaledCanvas.getContext('2d');
        scaledCtx.imageSmoothingEnabled = true;
        scaledCtx.imageSmoothingQuality = 'high';
        scaledCtx.fillStyle = '#FFF';
        scaledCtx.fillRect(0, 0, scaledW, scaledH);
        scaledCtx.drawImage(sourceCanvas, 0, 0, sourceW, sourceH, 0, 0, scaledW, scaledH);
        applyGrayscale(scaledCtx, scaledW, scaledH);

        // Check if split needed
        if (scaledW <= MAX_WIDTH) {
          const blob = await new Promise(res => scaledCanvas.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
          const arrBuf = await blob.arrayBuffer();
          resolve({
            parts: [{ data: arrBuf, suffix: '', width: scaledW, height: scaledH, size: arrBuf.byteLength }],
            meta: { origW, origH, origSize, wasSplit: false, rotated: false, finalW: scaledW, finalH: scaledH, finalSize: arrBuf.byteLength, imageState: 2 }
          });
        } else {
          // Split by WIDTH (vertical cuts) - LEFT to RIGHT (natural reading order)
          const parts = [];
          const maxW = MAX_WIDTH;
          
          // Centered distribution: calculate numParts first, then distribute evenly
          let overlapPx, step, numParts;
          const minOverlapPx = Math.round(maxW * (OVERLAP_PERCENT / 100));  // Configurable overlap
          const maxStep = maxW - minOverlapPx;
          numParts = Math.ceil((scaledW - minOverlapPx) / maxStep);
          if (numParts < 2) numParts = 2;
          // Now calculate step to distribute evenly
          step = Math.round((scaledW - maxW) / (numParts - 1));
          overlapPx = maxW - step;
          // Ensure minimum overlap
          if (overlapPx < minOverlapPx) {
            overlapPx = minOverlapPx;
            step = maxW - overlapPx;
          }

          // Calculate all x positions first to ensure consistency
          const positions = [];
          for (let i = 0; i < numParts; i++) {
            let x = i * step;
            // Clamp to valid range
            x = Math.max(0, Math.min(x, scaledW - maxW));
            positions.push(x);
          }
          // Ensure last position is at right edge
          positions[0] = 0;
          positions[numParts - 1] = scaledW - maxW;

          for (let i = 0; i < numParts; i++) {
            const x = positions[i];
            const partW = maxW; // Always full width for consistency

            const partCanvas = document.createElement('canvas');
            partCanvas.width = partW;
            partCanvas.height = scaledH;
            const partCtx = partCanvas.getContext('2d');
            // Clear canvas first
            partCtx.fillStyle = '#FFFFFF';
            partCtx.fillRect(0, 0, partW, scaledH);
            // Draw the slice
            partCtx.drawImage(scaledCanvas, x, 0, partW, scaledH, 0, 0, partW, scaledH);

            const blob = await new Promise(res => partCanvas.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
            const arrBuf = await blob.arrayBuffer();
            parts.push({ data: arrBuf, suffix: `_part${i + 1}`, width: partW, height: scaledH, size: arrBuf.byteLength });
          }

          const totalSize = parts.reduce((sum, p) => sum + p.size, 0);
          resolve({
            parts,
            meta: { origW, origH, origSize, wasSplit: true, splitCount: numParts, rotated: false, finalW: parts[0].width, finalH: parts[0].height, finalSize: totalSize, imageState: 2 }
          });
        }
      }
      // ========================================================================
      // STATE 3: Rotate & Fit (Rotate 90°, then scale to fit 480x800, no split)
      // ========================================================================
      else if (imageState === 3) {
        // Step 1: Rotate 90° based on handedness
        const rotW = sourceH;
        const rotH = sourceW;

        const rotCanvas = document.createElement('canvas');
        rotCanvas.width = rotW;
        rotCanvas.height = rotH;
        const rotCtx = rotCanvas.getContext('2d');
        rotCtx.fillStyle = '#FFF';
        rotCtx.fillRect(0, 0, rotW, rotH);

        const isClockwise = HANDEDNESS === 'right';
        if (isClockwise) {
          rotCtx.translate(rotW, 0);
          rotCtx.rotate(Math.PI / 2);
        } else {
          rotCtx.translate(0, rotH);
          rotCtx.rotate(-Math.PI / 2);
        }
        rotCtx.drawImage(sourceCanvas, 0, 0);
        rotCtx.setTransform(1, 0, 0, 1, 0, 0);

        // Step 2: Scale to fit 480x800 (if needed)
        const fitsInScreen = rotW <= MAX_WIDTH && rotH <= MAX_HEIGHT;
        
        if (fitsInScreen && !sourceWasCropped) {
          // Already fits after rotation - just apply grayscale
          applyGrayscale(rotCtx, rotW, rotH);
          const blob = await new Promise(res => rotCanvas.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
          const arrBuf = await blob.arrayBuffer();
          resolve({
            parts: [{ data: arrBuf, suffix: '', width: rotW, height: rotH, size: arrBuf.byteLength }],
            meta: { origW, origH, origSize, wasSplit: false, rotated: true, finalW: rotW, finalH: rotH, finalSize: arrBuf.byteLength, imageState: 3 }
          });
        } else {
          // Scale to fit 480x800
          const scale = Math.min(MAX_WIDTH / rotW, MAX_HEIGHT / rotH);
          const newW = Math.round(rotW * scale);
          const newH = Math.round(rotH * scale);

          const scaledCanvas = document.createElement('canvas');
          scaledCanvas.width = newW;
          scaledCanvas.height = newH;
          const scaledCtx = scaledCanvas.getContext('2d');
          scaledCtx.imageSmoothingEnabled = true;
          scaledCtx.imageSmoothingQuality = 'high';
          scaledCtx.fillStyle = '#FFF';
          scaledCtx.fillRect(0, 0, newW, newH);
          scaledCtx.drawImage(rotCanvas, 0, 0, newW, newH);
          applyGrayscale(scaledCtx, newW, newH);

          const blob = await new Promise(res => scaledCanvas.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
          const arrBuf = await blob.arrayBuffer();
          resolve({
            parts: [{ data: arrBuf, suffix: '', width: newW, height: newH, size: arrBuf.byteLength }],
            meta: { origW, origH, origSize, wasSplit: false, rotated: true, finalW: newW, finalH: newH, finalSize: arrBuf.byteLength, imageState: 3 }
          });
        }
      }
      // ========================================================================
      // STATE 0: Normal processing (scale to fit, no split/rotation)
      // ========================================================================
      else {
        // Normal processing: check if scaling is needed
        const fitsInScreen = sourceW <= MAX_WIDTH && sourceH <= MAX_HEIGHT;

        if (fitsInScreen && !sourceWasCropped) {
          // Image already fits - just convert to JPEG with grayscale
          const c = document.createElement('canvas');
          c.width = sourceW;
          c.height = sourceH;
          const ctx = c.getContext('2d');
          ctx.fillStyle = '#FFF';
          ctx.fillRect(0, 0, sourceW, sourceH);
          ctx.drawImage(sourceCanvas, 0, 0);
          applyGrayscale(ctx, sourceW, sourceH);

          const blob = await new Promise(res => c.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
          const arrBuf = await blob.arrayBuffer();
          resolve({
            parts: [{ data: arrBuf, suffix: '', width: sourceW, height: sourceH, size: arrBuf.byteLength }],
            meta: { origW, origH, origSize, wasSplit: false, rotated: false, finalW: sourceW, finalH: sourceH, finalSize: arrBuf.byteLength, imageState: 0 }
          });
        } else {
          // Scale to fit 480x800
          const scale = Math.min(MAX_WIDTH / sourceW, MAX_HEIGHT / sourceH);
          const newW = Math.round(sourceW * scale);
          const newH = Math.round(sourceH * scale);

          const c = document.createElement('canvas');
          c.width = newW;
          c.height = newH;
          const ctx = c.getContext('2d');
          ctx.imageSmoothingEnabled = true;
          ctx.imageSmoothingQuality = 'high';
          ctx.fillStyle = '#FFF';
          ctx.fillRect(0, 0, newW, newH);
          ctx.drawImage(sourceCanvas, 0, 0, newW, newH);
          applyGrayscale(ctx, newW, newH);

          const blob = await new Promise(res => c.toBlob(res, 'image/jpeg', JPEG_QUALITY / 100));
          const arrBuf = await blob.arrayBuffer();
          resolve({
            parts: [{ data: arrBuf, suffix: '', width: newW, height: newH, size: arrBuf.byteLength }],
            meta: { origW, origH, origSize, wasSplit: false, rotated: false, finalW: newW, finalH: newH, finalSize: arrBuf.byteLength, imageState: 0 }
          });
        }
      }
    };
    img.onerror = () => {
      clearTimeout(timeoutId);
      URL.revokeObjectURL(url);
      reject(new Error('Image load failed'));
    };
    img.src = url;
  });
}

