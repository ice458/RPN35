#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/vreg.h"
#include "hardware/xosc.h"
#include "hardware/ticks.h"
#include "hardware/timer.h"
#include "hardware/i2c.h"
#include "clock_ctrl.h"
#include "hardware_definition.h"

// TIMERの1us tick較正はclk_ref基準で起動時に一度だけ設定され自動追従しないため、
// clk_refは12MHzに固定し以後変更しない。低消費電力化はclk_sysの分周のみで行う。
static void clockctrl_init_ticks(void)
{
    uint32_t cycles = clock_get_hz(clk_ref) / MHZ;
    for (int i = 0; i < (int)TICK_COUNT; ++i)
        tick_start((tick_gen_num_t)i, cycles);
}

// SDK既定のruntime_init_clocks()(__weak)を上書きし、main()到達前に一度も
// 150MHzへブーストせず12MHz/1.00Vへ直行させる。低電圧電源での起動失敗対策。
// VREGを下げるのはclk_sysを12MHzに確定させた後にすること
// (リセット直後の未トリムROSCのままVREGを下げると周波数次第でハングする)。
void runtime_init_clocks(void)
{
    clocks_hw->resus.ctrl = 0;
    xosc_init();

    // aux源から離脱(ウォッチドッグ再起動等への備え。SDK既定と同じ手順)
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_sys].selected != 0x1)
        tight_loop_contents();
    hw_clear_bits(&clocks_hw->clk[clk_ref].ctrl, CLOCKS_CLK_REF_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_ref].selected != 0x1)
        tight_loop_contents();

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

    vreg_set_voltage(VREG_VOLTAGE_1_00);
    // レギュレータ安定待ち(TIMER未確立のためXOSC基準のサイクル数で待つ)
    busy_wait_at_least_cycles((uint32_t)((1000ull * XOSC_HZ) / 1000000));

    // 0Hzのままだと直後のSDK既定処理(周辺ブロックのunreset待ち)がハングするため、
    // 使わない周辺クロックにもPLLなしの実クロックを与えておく
    clock_configure_undivided(clk_peri,
                              0,
                              CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                              12 * MHZ);
    clock_configure_undivided(clk_adc,
                              0,
                              CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC,
                              12 * MHZ);
    clock_configure_undivided(clk_usb,
                              0,
                              CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_XOSC_CLKSRC,
                              12 * MHZ);
    clock_configure_undivided(clk_hstx,
                              0,
                              CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS,
                              12 * MHZ);

    clockctrl_init_ticks();
}

// I2Cボーレートはclk_sys由来で自動追従しないため、clk_sys変更時は必ず呼ぶこと。
// SDKは分周比20以上を要求し100kHz固定だとclk_sys=1MHzで生成不能なため、
// 出せる範囲(上限100kHz)にクランプする。
void clockctrl_apply_i2c_baudrate(void)
{
    uint32_t baud = clock_get_hz(clk_sys) / 20;
    if (baud > 100 * 1000)
        baud = 100 * 1000;
    i2c_set_baudrate(I2C_PORT, baud);
}

// 低速クロックへ (clk_sysのみ1MHzへ分周。clk_refは12MHzのまま維持する)
void clockctrl_enter_low_power(void)
{
    clock_configure(clk_sys,
                    CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                    0,
                    12 * MHZ,
                    1 * MHZ);
    clockctrl_apply_i2c_baudrate();
}

// 高速クロックへ (clk_sysの分周を解除)
void clockctrl_enter_high_speed_12mhz(void)
{
    clock_configure_undivided(clk_sys,
                              CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                              0,
                              12 * MHZ);
    clockctrl_apply_i2c_baudrate();
}
