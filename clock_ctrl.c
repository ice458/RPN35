#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "clock_ctrl.h"

// 即時ブースト: 12MHz動作に切替
void clockctrl_boost_now(void)
{
    // 既に12MHz以上なら何もしない
    uint32_t sys_hz = clock_get_hz(clk_sys);
    if (sys_hz >= 12 * MHZ)
        return;

    clock_configure_undivided(clk_ref,
                              CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                              0,
                              12 * MHZ);

    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              12 * MHZ);

    pll_deinit(pll_usb);
    pll_deinit(pll_sys);
}

// 低速クロックへ
void clockctrl_enter_low_power(void)
{
    clock_configure(clk_ref,
                    CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                    0,
                    12 * MHZ,
                    1 * MHZ);

    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              1 * MHZ);

    pll_deinit(pll_usb);
    pll_deinit(pll_sys);
}

// 高速クロックへ
void clockctrl_enter_high_speed_12mhz(void)
{
    clock_configure_undivided(clk_ref,
                              CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                              0,
                              12 * MHZ);

    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              12 * MHZ);

    pll_deinit(pll_usb);
    pll_deinit(pll_sys);
}
