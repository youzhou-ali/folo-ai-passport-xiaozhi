<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# FoloToy AI Passport（ai-passport）

让小智语音助手在 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)
随身工牌上运行的开发板实现。

## 硬件

- 主控：ESP32-C3，8 MB Flash，**无 PSRAM**，USB Serial/JTAG 控制台。
- 音频：共享 I2C 总线上的 ES8311（地址 0x18），I2S 全双工；功放使能未接主控，视为常开。
- 显示：ST7789（ST7789P3）240×320 竖屏，四线 SPI。
- 电量：CW2017 电量计（地址 0x63，可选）。
- 按键：上、下、OK 三键通过电阻分压共用 GPIO0（ADC1_CH0）。

引脚遵循 `ai-passport/components/bsp/include/bsp_pins.h`：

| 功能 | 引脚 |
| --- | --- |
| LCD MOSI / SCLK / CS / DC | 9 / 8 / 1 / 20 |
| LCD 背光（PWM） | 21 |
| I2S MCLK / BCLK / WS / DOUT / DIN | 6 / 5 / 3 / 2 / 4 |
| I2C SDA / SCL | 10 / 7 |
| 电阻分压按键 | GPIO0 |

## 构建

```sh
python scripts/build.py folotoy/ai-passport --name ai-passport
```

生成 `build/merged-binary.bin`，使用 8 MB Flash 和 USB Serial/JTAG 控制台。

## 操作

- **OK**：单击切换对话状态；启动过程中单击进入 Wi-Fi 配网。
- **上键**：单击增加 10 音量，长按设为最大音量。
- **下键**：单击降低 10 音量，长按静音。

三个按键共用一个 ADC 引脚，通过不同电压窗口独立识别，方式与 ESP-BOX-Lite 相同。

## 眼神陪伴界面

竖屏采用深色背景和一双圆润发光的眼睛。待命时轻轻呼吸、转动和眨眼；聆听时睁大眼睛；
识别出用户语音后，以向右上看的暖琥珀色眼神表示思考；回答时眼睛随节奏起伏。
云端的开心情绪变成薄荷色笑眼，异常变成柔和的珊瑚色表情。临时情绪持续三秒，
随后恢复当前对话状态。动画使用 LVGL 图形绘制，无需解码视频。

字幕在眼睛下方的固定区域换行显示，超长内容以每秒 20 像素向上滚动，首尾各停留片刻。
对话结束后保留最后一条回答，便于继续阅读，下一次对话开始时清空。
继续使用原有中文字库和动态字形支持。

闲置 60 秒后关闭 LCD 和背光，暂停眼睛动画。对话、配网、升级过程中，以及长字幕第一遍
尚未读完时保持亮屏。任意按键均可唤醒并在同一次按压中执行原操作，OK 键开始对话。
唤醒后恢复已有亮度设置。息屏不会断开 Wi-Fi 或清除配置。本 ESP32-C3 版本使用 OK 键，
未启用语音唤醒。

## 校准说明

- 屏幕方向、反色和背光 PWM 极性来自 Passport BSP，仍需在实物上确认；调整时使用
  `config.h` 中的 `DISPLAY_*` 定义。
- ADC 电压窗口按 Passport 外部 10 kΩ 上拉设计，若阈值漂移，应在原固件的按键测试页重新测量。
- CW2017 为可选器件；不存在时不显示电量，开发板其他功能仍可运行。
