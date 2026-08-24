#ifndef CLOCK_CTRL_H
#define CLOCK_CTRL_H

#ifdef __cplusplus
extern "C"
{
#endif

    // 現在のclk_sysに適したI2Cボーレートを適用（clk_sys変更後は必須）
    void clockctrl_apply_i2c_baudrate(void);
    // 低速クロックへ
    void clockctrl_enter_low_power(void);
    // 高速クロックへ
    void clockctrl_enter_high_speed_12mhz(void);

#ifdef __cplusplus
}
#endif

#endif // CLOCK_CTRL_H
