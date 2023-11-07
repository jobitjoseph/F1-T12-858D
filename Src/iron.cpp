/*
 * iron.cpp
 *
 *  Created on: 13 aug 2019
 *      Author: Alex
 */

#include <math.h>
#include "iron.h"
#include "tools.h"

void IRON::init(void) {
	mode		= POWER_OFF;
	fix_power	= 0;
	chill		= false;
	UNIT::init(iron_sw_len, iron_off_value,	iron_on_value,   sw_tilt_len, sw_off_value, sw_on_value);
	t_iron_short.length(iron_emp_coeff);
	t_amb.length(ambient_emp_coeff);
	h_power.length(ec);
	h_temp.length(ec);
	d_power.length(ec);
	d_temp.length(ec);
	// The IRON is powered by TIM2, calculate the TIM2 period in ms
	uint32_t tim2_period = (TIM2->PSC + 1) * (TIM2->ARR + 1);
	uint32_t cpu_speed = SystemCoreClock / 1000;			// Calculate TIM2 period in ms
	tim2_period /= cpu_speed;
	PID::init(tim2_period, 11, true);						// Initialize PID for IRON
	resetPID();
}

void IRON::switchPower(bool On) {
	if (!On) {
		fix_power	= 0;
		if (mode != POWER_OFF)
			mode = POWER_COOLING;							// Start the cooling process
	} else {
		resetPID();
		temp_low	= 0;									// Disable low power mode
		mode		= POWER_ON;
	}
	h_power.reset();
	d_power.reset();
	temp_low	= 0;
}

uint16_t IRON::alternateTemp(void) {
	uint16_t t = h_temp.read();
	if (mode == POWER_OFF)
		t = 0;
	return t;
}

void IRON::setTemp(uint16_t t) {
	if (mode == POWER_ON) resetPID();
	if (t > int_temp_max) t = int_temp_max;					// Do not allow over heating. int_temp_max is defined in vars.cpp
	temp_set = t;
	uint16_t ta = h_temp.read();
	chill = (ta > t + 20);                         			// The IRON must be cooled
}

uint16_t IRON::avgPower(void) {
	uint16_t p = h_power.read();
	if (mode == POWER_FIXED)
		p = fix_power;
	if (p > max_power) p = max_power;
	return p;
}

uint8_t IRON::avgPowerPcnt(void) {
	uint16_t p 		= h_power.read();
	uint16_t max_p 	= max_power;
	if (mode == POWER_FIXED) {
		p	  = fix_power;
		max_p = max_fix_power;
	} else if (mode == POWER_PID_TUNE) {
		max_p = max_fix_power;
	}
	p = constrain(p, 0, max_p);
	return map(p, 0, max_p, 0, 100);
}

void IRON::fixPower(uint16_t Power) {
	h_power.reset();
	d_power.reset();
	if (Power == 0) {										// To switch off the IRON, set the Power to 0
		fix_power 	= 0;
		mode		= POWER_COOLING;
		return;
	}

	if (Power > max_fix_power)
		fix_power 	= max_fix_power;

	fix_power 	= Power;
	mode		= POWER_FIXED;
}

void IRON::adjust(uint16_t t) {
	if (t > int_temp_max) t = int_temp_max;					// Do not allow over heating
	temp_set = t;
}

// Called from HAL_ADC_ConvCpltCallback() event handler. See core.cpp for details.
uint16_t IRON::power(int32_t t) {
	t				= tempShortAverage(t);					// Prevent temperature deviation using short term history average
	temp_curr		= t;
	int32_t at 		= h_temp.average(temp_curr);
	int32_t diff	= at - temp_curr;
	d_temp.update(diff*diff);

	if ((t >= int_temp_max + 100) || (t > (temp_set + 400))) {	// Prevent global over heating
		if (mode == POWER_ON) chill = true;					// Turn off the power in main working mode only;
	}

	int32_t p = 0;
	switch (mode) {
		case POWER_OFF:
			break;
		case POWER_COOLING:
			if (at < iron_cold)
				mode = POWER_OFF;
			break;
		case POWER_ON:
		{
			uint16_t t_set = temp_set;
			if (temp_low) t_set = temp_low;					// If temp_low setup, turn-on low power mode
			if (chill) {
				if (t < (t_set - 2)) {
					chill = false;
					resetPID();
				} else {
					break;
				}
			}
			p = PID::reqPower(t_set, t);
			p = constrain(p, 0, max_power);
			break;
		}
		case POWER_FIXED:
			p = fix_power;
			break;
		case POWER_PID_TUNE:
			p = PIDTUNE::run(t);
			break;
		default:
			break;
	}

	int32_t	ap		= h_power.average(p);
	diff 			= ap - p;
	d_power.update(diff*diff);
	return p;
}

void IRON::reset(void) {
	resetShortTemp();
	h_power.reset();
	h_temp.reset();
	d_power.reset();
	d_temp.reset();
	mode = POWER_OFF;										// New tip inserted, clear COOLING mode
}


void IRON::lowPowerMode(uint16_t t) {
    if (mode == POWER_ON && t < temp_set) {
        temp_low = t;                           			// Activate low power mode
        chill = true;										// Stop heating, when temp reaches standby one, reset PID
    	h_power.reset();
    	d_power.reset();
    }
}

/*
 * Return ambient temperature in Celsius
 * Caches previous result to skip expensive calculations
 */
int32_t	IRON::ambientTemp(void) {
static const uint16_t add_resistor	= 10000;				// The additional resistor value (10koHm)
static const float 	  normal_temp[2]= { 10000, 25 };		// nominal resistance and the nominal temperature
static const uint16_t beta 			= 3950;     			// The beta coefficient of the thermistor (usually 3000-4000)
static int32_t	average 			= 0;					// Previous value of analog read
static int 		cached_ambient 		= 0;					// Previous value of the temperature

	if (!isConnected()) return default_ambient;				// If IRON is not connected, return default ambient temperature
	if (abs(t_amb.read() - average) < 20)
		return cached_ambient;

	average = t_amb.read();

	if (average < max_ambient_value) {						// prevent division by zero; About -30 degrees
		// convert the value to resistance
		float resistance = 4095.0 / (float)average - 1.0;
		resistance = (float)add_resistor / resistance;

		float steinhart = resistance / normal_temp[0];		// (R/Ro)
		steinhart = log(steinhart);							// ln(R/Ro)
		steinhart /= beta;									// 1/B * ln(R/Ro)
		steinhart += 1.0 / (normal_temp[1] + 273.15);  		// + (1/To)
		steinhart = 1.0 / steinhart;						// Invert
		steinhart -= 273.15;								// convert to Celsius
		cached_ambient	= round(steinhart);
	} else {
		cached_ambient	= default_ambient;
	}
	return cached_ambient;
}
