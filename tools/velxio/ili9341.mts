// Adapted from Velxio 77ee0cf; AGPL-3.0. See THIRD_PARTY.md.
export const ili9341Simulation = {
  attachEvents: (element, simulator, getArduinoPinHelper) => {
    const el = element as any;
    const pinManager = (simulator as any).pinManager;
    // Generic .spi accessor — every simulator (AVR, RP2040, ESP32 family)
    // exposes a SpiBusLike object via this name (see frontend/src/simulation/
    // SpiBus.ts). Single-listener channel: assign to spi.onByte and
    // chain any prior handler in our cleanup.
    const spi = (simulator as any).spi as
      | { onByte: ((mosi: number) => void) | null;
          completeTransfer?: (miso: number) => void }
      | undefined;

    if (!pinManager || !spi) return () => {};

    // ── Canvas setup ──────────────────────────────────────────────────
    const SCREEN_W = 240;
    const SCREEN_H = 320;

    const initCanvas = (): CanvasRenderingContext2D | null => {
      // el.canvas is the getter defined in ili9341-element.ts:
      //   get canvas() { return this.shadowRoot?.querySelector('canvas'); }
      // The element already sets width=240 height=320 in its LitElement template.
      const canvas = el.canvas as HTMLCanvasElement | null;
      if (!canvas) return null;
      return canvas.getContext('2d');
    };

    let ctx = initCanvas();

    const onCanvasReady = () => {
      ctx = initCanvas();
    };
    el.addEventListener('canvas-ready', onCanvasReady);

    // ── Shared ImageData buffer ───────────────────────────────────────
    // Accumulate pixels here; flush to canvas once per animation frame.
    let imageData: ImageData | null = null;

    const getOrCreateImageData = (): ImageData => {
      if (!ctx) ctx = initCanvas();
      if (!imageData && ctx) imageData = ctx.createImageData(SCREEN_W, SCREEN_H);
      return imageData!;
    };

    // Flush is debounced rather than rAF-pinned: TFT firmwares emit each
    // frame as one long SPI burst that often takes >16 ms to drain
    // (rp2040js is sub-realtime), so painting every rAF would snapshot
    // the canvas mid-burst — the user would see only the pixels that
    // happened to land before that tick. We instead wait for SPI silence
    // (a real frame boundary), bounded by a hard cap so continuous-write
    // sketches still update.
    let pendingFlush = false;
    let idleTimerId: number | null = null;
    let firstWriteSinceFlush = 0;
    const IDLE_FLUSH_MS = 16;
    const MAX_FLUSH_INTERVAL_MS = 100;

    const doFlush = () => {
      if (idleTimerId !== null) {
        clearTimeout(idleTimerId);
        idleTimerId = null;
      }
      if (pendingFlush && ctx && imageData) {
        ctx.putImageData(imageData, 0, 0);
        pendingFlush = false;
        firstWriteSinceFlush = 0;
      }
    };

    const scheduleFlush = () => {
      if (!pendingFlush) return;
      const now = performance.now();
      if (firstWriteSinceFlush === 0) firstWriteSinceFlush = now;
      if (now - firstWriteSinceFlush >= MAX_FLUSH_INTERVAL_MS) {
        doFlush();
        return;
      }
      if (idleTimerId !== null) clearTimeout(idleTimerId);
      idleTimerId = window.setTimeout(doFlush, IDLE_FLUSH_MS);
    };

    // ── ILI9341 state ─────────────────────────────────────────────────
    let colStart = 0,
      colEnd = SCREEN_W - 1;
    let rowStart = 0,
      rowEnd = SCREEN_H - 1;
    let curX = 0,
      curY = 0;

    let currentCmd = -1;
    let inverted = false;
    let dataBytes: number[] = [];
    let inRamWrite = false;
    let pixelHiByte = 0;
    let pixelByteCount = 0;

    // ── MADCTL state ──────────────────────────────────────────────────
    // ILI9341 0x36 command bits we care about (datasheet §8.2.29). Set
    // by setRotation() in every Adafruit-style driver; default is
    // rotation 0 = all bits clear (portrait, no swap, no mirror).
    let madMV = false; // row/column exchange — landscape orientation
    let madMX = false; // column address mirror
    let madMY = false; // row address mirror

    // ── DC pin tracking ───────────────────────────────────────────────
    let dcState = false; // LOW = command, HIGH = data
    const pinDC = getArduinoPinHelper('D/C');

    const unsubscribers: (() => void)[] = [];

    if (pinDC !== null) {
      unsubscribers.push(
        pinManager.onPinChange(pinDC, (_: number, s: boolean) => {
          dcState = s;
        }),
      );
    }

    // ── Pixel writer ──────────────────────────────────────────────────
    // curX / curY / col* / row* are LOGICAL coordinates — the values the
    // driver thinks it's writing to. In rotation 0 logical = physical.
    // In rotation 1/3 (MV set) the driver iterates X in 0..319 and Y in
    // 0..239; we swap them at the last possible moment before touching
    // the imageData buffer (which is always physically 240 wide × 320 tall).
    //
    // The mapping is rotation-specific because applying MX/MY/MV as three
    // independent flags double-mirrors the output (we tried that in
    // commit 6edc715 and the user saw "espejada" text). The four
    // Adafruit_ILI9341 setRotation() values map cleanly to four explicit
    // (curX, curY) → (physX, physY) formulae taken from the chip's
    // datasheet section 8.2.29 (Memory Access Control):
    //
    //   rot 0  M=0x48 (MX|BGR)            : (curX, curY)              [portrait]
    //   rot 1  M=0x28 (MV|BGR)            : (curY, (319 - curX))      [landscape]
    //   rot 2  M=0x88 (MY|BGR)            : ((239 - curX), (319 - curY))  [portrait flipped]
    //   rot 3  M=0xE8 (MX|MY|MV|BGR)      : ((239 - curY), curX)      [landscape flipped]
    //
    // The Adafruit driver computes the rotation register value, sends it
    // once via MADCTL, then writes pixels in the rotated framebuffer's
    // coordinate space — we mirror that on the receive side.
    const writePixel = (hi: number, lo: number) => {
      if (curX > colEnd || curY > rowEnd) return;

      // Map logical → physical via the (MV, MX, MY) rotation signature.
      let physX: number, physY: number;
      if (!madMV) {
        // Portrait (rotations 0 or 2)
        physX = madMY ? (SCREEN_W - 1) - curX : curX;
        physY = madMY ? (SCREEN_H - 1) - curY : curY;
      } else if (!madMX && !madMY) {
        // Landscape rotation 1: m = MV | BGR. (curY, 319 - curX)
        physX = curY;
        physY = (SCREEN_H - 1) - curX;
      } else {
        // Landscape rotation 3: m = MX | MY | MV | BGR. (239 - curY, curX)
        physX = (SCREEN_W - 1) - curY;
        physY = curX;
      }

      if (physX < 0 || physX >= SCREEN_W || physY < 0 || physY >= SCREEN_H) {
        curX++;
        if (curX > colEnd) {
          curX = colStart;
          curY++;
        }
        return;
      }

      const id = getOrCreateImageData();
      const color = ((hi << 8) | lo) ^ (inverted ? 0xffff : 0);
      const r = ((color >> 11) & 0x1f) * 8;
      const g = ((color >> 5) & 0x3f) * 4;
      const b = (color & 0x1f) * 8;

      const idx = (physY * SCREEN_W + physX) * 4;
      id.data[idx] = r;
      id.data[idx + 1] = g;
      id.data[idx + 2] = b;
      id.data[idx + 3] = 255;

      pendingFlush = true;
      curX++;
      if (curX > colEnd) {
        curX = colStart;
        curY++;
      }
    };

    // ── Command / data processing ─────────────────────────────────────
    const processCommand = (cmd: number) => {
      currentCmd = cmd;
      if (cmd === 0x20) inverted = false;
      if (cmd === 0x21) inverted = true;
      dataBytes = [];
      inRamWrite = cmd === 0x2c;
      if (inRamWrite) { curX = colStart; curY = rowStart; }
      pixelByteCount = 0;

      if (cmd === 0x01) {
        // SWRESET – clear framebuffer + reset MADCTL to defaults
        colStart = 0;
        colEnd = SCREEN_W - 1;
        rowStart = 0;
        rowEnd = SCREEN_H - 1;
        curX = 0;
        curY = 0;
        madMV = false;
        madMX = false;
        madMY = false;
        imageData = null;
        if (ctx) ctx.clearRect(0, 0, SCREEN_W, SCREEN_H);
      }
    };

    const processData = (value: number) => {
      if (inRamWrite) {
        // RGB-565: two bytes per pixel
        if (pixelByteCount === 0) {
          pixelHiByte = value;
          pixelByteCount = 1;
        } else {
          writePixel(pixelHiByte, value);
          scheduleFlush();
          pixelByteCount = 0;
        }
        return;
      }

      dataBytes.push(value);
      switch (currentCmd) {
        case 0x2a: // CASET – column address set
          if (dataBytes.length === 2) colStart = (dataBytes[0] << 8) | dataBytes[1];
          if (dataBytes.length === 4) {
            colEnd = (dataBytes[2] << 8) | dataBytes[3];
            curX = colStart;
          }
          break;
        case 0x2b: // PASET – page address set
          if (dataBytes.length === 2) rowStart = (dataBytes[0] << 8) | dataBytes[1];
          if (dataBytes.length === 4) {
            rowEnd = (dataBytes[2] << 8) | dataBytes[3];
            curY = rowStart;
          }
          break;
        case 0x36: // MADCTL – memory access control (rotation / mirror)
          if (dataBytes.length === 1) {
            const m = dataBytes[0];
            madMY = (m & 0x80) !== 0;
            madMX = (m & 0x40) !== 0;
            madMV = (m & 0x20) !== 0;
          }
          break;
        // All other commands (DISPON, COLMOD…) just buffer data
      }
    };

    // ── Intercept SPI (board-agnostic) ────────────────────────────────
    // Single hook regardless of board kind: every simulator's `.spi`
    // exposes the same shape — settable onByte handler + optional
    // completeTransfer to drive MISO. AVR and RP2040 actually use
    // completeTransfer; ESP32 ignores it (worker drives MISO via
    // its own _spi_response global).
    const prevOnByte = spi.onByte;
    spi.onByte = (value: number) => {
      if (!dcState) processCommand(value);
      else          processData(value);
      // Idle-byte response — the typical ILI9341 driver writes only,
      // so any value works. 0xff matches what the prior AVR path
      // returned to keep behaviour stable.
      spi.completeTransfer?.(0xff);
    };

    // ── Cleanup ───────────────────────────────────────────────────────
    return () => {
      spi.onByte = prevOnByte;
      if (idleTimerId !== null) clearTimeout(idleTimerId);
      el.removeEventListener('canvas-ready', onCanvasReady);
      unsubscribers.forEach((u) => u());
    };
  },
};

