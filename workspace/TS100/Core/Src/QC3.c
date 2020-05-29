/*
 * QC3.c
 *
 *  Created on: 29 May 2020
 *      Author: Ralim
 */


//Quick charge 3.0 supporting functions



#ifdef MODEL_TS80
void DPlusZero_Six() {
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);		// pull down D+
}
void DNegZero_Six() {
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_RESET);
}
void DPlusThree_Three() {
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);			// pull up D+
}
void DNegThree_Three() {
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_SET);
	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_8, GPIO_PIN_SET);
}

void QC_Seek9V() {
	DNegZero_Six();
	DPlusThree_Three();
}
void QC_Seek12V() {
	DNegZero_Six();
	DPlusZero_Six();
}
void QC_Seek20V() {
	DNegThree_Three();
	DPlusThree_Three();
}
void QC_SeekContMode() {
	DNegThree_Three();
	DPlusZero_Six();
}
void QC_SeekContPlus() {
	QC_SeekContMode();
	vTaskDelay(3);
	QC_Seek20V();
	vTaskDelay(1);
	QC_SeekContMode();
}
void QC_SeekContNeg() {
	QC_SeekContMode();
	vTaskDelay(3);
	QC_Seek12V();
	vTaskDelay(1);
	QC_SeekContMode();
}
uint8_t QCMode = 0;
uint8_t QCTries = 0;
void seekQC(int16_t Vx10, uint16_t divisor) {
	if (QCMode == 5)
		startQC(divisor);
	if (QCMode == 0)
		return;  // NOT connected to a QC Charger

	if (Vx10 < 45)
		return;
	if (xTaskGetTickCount() < 100)
		return;
	if (Vx10 > 130)
		Vx10 = 130;  //Cap max value at 13V
	// Seek the QC to the Voltage given if this adapter supports continuous mode
	// try and step towards the wanted value

	// 1. Measure current voltage
	int16_t vStart = getInputVoltageX10(divisor, 1);
	int difference = Vx10 - vStart;

	// 2. calculate ideal steps (0.2V changes)

	int steps = difference / 2;
	if (QCMode == 3) {
		if (steps > -2 && steps < 2)
			return; // dont bother with small steps
		while (steps < 0) {
			QC_SeekContNeg();
			vTaskDelay(3);
			steps++;
		}
		while (steps > 0) {
			QC_SeekContPlus();
			vTaskDelay(3);
			steps--;
		}
		vTaskDelay(10);
	}
#ifdef ENABLE_QC2
	// Re-measure
	/* Disabled due to nothing to test and code space of around 1k*/
	steps = vStart - getInputVoltageX10(divisor, 1);
	if (steps < 0)
		steps = -steps;
	if (steps > 4) {
		// No continuous mode, so QC2
		QCMode = 2;
		// Goto nearest
		if (Vx10 > 110) {
			// request 12V
			// D- = 0.6V, D+ = 0.6V
			// Clamp PB3
			QC_Seek12V();

		} else {
			// request 9V
			QC_Seek9V();
		}
	}
#endif
}
// Must be called after FreeRToS Starts
void startQC(uint16_t divisor) {
	// Pre check that the input could be >5V already, and if so, dont both
	// negotiating as someone is feeding in hv
	uint16_t vin = getInputVoltageX10(divisor, 1);
	if (vin > 100) {
		QCMode = 1;  // Already at 12V, user has probably over-ridden this
		return;
	}
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.Pin = GPIO_PIN_3;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_10;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
	//Turn off output mode on pins that we can
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Pin = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_14 | GPIO_PIN_13;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	// Tries to negotiate QC for 9V
	// This is a multiple step process.
	// 1. Set around 0.6V on D+ for 1.25 Seconds or so
	// 2. After this It should un-short D+->D- and instead add a 20k pulldown on
	// D-
	DPlusZero_Six();

	// Delay 1.25 seconds
	uint8_t enteredQC = 0;
	for (uint16_t i = 0; i < 200 && enteredQC == 0; i++) {
		vTaskDelay(1);	//10mS pause
		if (i > 130) {
			if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_11) == GPIO_PIN_RESET) {
				enteredQC = 1;
			}
			if (i == 140) {
				//For some marginal QC chargers, we try adding a pulldown
				GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
				GPIO_InitStruct.Pull = GPIO_PULLDOWN;
				GPIO_InitStruct.Pin = GPIO_PIN_11;
				HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
			}
		}
	}
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Pin = GPIO_PIN_11;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
	if (enteredQC) {
		// We have a QC capable charger
		QC_Seek9V();
		GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_10;
		GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
		HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
		QC_Seek9V();
		// Wait for frontend ADC to stabilise
		QCMode = 4;
		for (uint8_t i = 0; i < 10; i++) {
			if (getInputVoltageX10(divisor, 1) > 80) {
				// yay we have at least QC2.0 or QC3.0
				QCMode = 3;	// We have at least QC2, pray for 3
				return;
			}
			vTaskDelay(10);  // 100mS
		}
		QCMode = 5;
		QCTries++;
		if (QCTries > 10) // 10 goes to get it going
			QCMode = 0;
	} else {
		// no QC
		QCMode = 0;
	}
	if (QCTries > 10)
		QCMode = 0;
}


#endif