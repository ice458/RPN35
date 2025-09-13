#ifndef HARDWARE_DEFINITION_H
#define HARDWARE_DEFINITION_H

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 16
#define I2C_SCL 17
#define LCD_ADDR 0x3E

// nRST pin for LCD
#define nRST 0

// Key scan defines
#define ROW1 18
#define ROW2 19
#define ROW3 20
#define ROW4 21
#define ROW5 22
#define ROW6 24
#define ROW7 23
#define ROW_MASK ((1 << ROW1) | (1 << ROW2) | (1 << ROW3) | (1 << ROW4) | (1 << ROW5) | (1 << ROW6) | (1 << ROW7))

#define COL1 29
#define COL2 28
#define COL3 27
#define COL4 26
#define COL5 25
#define COL_MASK ((1 << COL1) | (1 << COL2) | (1 << COL3) | (1 << COL4) | (1 << COL5))

// Power control pin
#define POWER_EN 12
#define POWER_DOWN {gpio_put(POWER_EN, 0);}

#endif // HARDWARE_DEFINITION_H