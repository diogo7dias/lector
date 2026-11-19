// Apply grayscale to canvas image data
function applyGrayscale(ctx, width, height) {
  if (!ENABLE_GRAYSCALE) return;
  const imageData = ctx.getImageData(0, 0, width, height);
  const data = imageData.data;
  for (let i = 0; i < data.length; i += 4) {
    // Alpha-blend against white background before grayscaling (handles transparent PNGs)
    const a = data[i + 3] / 255;
    const blendedR = data[i] * a + 255 * (1 - a);
    const blendedG = data[i + 1] * a + 255 * (1 - a);
    const blendedB = data[i + 2] * a + 255 * (1 - a);
    const gray = Math.round(blendedR * 0.299 + blendedG * 0.587 + blendedB * 0.114);
    data[i] = gray; data[i + 1] = gray; data[i + 2] = gray; data[i + 3] = 255;
  }
  ctx.putImageData(imageData, 0, 0);
}

function shouldSkipAutoCrop(imagePath, width, height, ignoreToggle = false) {
  if (!ignoreToggle && !ENABLE_AUTO_CROP) return true;
  if (autoCropProtectedPaths.has(imagePath)) return true;
  if (width < MIN_CROP_DIMENSION || height < MIN_CROP_DIMENSION) return true;
  return /(^|\/)(cover|thumbnail|thumb|icon)[^/]*\.(jpe?g|png|gif|webp|bmp)$/i.test(imagePath || '');
}

function blendedPixel(data, i) {
  const alpha = data[i + 3] / 255;
  return {
    r: data[i] * alpha + 255 * (1 - alpha),
    g: data[i + 1] * alpha + 255 * (1 - alpha),
    b: data[i + 2] * alpha + 255 * (1 - alpha)
  };
}

function estimateCropBackground(data, width, height) {
  const sampleSize = Math.min(CROP_EDGE_SAMPLE_SIZE, Math.floor(width / 8), Math.floor(height / 8));
  if (sampleSize < 2) return null;

  // Use corners plus edge midpoints so colored margins work without trimming full-bleed art.
  const samples = [];
  const points = [
    [0, 0],
    [width - sampleSize, 0],
    [0, height - sampleSize],
    [width - sampleSize, height - sampleSize],
    [Math.floor((width - sampleSize) / 2), 0],
    [Math.floor((width - sampleSize) / 2), height - sampleSize],
    [0, Math.floor((height - sampleSize) / 2)],
    [width - sampleSize, Math.floor((height - sampleSize) / 2)]
  ];

  for (const [startX, startY] of points) {
    let r = 0, g = 0, b = 0, count = 0;
    for (let y = startY; y < startY + sampleSize; y++) {
      const row = y * width * 4;
      for (let x = startX; x < startX + sampleSize; x++) {
        const px = blendedPixel(data, row + x * 4);
        r += px.r; g += px.g; b += px.b; count++;
      }
    }
    samples.push({ r: r / count, g: g / count, b: b / count });
  }

  const avg = samples.reduce((sum, px) => ({
    r: sum.r + px.r,
    g: sum.g + px.g,
    b: sum.b + px.b
  }), { r: 0, g: 0, b: 0 });
  avg.r /= samples.length;
  avg.g /= samples.length;
  avg.b /= samples.length;

  const maxSpread = samples.reduce((spread, px) => Math.max(
    spread,
    Math.abs(px.r - avg.r),
    Math.abs(px.g - avg.g),
    Math.abs(px.b - avg.b)
  ), 0);

  if (maxSpread > CROP_BACKGROUND_MAX_SPREAD) return null;
  return avg;
}

function isCropContentPixel(px, background) {
  if (background) {
    return Math.abs(px.r - background.r) > CROP_BACKGROUND_TOLERANCE ||
           Math.abs(px.g - background.g) > CROP_BACKGROUND_TOLERANCE ||
           Math.abs(px.b - background.b) > CROP_BACKGROUND_TOLERANCE;
  }
  return px.r < CROP_WHITE_THRESHOLD || px.g < CROP_WHITE_THRESHOLD || px.b < CROP_WHITE_THRESHOLD;
}

function isNearWhiteBackground(background) {
  return background &&
         background.r >= CROP_WHITE_THRESHOLD &&
         background.g >= CROP_WHITE_THRESHOLD &&
         background.b >= CROP_WHITE_THRESHOLD;
}

function findNonWhiteBounds(ctx, width, height) {
  const imageData = ctx.getImageData(0, 0, width, height);
  const data = imageData.data;
  const background = estimateCropBackground(data, width, height);
  let left = width, top = height, right = -1, bottom = -1;

  for (let y = 0; y < height; y++) {
    const row = y * width * 4;
    for (let x = 0; x < width; x++) {
      const i = row + x * 4;
      const px = blendedPixel(data, i);

      if (isCropContentPixel(px, background)) {
        if (x < left) left = x;
        if (x > right) right = x;
        if (y < top) top = y;
        if (y > bottom) bottom = y;
      }
    }
  }

  if (right < left || bottom < top) return null;

  left = Math.max(0, left - CROP_PADDING_PX);
  top = Math.max(0, top - CROP_PADDING_PX);
  right = Math.min(width - 1, right + CROP_PADDING_PX);
  bottom = Math.min(height - 1, bottom + CROP_PADDING_PX);

  const cropW = right - left + 1;
  const cropH = bottom - top + 1;
  const savedRatio = 1 - ((cropW * cropH) / (width * height));
  // Colored-background crops need a bigger win to avoid shaving illustrated pages.
  const minSavedRatio = background && !isNearWhiteBackground(background)
    ? MIN_COLOR_CROP_SAVINGS_RATIO
    : MIN_CROP_SAVINGS_RATIO;
  if (savedRatio < minSavedRatio) return null;

  return { x: left, y: top, width: cropW, height: cropH };
}

function createAutoCroppedCanvas(img, imagePath, width, height, ignoreToggle = false) {
  // Build a white-backed source canvas, detect the uniform page/background color
  // from its edges, crop to pixels that differ from that background, then let the
  // normal resize/rotate/split pipeline fit that cropped content to the device.
  // If the image is a cover/icon/small asset, the edges are not uniform, or the
  // crop would save too little, return the original canvas unchanged.
  const sourceCanvas = document.createElement('canvas');
  sourceCanvas.width = width;
  sourceCanvas.height = height;
  const sourceCtx = sourceCanvas.getContext('2d');
  sourceCtx.fillStyle = '#FFF';
  sourceCtx.fillRect(0, 0, width, height);
  sourceCtx.drawImage(img, 0, 0);

  if (shouldSkipAutoCrop(imagePath, width, height, ignoreToggle)) {
    return { canvas: sourceCanvas, width, height };
  }

  const crop = findNonWhiteBounds(sourceCtx, width, height);
  if (!crop) {
    return { canvas: sourceCanvas, width, height };
  }

  const croppedCanvas = document.createElement('canvas');
  croppedCanvas.width = crop.width;
  croppedCanvas.height = crop.height;
  const croppedCtx = croppedCanvas.getContext('2d');
  croppedCtx.fillStyle = '#FFF';
  croppedCtx.fillRect(0, 0, crop.width, crop.height);
  croppedCtx.drawImage(sourceCanvas, crop.x, crop.y, crop.width, crop.height, 0, 0, crop.width, crop.height);

  return { canvas: croppedCanvas, width: crop.width, height: crop.height, crop };
}

