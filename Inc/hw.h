/*
 * hw.h
 *
 *  Created on: 12 aug 2019
 *      Author: Alex
 */

#ifndef HW_H_
#define HW_H_

#include "stat.h"
#include "iron.h"
#include "gun.h"
#include "encoder.h"
#include "display.h"
#include "config.h"

extern I2C_HandleTypeDef 	hi2c1;

class SCRSAVER {
	public:
		SCRSAVER(void)										{ }
		void			init(uint8_t timeout)				{ to = timeout; reset(); }
		void			reset(void);
		bool 			scrSaver(void);
	private:
		uint32_t		scr_save_ms		= 0;				// Time to switch to Screen Saver mode (if > 0, ms)
		uint8_t			to				= 0;				// Timeout, minutes
		bool			scr_saver		= false;			// Is the screen saver active
};

class HW {
	public:
		HW(void) : cfg(&hi2c1),
			encoder(ENCODER_R_GPIO_Port, ENCODER_R_Pin, ENCODER_L_GPIO_Port, ENCODER_L_Pin)		{ }
		CFG_STATUS	init(void);
		CFG			cfg;
		DSPL		dspl;
		IRON		iron;
		RENC		encoder;
		HOTGUN		hotgun;
		BUZZER		buzz;
		SCRSAVER	scrsaver;
};

#endif
