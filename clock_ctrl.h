#ifndef CLOCK_CTRL_H
#define CLOCK_CTRL_H

#ifdef __cplusplus
extern "C"
{
#endif

    // 即時ブースト
    void clockctrl_boost_now(void);
    // 低速クロックへ
    void clockctrl_enter_low_power(void);
    // 高速クロックへ
    void clockctrl_enter_high_speed_12mhz(void);

#ifdef __cplusplus
}
#endif

#endif // CLOCK_CTRL_H
