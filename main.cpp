#include <cstdio>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "modules/pico-onewire/api/one_wire.h"

// Buttons

// Select
#define select 14

// Down
#define down 13

// Up
#define up 12

/////////////////

// Thanks to https://github.com/ParicBat/rpi-pico-i2c-display-lib for LCD pointers.


// LCD I2C address
#define lcd_addr 0x27

// Supply voltage
#define supply 26

// Fan
#define fan_supply 22

// Active lamp
#define active_lamp 25

// Temp sensor
#define sensor 15

#define f_start 30.0
#define f_stop 28.0

const int pins[6][2]{
    {supply, GPIO_IN},
    {fan_supply, GPIO_OUT},
    {active_lamp, GPIO_OUT},
    {select, GPIO_IN},
    {down, GPIO_IN},
    {up, GPIO_IN}
};

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

char* menus[5] = {
    "MENU: BACKLIGHT ",
    "MENU: H TEMP    ",
    "MENU: L TEMP    ",
    "MENU: OVERRIDE  ",
    "MENU: WARNING   "
};

uint8_t bselect = 0;
uint32_t debounce = 0;

typedef struct Config{
    bool backlight;
    float fan_on;
    float fan_off;
}settings;

typedef struct Status{
    bool fan;
    bool high_warning;
    bool low_warning;
    uint64_t samples;
    float temp;
}status;

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

void lcd_write(char val, settings *cfg) {
    lcd_send_byte(val, LCD_TEXT, cfg->backlight);
}

void lcd_print(char *s, settings *cfg) {
    while (*s) {
        lcd_write(*s++, cfg);
    }
}

void buttons(uint gpio, uint32_t events){
    if (time_us_64() - debounce < 300000UL){
        return;
    }
    switch (gpio){
        case select:
        bselect = 0x1;
        break;
        case down:
        bselect = 0x2;
        break;
        case up:
        bselect = 0x4;
        break;
    }
    debounce = time_us_64();
}

int main() {
    debounce = time_us_64();
    char wbuff[5];
    char bbuff[9];

    settings set={
        .backlight = true,
        .fan_on = f_start,
        .fan_off = f_stop
    };

    status state={
        .fan = false,
        .high_warning = false,
        .low_warning = false,
        .samples = 0,
        .temp = 0.0
    };

    settings *mysettings = &set;
    status *mystatus = &state;


    for (int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++){
        gpio_init(pins[i][0]);
    }

    for (int i = 0; i < sizeof(pins)/sizeof(pins[0]); i++){
        gpio_set_dir(pins[i][0], pins[i][1]);
    }

    for (int i = 3; i < sizeof(pins)/sizeof(pins[0]); i++){
        gpio_pull_up(pins[i][0]);
    }

    //stdio_init_all();
    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(PICO_DEFAULT_I2C_SDA_PIN);
    gpio_pull_up(PICO_DEFAULT_I2C_SCL_PIN);
    gpio_put(fan_supply, true);

    adc_init();
    adc_gpio_init(supply);
    adc_select_input(0);

    gpio_set_irq_enabled_with_callback(select, GPIO_IRQ_EDGE_FALL, true, &buttons);
    gpio_set_irq_enabled(down, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(up, GPIO_IRQ_EDGE_FALL, true);

    One_wire one_wire(sensor);
    one_wire.init();
    rom_address_t address{};
    lcd_init();
    sleep_ms(500);

    while (true) {
        mystatus->samples++;
        one_wire.single_device_read_rom(address);
        one_wire.convert_temperature(address, true, false);
        mystatus->temp = one_wire.temperature(address);
        uint16_t voltage = adc_read();
        voltage = voltage*0.8/55;
        if (mystatus->temp > mysettings->fan_on && !mystatus->fan){
            lcd_setCursor(0,8);
            lcd_print("FAN: ON ", mysettings);
            gpio_put(fan_supply, false);
            gpio_put(active_lamp, true);
            mystatus->fan = true;
        } else if (mystatus->temp < mysettings->fan_off && mystatus->fan){
            lcd_setCursor(0,8);
            lcd_print("FAN: OFF", mysettings);
            gpio_put(fan_supply, true);
            gpio_put(active_lamp, false);
            mystatus->fan = false;
        }
        sprintf(wbuff, "%3.1fC", mystatus->temp);
        sprintf(bbuff, (voltage < 10) ? "Batt: %dV " : "Batt: %dV", voltage);

        lcd_setCursor(0,0);
        lcd_print(wbuff, mysettings);
        lcd_setCursor(1,0);
        lcd_print(bbuff, mysettings);

        if (bselect == 0x1){
            uint8_t mselect = 0;
            bool EXIT = false;
            bool toggle = false;
            lcd_send_byte(LCD_CLEAR, LCD_COMMAND, 1);
            lcd_setCursor(0,0);
            lcd_print(menus[mselect], mysettings);
            lcd_setCursor(1,0);
            while (!EXIT){
                switch(bselect){
                    case 0x1:
                    mselect++;
                    lcd_setCursor(0,0);
                    lcd_print(menus[mselect], mysettings);
                    if (mselect > 4)
                        mselect = 0;
                    bselect = 0;
                    break;
                    case 0x04:
                    lcd_setCursor(1,12);
                    if (toggle){
                        lcd_print("ON ", mysettings);
                        toggle = false;
                    } else {
                        lcd_print("OFF", mysettings);
                        toggle = true;
                    }
                    bselect = 0;
                    break;
                    default:
                    break;
                }

                if (bselect==2){
                    lcd_send_byte(LCD_CLEAR, LCD_COMMAND, 1);
                    EXIT = true;
                    bselect = 0;
                }
            }

        }
    }
    return 0;
}