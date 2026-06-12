# Nailoong Interactive Widget

An interactive Nailoong (奶龙) widget for the Waveshare ESP32-S3-Touch-LCD-1.46 board.

## Description

Touch the screen to make Nailoong laugh out LOUD! The display shows a still frame from the video. Tapping the screen plays the full video and audio together. Touch is ignored during playback; when the video ends the still frame returns.

## Features

- Touchscreen interaction
- MJPEG video playback from SD card
- Synchronized audio (M4A) from SD card
- Built for Waveshare ESP32-S3-Touch-LCD-1.46

## SD Card Setup

Copy the following files from the `Assets/` folder to the **root** of a FAT32-formatted SD card:

| File | Description |
|------|-------------|
| `output_video.avi` | MJPEG video (412×412) |
| `output_audio.m4a` | AAC audio track |

Insert the SD card into the board before powering on.

## Usage

1. Copy the asset files to the SD card root as described above.
2. Flash the project to your ESP32-S3-Touch-LCD-1.46 board.
3. The first frame of the video is shown on boot.
4. Tap the touchscreen to play the video and audio.
5. After playback, the still frame returns — tap again to replay.

## Library Requirements

- `esp32` by Espressif Systems @ `3.0.2`
- `lvgl` @ `8.3.10`
- `ESP32-audioI2S-master` @ `2.0.0`
- `JPEGDEC` by Larry Bank
