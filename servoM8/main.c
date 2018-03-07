/*
    Copyright (c) 2006 Michael P. Thompson <mpthompson@gmail.com>

    Permission is hereby granted, free of charge, to any person 
    obtaining a copy of this software and associated documentation 
    files (the "Software"), to deal in the Software without 
    restriction, including without limitation the rights to use, copy, 
    modify, merge, publish, distribute, sublicense, and/or sell copies 
    of the Software, and to permit persons to whom the Software is 
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be 
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
    DEALINGS IN THE SOFTWARE.

    $Id$
*/

#include <inttypes.h>
#include <avr/interrupt.h>
#include <avr/io.h>

#include "openservo.h"
#include "config.h"
#include "adc.h"
#include "eeprom.h"
#include "estimator.h"
#include "ipd.h"
#include "motion.h"
#include "pid.h"
#include "regulator.h"
#include "power.h"
#include "pwm.h"
#include "seek.h"
#include "pulsectl.h"
#include "timer.h"
#include "twi.h"
#include "watchdog.h"
#include "registers.h"

static void config_pin_defaults(void)
// Configure pins to their default states to conform to recommendation that all 
// AVR MCU pins have a defined level.  We do this by configuring unused pins
// as inputs and enabling the internal pull-ups.
{
#if defined(__AVR_ATmega8__) || defined(__AVR_ATmega88__) || defined(__AVR_ATmega168__)
    // Configure unused port B pins as inputs and enable internal pull-up resistor.
    DDRB = (0<<DDB7) | (0<<DDB6) | (0<<DDB5) | (0<<DDB4) |
           (0<<DDB3) | (1<<DDB2) | (1<<DDB1) | (0<<DDB0);
    PORTB = (1<<PORTB7) | (1<<PORTB6) | (1<<PORTB5) | (1<<PORTB4) |
            (1<<PORTB3) | (0<<PORTB2) | (0<<PORTB1) | (1<<PORTB0);

    // Configure unused port C pins as inputs and enable internal pull-up resistor.
    DDRC = (0<<DDC6) | (0<<DDC5) | (0<<DDC4) |
           (0<<DDC3) | (0<<DDC2) | (0<<DDC1) | (0<<DDC0);
    PORTC = (1<<PORTC6) | (1<<PORTC5) | (1<<PORTC4) |
            (1<<PORTC3) | (1<<PORTC2) | (1<<PORTC1) | (1<<PORTC0);

    // Configure port D pins as inputs and enable internal pull-up resistor.
    DDRD = (0<<DDD7) | (0<<DDD6) | (0<<DDD5) | (0<<DDD4) |
           (0<<DDD3) | (0<<DDD2) | (0<<DDD1) | (0<<DDD0);
    PORTD = (1<<PORTD7) | (1<<PORTD6) | (1<<PORTD5) | (1<<PORTD4) |
            (1<<PORTD3) | (1<<PORTD2) | (1<<PORTD1) | (1<<PORTD0);
#endif
}


static void handle_twi_command(void)
{
    uint8_t command;

    // Get the command from the receive buffer.
    command = twi_receive_byte();

    switch (command)
    {
        case TWI_CMD_RESET:

            // Reset the servo.
            watchdog_hard_reset();

            break;

        case TWI_CMD_PWM_ENABLE:

            // Enable PWM to the servo motor.
            pwm_enable();

            break;

        case TWI_CMD_PWM_DISABLE:

            // Disable PWM to the servo motor.
            pwm_disable();

            break;

        case TWI_CMD_WRITE_ENABLE:

            // Enable write to read/write protected registers.
            registers_write_enable();

            break;

        case TWI_CMD_WRITE_DISABLE:

            // Disable write to read/write protected registers.
            registers_write_disable();

            break;

        case TWI_CMD_REGISTERS_SAVE:

            // Save register values into EEPROM.
            eeprom_save_registers();

            break;

        case TWI_CMD_REGISTERS_RESTORE:

            // Restore register values into EEPROM.
            eeprom_restore_registers();

            break;

        case TWI_CMD_REGISTERS_DEFAULT:

            // Restore register values to factory defaults.
            registers_defaults();
            break;

        case TWI_CMD_EEPROM_ERASE:

            // Erase the EEPROM.
            eeprom_erase();

            break;

        case TWI_CMD_VOLTAGE_READ:

            // Request a voltage reading.
            adc_read_voltage();

            break;

#if CURVE_MOTION_ENABLED
        case TWI_CMD_CURVE_MOTION_ENABLE:

            // Enable curve motion handling.
            motion_enable();

            break;

        case TWI_CMD_CURVE_MOTION_DISABLE:

            // Disable curve motion handling.
            motion_disable();

            break;

        case TWI_CMD_CURVE_MOTION_RESET:

            // Reset the motion to the current position.
            motion_reset(adc_get_position_value());

            break;

        case TWI_CMD_CURVE_MOTION_APPEND:

            // Append motion curve data stored in the registers.
            motion_append();

            break;
#endif

        default:

            // Ignore unknown command.
            break;
    }
}


int main (void)
{
	// Configure pins to the default states.
	config_pin_defaults();

    // Initialize the watchdog module.
    watchdog_init();

    // First, initialize registers that control servo operation.
    registers_init();

    // Initialize the PWM module.
    pwm_init();

    // Initialize the ADC module.
    adc_init();

#if ESTIMATOR_ENABLED
    // Initialize the state estimator module.
    estimator_init();
#endif

#if REGULATOR_MOTION_ENABLED
    // Initialize the regulator algorithm module.
    regulator_init();
#endif

#if PID_MOTION_ENABLED
    // Initialize the PID algorithm module.
    pid_init();
#endif

#if IPD_MOTION_ENABLED
    // Initialize the IPD algorithm module.
    ipd_init();
#endif

#if CURVE_MOTION_ENABLED
    // Initialize curve motion module.
    motion_init();
#endif

    // Initialize the power module.
    power_init();

#if PULSE_CONTROL_ENABLED
    pulse_control_init();
#endif

    // Initialize the TWI slave module.
    twi_slave_init(registers_read_byte(REG_TWI_ADDRESS));

    // Finally initialize the timer.
    timer_set(0);

    // Enable interrupts.
    sei();

    // Wait until initial position value is ready.
    while (!adc_position_value_is_ready());

#if CURVE_MOTION_ENABLED
    // Reset the curve motion with the current position of the servo.
    motion_reset(adc_get_position_value());
#endif

    // Set the initial seek position and velocity.
    registers_write_word(REG_SEEK_POSITION_HI, REG_SEEK_POSITION_LO, adc_get_position_value());
    registers_write_word(REG_SEEK_VELOCITY_HI, REG_SEEK_VELOCITY_LO, 0);

    // XXX Enable PWM and writing.  I do this for now to make development and
    // XXX tuning a bit easier.  Constantly manually setting these values to 
    // XXX turn the servo on and write the gain values get's to be a pain.
    pwm_enable();
    registers_write_enable();

    // This is the main processing loop for the servo.  It basically looks
    // for new position, power or TWI commands to be processed.
    for (;;)
    {
        // Is position value ready?
        if (adc_position_value_is_ready())
        {
            int16_t pwm;
            int16_t position;

#if PULSE_CONTROL_ENABLED
            // Give pulse control a chance to update the seek position.
            pulse_control_update();
#endif

#if CURVE_MOTION_ENABLED
            // Give the motion curve a chance to update the seek position and velocity.
            motion_next(10);
#endif

            // Get the new position value.
            position = (int16_t) adc_get_position_value();

#if ESTIMATOR_ENABLED
            // Estimate velocity.
            estimate_velocity(position);
#endif

#if PID_MOTION_ENABLED
            // Call the PID algorithm module to get a new PWM value.
            pwm = pid_position_to_pwm(position);
#endif

#if IPD_MOTION_ENABLED
            // Call the IPD algorithm module to get a new PWM value.
            pwm = ipd_position_to_pwm(position);
#endif

#if REGULATOR_MOTION_ENABLED
            // Call the state regulator algorithm module to get a new PWM value.
            pwm = regulator_position_to_pwm(position);
#endif

            // Update the servo movement as indicated by the PWM value.
            // Sanity checks are performed against the position value.
            pwm_update(position, pwm);
        }

        // Is a power value ready?
        if (adc_power_value_is_ready())
        {
            // Get the new power value.
            uint16_t power = adc_get_power_value();

            // Update the power value for reporting.
            power_update(power);
        }

        // Was a command recieved?
        if (twi_data_in_receive_buffer())
        {
            // Handle any TWI command.
            handle_twi_command();
        }

#if MAIN_MOTION_TEST_ENABLED
        // This code is in place for having the servo drive itself between 
        // two positions to aid in the servo tuning process.  This code 
        // should normally be disabled in config.h.
#if CURVE_MOTION_ENABLED
        if (motion_time_left() == 0)
        {
            registers_write_word(REG_CURVE_DELTA_HI, REG_CURVE_DELTA_LO, 2000);
            registers_write_word(REG_CURVE_POSITION_HI, REG_CURVE_POSITION_LO, 0x0100);
            motion_append();
            registers_write_word(REG_CURVE_DELTA_HI, REG_CURVE_DELTA_LO, 1000);
            registers_write_word(REG_CURVE_POSITION_HI, REG_CURVE_POSITION_LO, 0x0300);
            motion_append();
            registers_write_word(REG_CURVE_DELTA_HI, REG_CURVE_DELTA_LO, 2000);
            registers_write_word(REG_CURVE_POSITION_HI, REG_CURVE_POSITION_LO, 0x0300);
            motion_append();
            registers_write_word(REG_CURVE_DELTA_HI, REG_CURVE_DELTA_LO, 1000);
            registers_write_word(REG_CURVE_POSITION_HI, REG_CURVE_POSITION_LO, 0x0100);
            motion_append();
        }
#else
        {
            // Get the timer.
            uint16_t timer = timer_get();

            // Reset the timer if greater than 800.
            if (timer > 800) timer_set(0);

            // Look for specific events.
            if (timer == 0)
            {
                registers_write_word(REG_SEEK_HI, REG_SEEK_LO, 0x0100);
            }
            else if (timer == 400)
            {
                registers_write_word(REG_SEEK_HI, REG_SEEK_LO, 0x0300);
            }
        }
#endif
#endif
    }

    return 0;
}

