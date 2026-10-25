#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

#define IN_U 15
#define IN_V 16
#define IN_W 17
#define EN   18
#define BTN  4    // 按钮一端接 GPIO4，一端接 GND（用内部上拉，无需外接电阻）

static const uint8_t wave[6] = {0b101, 0b100, 0b110, 0b010, 0b011, 0b001};

static inline void set_step(int s)
{
    gpio_set_level(IN_U, (wave[s] >> 2) & 1);
    gpio_set_level(IN_V, (wave[s] >> 1) & 1);
    gpio_set_level(IN_W, (wave[s] >> 0) & 1);
}

static inline void all_off(void)
{
    gpio_set_level(IN_U, 0);
    gpio_set_level(IN_V, 0);
    gpio_set_level(IN_W, 0);
}

static void step_pwm(int step, int duration_ms, int duty_pct)
{
    const int period_us = 100;
    int on_us  = period_us * duty_pct / 100;
    int off_us = period_us - on_us;
    int cycles = duration_ms * 10;
    for (int i = 0; i < cycles; i++) {
        if (on_us)  { set_step(step); esp_rom_delay_us(on_us); }
        if (off_us) { all_off();     esp_rom_delay_us(off_us); }
    }
}

int app_main(void)
{
    // 电机 4 路输出
    gpio_config_t motor_cfg = {
        .pin_bit_mask = (1ULL << IN_U) | (1ULL << IN_V) | (1ULL << IN_W) | (1ULL << EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&motor_cfg);

    // 1) 按钮配置：上拉→下拉
gpio_config_t btn_cfg = {
    .pin_bit_mask = (1ULL << BTN),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,        // 改
    .pull_down_en = GPIO_PULLDOWN_ENABLE,      // 改：启用内部下拉
    .intr_type = GPIO_INTR_DISABLE,
};

    gpio_config(&btn_cfg);

    printf("=== BLDC 按键启停版 ===\n");
    printf("GPIO4 接按钮到 GND，按一下启动 / 再按一下停止\n");

    int step = 0;
    int duty = 30, d_ms = 20, ramp_cnt = 0;   // 软启动状态
    bool running = false;

    while (1) {
        // —— 按键检测（带消抖 + 等释放，避免一次按下被读多次）——
        if (gpio_get_level(BTN) == 1) {
            vTaskDelay(pdMS_TO_TICKS(20));            // 20ms 消抖
            if (gpio_get_level(BTN) == 1) {
                running = !running;
                while (gpio_get_level(BTN) == 1)      // 等手松开
                    vTaskDelay(pdMS_TO_TICKS(10));
                if (running) {
                    gpio_set_level(EN, 1);
                    duty = 30; d_ms = 20; ramp_cnt = 0;   // 复位软启动
                    printf("启动\n");
                } else {
                    all_off();
                    gpio_set_level(EN, 0);
                    printf("停止\n");
                }
            }
        }

        if (running) {
            step_pwm(step, d_ms, duty);
            step = (step + 1) % 6;

            // —— 软启动爬升：每 20 步升一档占空比 + 缩一档换相间隔 ——
            if (++ramp_cnt >= 20) {
                ramp_cnt = 0;
                if (duty < 100) duty += 10;
                if (d_ms > 2)   d_ms -= 1;
                if (duty >= 100 && d_ms <= 2)
                    printf("稳态运行\n");
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));             // 停止时让出 CPU
        }
    }
    return 0;
}
