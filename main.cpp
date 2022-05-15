#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "modules/pico-onewire/api/one_wire.h"

// Thanks to https://github.com/ParicBat/rpi-pico-i2c-display-lib for LCD pointers.

// LCD I2C address
#define lcd_addr 0x27

// Temp sensor
#define sensor 15

#define f_start 30.0
#define f_stop 28.0

// LCD control
const uint8_t LCD_CLEAR = 0x01;
const uint8_t LCD_RETURNHOME = 0x02;
const uint8_t LCD_ENTRYMODESET = 0x04;
const uint8_t LCD_DISPLAYCONTROL = 0x08;
const uint8_t LCD_CURSORSHIFT = 0x10;
const uint8_t LCD_FUNCTIONSET = 0x20;
const uint8_t LCD_SETCGRAMADDR = 0x40;
const uint8_t LCD_SETDDRAMADDR = 0x80;

// LCD params
const uint8_t LCD_ENABLE_BIT = 0x04;
const uint8_t LCD_BACKLIGHT = 0x08;

// LCD mode
const uint8_t LCD_COMMAND = 0;
const uint8_t LCD_TEXT = 1;

const uint8_t LCD_ENTRYLEFT = 0x02;
const uint8_t LCD_DISPLAYON = 0x04;

// LCD type
const uint8_t LCD_2LINE = 0x08;

void i2c_write_byte(uint8_t val) {
    i2c_write_blocking(i2c_default, lcd_addr, &val, 1, false);
}

void lcd_toggle_enable(uint8_t val) {
    // Toggle enable pin on LCD display
    // We cannot do this too quickly or things don't work
#define DELAY_US 600
    sleep_us(DELAY_US);
    i2c_write_byte(val | LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
    i2c_write_byte(val & ~LCD_ENABLE_BIT);
    sleep_us(DELAY_US);
}


void lcd_send_byte(uint8_t val, uint8_t mode, uint8_t backlight) {
    uint8_t high;
    uint8_t low;

    if(backlight)
    {
        high = mode | (val & 0xF0) | LCD_BACKLIGHT;
        low = mode | ((val << 4) & 0xF0) | LCD_BACKLIGHT;
    }
    else {
        high = mode | (val & 0xF0);
        low = mode | ((val << 4) & 0xF0);
    }

    i2c_write_byte(high);
    lcd_toggle_enable(high);
    i2c_write_byte(low);
    lcd_toggle_enable(low);
}

void lcd_setCursor(uint8_t line, uint8_t position) {
    uint8_t line_offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    uint8_t val = 0x80 + line_offsets[line] + position;
    lcd_send_byte(val, LCD_COMMAND, 1);
}

void lcd_init() {
    lcd_send_byte(0x03, LCD_COMMAND, 1);
    lcd_send_byte(0x03, LCD_COMMAND, 1);
    lcd_send_byte(0x03, LCD_COMMAND, 1);
    lcd_send_byte(0x02, LCD_COMMAND, 1);

    lcd_send_byte(LCD_ENTRYMODESET | LCD_ENTRYLEFT, LCD_COMMAND, 1);
    lcd_send_byte(LCD_FUNCTIONSET | LCD_2LINE, LCD_COMMAND, 1);
    lcd_send_byte(LCD_DISPLAYCONTROL | LCD_DISPLAYON, LCD_COMMAND, 1);
    lcd_send_byte(LCD_CLEAR, LCD_COMMAND, 1);
}

void lcd_write(char val) {
    lcd_send_byte(val, 1, 1);
}

void lcd_print(char *s) {
    while (*s) {
        lcd_write(*s++);
    }
}

int main() {
    char wbuff[5];
    bool fan = false;
    gpio_init(sensor);
    stdio_init_all();
    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    One_wire one_wire(sensor);
    one_wire.init();
    rom_address_t address{};
    lcd_init();
    lcd_setCursor(1,0);
    lcd_print("Batt: 50v");
    lcd_setCursor(0,8);
    lcd_print("FAN: OFF");
    lcd_setCursor(0,0);
    sleep_ms(500);
    
    while (true) {
        one_wire.single_device_read_rom(address);
        one_wire.convert_temperature(address, true, false);
        printf("Temperature: %3.1foC\n", one_wire.temperature(address));
        float ftemp = one_wire.temperature(address);
        if (ftemp > f_start && !fan){
            lcd_setCursor(0,8);
            lcd_print("FAN: ON ");
            fan = true;
        } else if (ftemp < f_stop && fan){
            lcd_setCursor(0,8);
            lcd_print("FAN: OFF");
            fan = false;
        }
        sprintf(wbuff, "%3.1fC", one_wire.temperature(address));
        lcd_setCursor(0,0);
        lcd_print(wbuff);
    }
    return 0;
}