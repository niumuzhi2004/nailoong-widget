#pragma once
#include <Arduino.h>
#include <SD_MMC.h>
#include <JPEGDEC.h>
#include "Display_SPD2010.h"

// ----------------------------------------------------------------------------
// Panel geometry (from Display_SPD2010.h: 412 x 412)
// ----------------------------------------------------------------------------
#define MJPEG_PANEL_W   EXAMPLE_LCD_WIDTH
#define MJPEG_PANEL_H   EXAMPLE_LCD_HEIGHT

// Centering / crop offset for the current clip.
static int   s_mjpeg_offX = 0;
static int   s_mjpeg_offY = 0;

// Compressed-JPEG input buffer (PSRAM, grown on demand)
static uint8_t  *s_mjpeg_buf = nullptr;
static uint32_t  s_mjpeg_cap = 0;

// Full decoded framebuffer (PSRAM, 412x412x2 ≈ 330 KB)
static uint16_t *s_frame_buf = nullptr;

// ----------------------------------------------------------------------------
// JPEGDEC callback — copies one MCU block into the framebuffer (no LCD call)
// ----------------------------------------------------------------------------
static int jpegDrawCallback(JPEGDRAW *pDraw)
{
  if (!s_frame_buf) return 0;

  int sx = pDraw->x + s_mjpeg_offX;
  int sy = pDraw->y + s_mjpeg_offY;

  for (int r = 0; r < pDraw->iHeight; r++) {
    int fy = sy + r;
    if (fy < 0 || fy >= MJPEG_PANEL_H) continue;

    uint16_t *src = pDraw->pPixels + r * pDraw->iWidth;
    uint16_t *dst = s_frame_buf + fy * MJPEG_PANEL_W;

    int fx0 = sx < 0 ? 0 : sx;
    int fx1 = sx + pDraw->iWidth;
    if (fx1 > MJPEG_PANEL_W) fx1 = MJPEG_PANEL_W;
    if (fx0 >= fx1) continue;

    memcpy(dst + fx0, src + (fx0 - sx), (fx1 - fx0) * sizeof(uint16_t));
  }
  return 1;
}

// ----------------------------------------------------------------------------
// Small AVI helpers
// ----------------------------------------------------------------------------
static uint32_t readU32LE(File &f) {
  uint8_t b[4] = {0};
  if (f.read(b, 4) != 4) return 0;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static bool mjpeg_ensure_buf(uint32_t need) {
  if (need <= s_mjpeg_cap && s_mjpeg_buf) return true;
  uint8_t *p = (uint8_t*)heap_caps_realloc(s_mjpeg_buf, need, MALLOC_CAP_SPIRAM);
  if (!p) p = (uint8_t*)heap_caps_realloc(s_mjpeg_buf, need, MALLOC_CAP_8BIT);
  if (!p) return false;
  s_mjpeg_buf = p;
  s_mjpeg_cap = need;
  return true;
}

static bool mjpeg_ensure_framebuf() {
  if (s_frame_buf) return true;
  s_frame_buf = (uint16_t*)heap_caps_malloc(
      (uint32_t)MJPEG_PANEL_W * MJPEG_PANEL_H * sizeof(uint16_t),
      MALLOC_CAP_SPIRAM);
  if (!s_frame_buf) {
    Serial.println("MJPEG: failed to allocate framebuffer");
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
// Decode and display only the first video frame of an AVI — used as the
// idle/thumbnail state while waiting for user input.
// ----------------------------------------------------------------------------
void Show_First_MJPEG_Frame(const char *filename)
{
  if (!mjpeg_ensure_framebuf()) return;
  static JPEGDEC jpeg;

  File aviFile = SD_MMC.open(filename);
  if (!aviFile) {
    Serial.printf("Show_First_MJPEG_Frame: failed to open %s\r\n", filename);
    return;
  }

  uint32_t fileSize = aviFile.size();
  uint8_t  tag[4]   = {0};

  // Locate 'movi' (same logic as Play_MJPEG)
  bool moviFound = false;
  if (fileSize > 12) {
    aviFile.read(tag, 4); readU32LE(aviFile); aviFile.read(tag, 4);
    if (memcmp(tag, "AVI ", 4) == 0) {
      while (aviFile.position() + 8 <= fileSize) {
        if (aviFile.read(tag, 4) != 4) break;
        uint32_t sz = readU32LE(aviFile);
        if (memcmp(tag, "LIST", 4) == 0) {
          uint8_t lt[4];
          if (aviFile.read(lt, 4) != 4) break;
          if (memcmp(lt, "movi", 4) == 0) { moviFound = true; break; }
          aviFile.seek(aviFile.position() + (sz - 4));
        } else {
          aviFile.seek(aviFile.position() + sz);
        }
        if (sz & 1) aviFile.seek(aviFile.position() + 1);
      }
    }
  }
  if (!moviFound) { aviFile.close(); return; }

  // Walk chunks until the first non-empty video frame
  while (aviFile.position() + 8 <= fileSize) {
    if (aviFile.read(tag, 4) != 4) break;
    uint32_t chunkSize    = readU32LE(aviFile);
    uint32_t chunkDataPos = aviFile.position();

    if (memcmp(tag, "idx1", 4) == 0) break;
    if (memcmp(tag, "LIST", 4) == 0) { aviFile.seek(chunkDataPos + 4); continue; }
    if (chunkDataPos + chunkSize > fileSize) break;
    if (chunkSize == 0) { aviFile.seek(chunkDataPos); continue; }

    bool isVideo = (tag[2] == 'd' && (tag[3] == 'c' || tag[3] == 'b'));
    if (isVideo && mjpeg_ensure_buf(chunkSize)) {
      aviFile.read(s_mjpeg_buf, chunkSize);
      if (jpeg.openRAM(s_mjpeg_buf, chunkSize, jpegDrawCallback)) {
        jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
        s_mjpeg_offX = (MJPEG_PANEL_W - jpeg.getWidth())  / 2;
        s_mjpeg_offY = (MJPEG_PANEL_H - jpeg.getHeight()) / 2;
        jpeg.decode(0, 0, 0);
        jpeg.close();
        LCD_addWindow(0, 0, MJPEG_PANEL_W - 1, MJPEG_PANEL_H - 1, s_frame_buf);
      }
      break;  // only the first frame
    }
    aviFile.seek(chunkDataPos + chunkSize + (chunkSize & 1));
  }

  aviFile.close();
}

// ----------------------------------------------------------------------------
// Play an MJPEG-in-AVI file.
//   filename     : e.g. "/output_video.avi"
//   loopForever  : restart from the top when the clip ends
//   targetFPS    : 0 = play as fast as decoding allows; otherwise pace to FPS
// ----------------------------------------------------------------------------
void Play_MJPEG(const char *filename, bool loopForever = false, uint8_t targetFPS = 0)
{
  if (!mjpeg_ensure_framebuf()) return;

  const uint32_t frameInterval_us = targetFPS ? (1000000UL / targetFPS) : 0;
  static JPEGDEC jpeg;

  do {
    File aviFile = SD_MMC.open(filename);
    if (!aviFile) {
      Serial.printf("MJPEG: failed to open %s\r\n", filename);
      return;
    }

    uint32_t fileSize = aviFile.size();
    uint8_t  tag[4]   = {0};

    // --- Locate the 'movi' list -----------------------------------------------
    bool moviFound = false;
    if (fileSize > 12) {
      aviFile.read(tag, 4);                  // "RIFF"
      readU32LE(aviFile);                    // file size
      aviFile.read(tag, 4);                  // "AVI "
      if (memcmp(tag, "AVI ", 4) == 0) {
        while (aviFile.position() + 8 <= fileSize) {
          if (aviFile.read(tag, 4) != 4) break;
          uint32_t sz = readU32LE(aviFile);
          if (memcmp(tag, "LIST", 4) == 0) {
            uint8_t listType[4];
            if (aviFile.read(listType, 4) != 4) break;
            if (memcmp(listType, "movi", 4) == 0) { moviFound = true; break; }
            aviFile.seek(aviFile.position() + (sz - 4));
          } else {
            aviFile.seek(aviFile.position() + sz);
          }
          if (sz & 1) aviFile.seek(aviFile.position() + 1);
        }
      }
    }

    if (!moviFound) {
      // Fallback: brute-force scan for the literal "movi" marker.
      aviFile.seek(0);
      while (aviFile.position() + 4 <= fileSize) {
        if (aviFile.read(tag, 4) != 4) break;
        if (memcmp(tag, "movi", 4) == 0) { moviFound = true; break; }
        aviFile.seek(aviFile.position() - 3);
      }
    }

    if (!moviFound) {
      Serial.println("MJPEG: 'movi' list not found");
      aviFile.close();
      return;
    }

    int      frameCount = 0;
    bool     sized      = false;
    uint32_t lastFrame  = micros();

    while (aviFile.position() + 8 <= fileSize) {
      if (aviFile.read(tag, 4) != 4) break;
      uint32_t chunkSize    = readU32LE(aviFile);
      uint32_t chunkDataPos = aviFile.position();   // byte right after the 8-byte header

      if (memcmp(tag, "idx1", 4) == 0) break;

      if (memcmp(tag, "LIST", 4) == 0) {
        aviFile.seek(chunkDataPos + 4);             // consume 4-byte list type, step inside
        continue;
      }

      // Reject oversized chunks (corrupt size field) — bail cleanly
      if (chunkDataPos + chunkSize > fileSize) {
        Serial.printf("MJPEG: oversized chunk [%c%c%c%c] size=%u @ %u\r\n",
                      tag[0], tag[1], tag[2], tag[3], chunkSize, chunkDataPos);
        break;
      }
      // Zero-size video chunk = null/duplicate frame — skip it gracefully
      if (chunkSize == 0) {
        aviFile.seek(chunkDataPos);   // already there; just let the loop continue
        continue;
      }

      // "##dc"/"##db" = video frame; anything else (e.g. "01wb" audio) is skipped.
      bool isVideo = (tag[2] == 'd' && (tag[3] == 'c' || tag[3] == 'b'));
      if (isVideo) {
        if (!mjpeg_ensure_buf(chunkSize)) {
          Serial.println("MJPEG: out of memory for frame");
        } else {
          aviFile.read(s_mjpeg_buf, chunkSize);
          if (jpeg.openRAM(s_mjpeg_buf, chunkSize, jpegDrawCallback)) {
            jpeg.setPixelType(RGB565_LITTLE_ENDIAN);

            if (!sized) {
              s_mjpeg_offX = (MJPEG_PANEL_W - jpeg.getWidth())  / 2;
              s_mjpeg_offY = (MJPEG_PANEL_H - jpeg.getHeight()) / 2;
              sized = true;
            }

            jpeg.decode(0, 0, 0);
            jpeg.close();
            LCD_addWindow(0, 0, MJPEG_PANEL_W - 1, MJPEG_PANEL_H - 1, s_frame_buf);
            frameCount++;
          }
        }
      }

      aviFile.seek(chunkDataPos + chunkSize + (chunkSize & 1));

      // Optional frame pacing (only counts time between video frames).
      if (isVideo && frameInterval_us) {
        uint32_t now = micros(), spent = now - lastFrame;
        if (spent < frameInterval_us) delay((frameInterval_us - spent) / 1000);
        lastFrame = micros();
      }

      vTaskDelay(1);
    }

    aviFile.close();
  } while (loopForever);
}
