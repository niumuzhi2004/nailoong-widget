#include "Display_SPD2010.h"
#include "Audio_PCM5101.h"
#include "RTC_PCF85063.h"
#include "Gyro_QMI8658.h"
#include "MIC_MSM.h"
#include "PWR_Key.h"
#include "SD_Card.h"
#include "BAT_Driver.h"
#include "Touch_SPD2010.h"
#include "MJPEGPlayer.h"

void Driver_Init()
{
  Flash_test();
  PWR_Init();
  BAT_Init();
  I2C_Init();
  TCA9554PWR_Init(0x00);
  Backlight_Init();
  Set_Backlight(50);
  PCF85063_Init();
  QMI8658_Init();
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Driver_Init();

  SD_Init();
  Audio_Init();
  MIC_Init();
  LCD_Init();
  Touch_Init();

  Show_First_MJPEG_Frame("/output_video.avi");
}

void loop() {
  uint16_t tx, ty;
  uint8_t  tcnt;
  bool touched = Touch_Get_xy(&tx, &ty, NULL, &tcnt, 1);

  if (touched) {
    audio.connecttoFS(SD_MMC, "/output_audio.m4a");
    Play_MJPEG("/output_video.avi", false, 24);
    audio.stopSong();

    // Drain any touches that built up during playback
    while (Touch_Get_xy(&tx, &ty, NULL, &tcnt, 1)) {}

    Show_First_MJPEG_Frame("/output_video.avi");
  }

  vTaskDelay(pdMS_TO_TICKS(20));
}
